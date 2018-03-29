#include <atomic>
#include "PartitionedMap.h"
#include "Extendible.h"
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
std_hasher_macro(IpEndpoint, ip, ats_ip_port_hash(ip));

namespace NextHop
{
/// API
using NameParam = ts::string_view const;
using AddrParam = IpEndpoint const;

/// Internal references
using NamePtr  = const shared_ptr<string>;
using AddrList = vector<const IpEndpoint>;

struct HostRecord;
struct AddrRecord;

//////////////////////////////////////////////

/// Allows code to allocate and access data per Host. Built-in thread safety.
/**
 * @see PartitionMap allows multithread access to data.
 * @see Extendible provides interface for threadsafe reading and writing
 */
class HostRecord : public Extendible<HostRecord>
{
public:
  // to enforce all references be shared_ptr
  static shared_ptr<HostRecord> &
  create(NameParam &host_name)
  {
    // do find-or-add with one access lock
    unique_lock<mutex> lck;
    auto part_map = map.getPartMap(host_name, lck);
    auto elm      = part_map.find(key);
    if (elm != part_map.end()) {
      // return existing matching record
      return elm->second();
    }
    // allocate a new record and return it.
    shared_ptr<HostRecord> host_rec_ptr = new HostRecord();
    part_map[host_name]                 = host_rec_ptr;
    return host_rec_ptr;
  }

  /// delete shared_ptr to data, it will safely clean up later.
  void
  destroy(NameParam &host_name)
  {
    map.pop(host_name);
  }

  static shared_ptr<HostRecord>
  find(NameParam &host_name)
  {
    return map.find(host_name);
  }

  // restrict lifetime management
private:
  // Note, this uses Extendible::new and delete to manage allocations.
  HostRecord();
  HostRecord(HostRecord &) = delete;
  ~HostRecord() {}

  static PartitionedMap<string, shared_ptr<HostRecord>, std::mutex> map;
};

/// Allows code to allocate and access data per Host IpAddr. Built-in thread safety.
/**
 * @see PartitionMap allows multithread access to data.
 * @see Extendible provides interface for threadsafe reading and writing
 */
class AddrRecord : public Extendible<AddrRecord>
{
public:
  static shared_ptr<AddrRecord>
  create(AddrParam &addr)
  {
    // do find-or-add with one access lock
    unique_lock<mutex> lck;
    auto part_map = map.getPartMap(addr, lck);
    auto elm      = part_map.find(key);
    if (elm != part_map.end()) {
      // return existing matching record
      return elm->second();
    }
    // allocate a new record and return it.
    shared_ptr<AddrRecord> addr_rec_ptr = new AddrRecord();
    part_map[addr]                      = addr_rec_ptr;
    return addr_rec_ptr;
  }

  static void
  destroy(AddrParam &addr)
  {
    shared_ptr<AddrRecord> add_rec_ptr = map.pop(addr);
  }

  static shared_ptr<AddrRecord>
  find(AddrParam &addr)
  {
    return map.find(addr);
  }

  // restrict lifetime management
private:
  // Note, this uses Extendible::new and delete to manage allocations.
  AddrRecord();
  AddrRecord(AddrRecord &) = delete;
  ~AddrRecord();

  // thread safe map: addr -> addr_rec
  static PartitionedMap<AddrParam, shared_ptr<AddrRecord>, std::mutex> map;
  //^ we use shared_ptr here to prevent delete while in use.
};

}; // namespace NextHop
