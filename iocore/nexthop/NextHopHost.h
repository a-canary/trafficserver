#include <atomic>
#include "PartitionedMap.h"
#include "SharedExtendible.h"
#include "ts/string_view.h"

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
std_hasher_macro(IpAddr const, ip, ip.hash());

namespace NextHop
{
/// API
using HostParam = ts::string_view const; ///< the FQDN of a host
using AddrParam = IpAdder const;         ///< an Ip address of a host (1 of many)

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
    return map.find_or_alloc(host_name, host_rec_ptr);
  }

  /// delete shared_ptr to record.
  static shared_ptr<HostRecord> &&
  destroy(HostParam &host_name)
  {
    map.pop(host_name);
  }

  /// get shared_ptr to record
  static shared_ptr<HostRecord>
  find(HostParam &host_name)
  {
    return map.find(host_name);
  }

  // restrict lifetime management
private:
  // Note, this uses SharedExtendible::new and delete to manage allocations.
  HostRecord();
  HostRecord(HostRecord &) = delete;
  ~HostRecord() {}

  static SharedMap<string, HostRecord> map;
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
    return map.find_or_alloc(addr, addr_rec_ptr);
  }

  static shared_ptr<HostRecord> &&
  destroy(AddrParam &addr)
  {
    return map.pop(addr);
  }

  static shared_ptr<AddrRecord>
  find(AddrParam &addr)
  {
    return map.find(addr);
  }

  // restrict lifetime management
private:
  // Note, this uses SharedExtendible::new and delete to manage allocations.
  AddrRecord();
  AddrRecord(AddrRecord &) = delete;
  ~AddrRecord();

  // thread safe map: addr -> addr_rec
  static SharedMap<AddrParam, AddrRecord> map;
  //^ we use shared_ptr here to prevent delete while in use.
};

}; // namespace NextHop
