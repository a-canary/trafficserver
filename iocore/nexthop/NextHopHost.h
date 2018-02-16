#include <atomic>
#include "I_EventSystem.h"
#include "PartitionedMap.h"
#include "PropertyBlock.h"
#include "ts/string_view.h"

/**
 * @file
 *
 * @brief this file defines structures to store data about each host and ip.
 *
 * HostAddr - stores a IpEndpoint, expire time, and up status. Additional properties can be defined at system start.
 *
 * HostName - stores a FQDN and a list of HostAddr. Additional properties can be defined at system start.
 *  All references to HostNames will be shared pointers. They are only deleted when all references are deleted.
 *
 * Lookups are performed with one global mutex lock to protect integrity of the map.
 * @see HostLookupByName, HostLookupByAddr
 *
 * When you are reading/writing a HostNamePtr, ensure that you find the assigned mutex from the LockPool. This ensures a single
 * reader/writer to the HostName, associated HostAddrs and property blocks.
 * @see HostName::getMutex()
 *
 */

// Define a std::hash<>() for the lookmaps
std_hasher_macro(IpEndpoint, ip, ats_ip_port_hash(ip));

namespace NextHop
{
using Mutex = RWLock;

/// uid for a host that should persist between updates and reloads.
/// It is prefered for other systems to cache HostID instead of a HostNamePtr.
using HostID = size_t;

struct HostName;
using HostNamePtr = std::shared_ptr<HostName>;

/// lookup HostID, direct string hash
HostID getHostID(ts::string_view hostname);

/// lookup HostID with map
HostID getHostID(IpEndpoint addr);

/// lookup HostNamePtr
HostNamePtr getHost(HostID);
HostNamePtr getHost(IpEndpoint addr);
HostNamePtr getHost(ts::string_view hostname);

/// our internal lookup maps
// TODO move to .cc
LookupMap<HostID, HostNamePtr> HostLookupByNameHash;
LookupMap<IpEndpoint, HostNamePtr> HostLookupByAddr;

/// Defines a host address, status, and a block of associated properties.
/** Only variables need by TSCore are in this structure.
 * Plugins should define property blocks.
 * @see PropBlockDeclare
 */
struct HostAddr : public PropertyBlock<HostAddr> {
  const IpEndpoint m_addr;      ///< ip and port
  std::atomic_uint m_eol;       ///< "end of life" time, set by DNS time to live
  std::atomic_bool m_available; ///< true when this IP is available for use

  HostAddr(const IpEndpoint &ip_addr, HostNamePtr host) : m_addr(ip_addr) { HostLookupByAddr.put(ip_addr, host); }

  // Note, this uses PropertyBlock::new and delete to manage.

private:
  HostAddr();
};

//////////////////////////////////////////////

/// Defines a host (name), group of IPs, and block of associated properties.
/** Only variables need by TSCore are in the structure.
 * Plugins can use property blocks to store host associated data.
 * @see PropBlockDeclare
 */
class HostName : public PropertyBlock<HostName>
{
public:
  // to enforce all references be shared_ptr
  static HostNamePtr
  alloc(ts::string_view hostname)
  {
    HostID id = std::hash<ts::string_view>()(hostname);

    // get a lock incase we are allocating hosts in parallel
    LockGuard part_lock(s_lock_pool.getMutex(s_lock_pool.getIndex(id)));

    // return existing host by name, to prevent duplicates
    HostNamePtr existing_host = getHost(id);
    if (existing_host) {
      // increment the count of systems that require this to exist.
      existing_host->m_strong_ref_count++;
      return existing_host;
    }

    HostNamePtr host(new HostName(hostname)); ///< call PropertyBlock::operator new
    host->propBlockInit();

    HostLookupByNameHash.put(id, host);
    return host;
  }

  void
  free()
  {
    ink_assert(m_strong_ref_count > 0); // we are calling 'free' more than 'alloc'
    ink_assert(hasWriteAccess(this));   // get the lock first, getMutex().lock()

    // decrement the count of systems that require this to exist.
    m_strong_ref_count--;
    if (m_strong_ref_count > 0) {
      return;
    }

    // start cleaning up this reference
    HostID id = std::hash<std::string>()(m_name);
    HostLookupByNameHash.erase(id);
    propBlockDestroy();
    getMutex().unlock();

    // it will delete after shared_ptr's are cleared,
    // most of which should have happend in the above propBlockDestroy()
    // or when current
  }

  ~HostName() { ink_assert(m_strong_ref_count == 0); }

#if DEBUG
  static bool
  hasReadAccess(const HostName *host)
  {
    auto m = host->getMutex();
    return m.has_lock_shared() || m.has_lock();
  }
  static bool
  hasWriteAccess(const HostName *host)
  {
    return host->getMutex().has_lock();
  }
#endif
  /// ----------------------------
  /// Thread Safe Methods
  ///

  /// thread management
  Mutex &
  getMutex() const
  {
    return s_lock_pool.getMutex(m_lock_idx);
  }

  /// get the FQDN that defines this host.
  const ts::string_view
  getName() const
  {
    return ts::string_view(m_name);
  }

  /// ----------------------------
  /// Requires Lock Methods
  ///

  /// Address editing and processing
  HostAddr *addAddr(IpEndpoint &addr);
  HostAddr *getAddr(IpEndpoint &addr);
  const std::vector<HostAddr *> &getAddrList(IpEndpoint &addr);

  /// remove all data associated with this host
  void
  reset()
  {
    ink_assert(hasWriteAccess(this));
    propBlockDestroy();
    m_addrs.clear();
    propBlockInit();
  }

private:
  HostName();
  HostName(ts::string_view name)
    : m_name(name.data(), name.size()), m_lock_idx(std::hash<ts::string_view>()(name)), m_strong_ref_count(1)
  {
  }
  // member variables

  const std::string m_name;                   ///< the FQDN that defines this host
  const LockPool<RWLock>::Index_t m_lock_idx; ///< reference to lock.
  std::atomic_bool m_has_lock;                ///< track if lock is already acquired
  std::atomic<uint8_t> m_strong_ref_count;    ///< the number of systems that need this to persist
  std::vector<HostAddr *> m_addrs;            ///< hosts can have multiple IPs.

  /// a shared lock pool for all HostNames
  static LockPool<RWLock> s_lock_pool;
};

}; // namespace NextHop
