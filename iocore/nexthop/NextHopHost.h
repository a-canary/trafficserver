#include <atomic>
#include "SharedMap.h"
#include "SharedExtendible.h"
#include "ts/string_view.h"
#include "ts/ink_inet.h"

/**
 * @file
 *
 * @brief this file defines structures to store data about each host and ip.
 *
 * Addr - stores a IpEndpoint, expire time, and up status. Additional properties can be defined at system start.
 *
 * Name - stores a FQDN and a list of Addr. Additional properties can be defined at system start.
 *  All references to Names will be shared pointers. They are only deleted when all references are deleted.
 *
 * Lookups are performed with one global mutex lock to protect integrity of the map.
 * @see HostLookupByName, HostLookupByAddr
 *
 * When you are reading/writing a NamePtr, ensure that you find the assigned mutex from the LockPool. This ensures a single
 * reader/writer to the Name, associated Addrs and property blocks.
 * @see Name::getMutex()
 *
 */

// Define a std::hash<>() for the key types
std_hasher_macro(IpAddr, ip, ip.hash());

namespace NextHop
{
using namespace std;
/// API
using HostParam = const string; ///< the FQDN of a host
using AddrParam = const IpAddr; ///< an Ip address of a host (1 of many)

//////////////////////////////////////////////

/// Allows code to allocate and access data per Host. Built-in thread safety.
/**
 * @see PartitionMap allows multithread access to data.
 * @see SharedExtendible provides interface for threadsafe reading and writing
 */
class HostRecord : public SharedExtendible<HostRecord>
{
public:
  /**
   * @brief find-or-add record with one access lock
   *
   * @param host_name: the FQDN of the host
   * @param host_rec_ptr = the record with that name (existing or new)
   * @return true: host_rec_ptr existed
   * @return false: host_rec_ptr is new
   */
  static bool
  find_or_alloc(HostParam &host_name, shared_ptr<HostRecord> &host_rec_ptr)
  {
    return map.find_or_alloc({host_name, Hash32fnv(host_name)}, host_rec_ptr);
  }

  /// delete shared_ptr to record.
  static shared_ptr<HostRecord>
  destroy(HostParam &host_name)
  {
    return map.pop({host_name, Hash32fnv(host_name)});
  }

  /// get shared_ptr to record
  static shared_ptr<HostRecord>
  find(HostParam &host_name)
  {
    return map.find({host_name, Hash32fnv(host_name)});
  }
  // restrict lifetime management
protected:
  // Note, this uses SharedExtendible::new and delete to manage allocations.
  HostRecord();
  HostRecord(HostRecord &) = delete;

  static SharedMap<KeyHashed<HostParam>, HostRecord> map;
  friend SharedMap<KeyHashed<HostParam>, HostRecord>; // allow the map to allocate
};

/// Allows code to allocate and access data per Host IpAddr. Built-in thread safety.
/**
 * @see PartitionMap allows multithread access to data.
 * @see SharedExtendible provides interface for threadsafe reading and writing
 * Extend by calling AddRecord::schema.addField<T>()
 */
class AddrRecord : public SharedExtendible<AddrRecord>
{
public:
  /**
   * @brief find-or-add record with one access lock
   *
   * @param addr: the IP:port of the host
   * @param addr_rec_ptr = the record with that addr (existing or new)
   * @return true: addr_rec_ptr existed
   * @return false: addr_rec_ptr is new
   */
  static bool
  create(AddrParam &addr, shared_ptr<AddrRecord> &addr_rec_ptr)
  {
    return map.find_or_alloc({addr, addr.hash()}, addr_rec_ptr);
  }

  static shared_ptr<AddrRecord>
  destroy(AddrParam &addr)
  {
    return map.pop({addr, addr.hash()});
  }

  static shared_ptr<AddrRecord>
  find(AddrParam &addr)
  {
    return map.find({addr, addr.hash()});
  }

  // restrict lifetime management
protected:
  // Note, this uses SharedExtendible::new and delete to manage allocations.
  AddrRecord();
  AddrRecord(AddrRecord &) = delete;

  // thread safe map: addr -> addr_rec
  static SharedMap<KeyHashed<AddrParam>, AddrRecord> map;
  friend SharedMap<KeyHashed<AddrParam>, AddrRecord>; // allow the map to allocate
};

}; // namespace NextHop
