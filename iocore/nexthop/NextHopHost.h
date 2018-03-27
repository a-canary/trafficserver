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
  static CopySwapFieldId fld_host_name = scheme.addField<AddrList, COPYSWAP>("addr_list");

  // to enforce all references be shared_ptr
  static shared_ptr<HostRecord>
  create(NameParam &host_name)
  {
    shared_ptr<HostRecord> name_rec_ptr = find(host_name);
    if (name_rec_ptr) {
      return name_rec_ptr;
    }

    name_rec_ptr = new HostRecord();
    map.put(host_name, name_rec_ptr);
    return name_rec_ptr;
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

private:
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
  /// Fields - extendible members

  /// add a host_name field for reverse lookups
  // using shared pointer so you don't have to allocate a string per ip
  static ConstFieldId fld_host_name = AddrRecord.scheme.addField<shared_ptr<string>, CONST>("host_name");

  static shared_ptr<AddrRecord>
  create(AddrParam &addr, NamePtr name_ptr)
  {
    ink_assert(!find(addr)); // handle existing records before calling this

    auto name_rec = HostRecord::find(*name_ptr);
    ink_assert(name_rec); // assume HostRecord exist, to force single creation code path
    {                     // insert the addr from the HostRecord in a copy-on-write style
      auto addr_list_writer = name_rec->writeCopySwap<AddList>("addr_list"); /// creates new instance
      addr_list_writer->push_back(addr);
    } // commit addr_list_writer at end of scope

    auto addr_rec = new AddrRecord();
    addr_rec[fld_host_name].init(name_ptr);
    AddrRecords.put(addr, shared_ptr<AddrRecord>(addr_rec));
    return addr_rec;
  }

  static void
  destroy(AddrParam &addr)
  {
    { // remove the addr from the HostRecord in a copy-on-write style
      auto addr_list_writer = NameRecords[this->host_name].writeCopySwap<AddList>("addr_list"); /// creates new addr list instance
      addr_list_writer->remove(addr);
    }

    AddrRecords.pop(addr);
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
