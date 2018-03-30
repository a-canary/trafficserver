#include "NextHopHost.h"

using namespace std;
using namespace NextHop;

/// Internal references
using AddrList = vector<AddrParam>;

static const string Host_fld_name_dns_add_list = "dns_addr_list";
static auto Fld_addr_list                      = HostRecord::scheme.addCopySwapField<AddrList>(Host_fld_name_add_list);

static const string Addr_fld_name_dns_host_name = "dns_host_name";
static auto Fld_host_name                       = AddrRecord::scheme.addCopySwapField<string>(Addr_fld_name_host_name);

bool
update_host_addr(string const &host_name, AddrList &&addr_list)
{
  sort(addr_list.begin(), addr_list.end());

  // get old addr_list
  auto host_rec = HostRecord::find(host_name);
  if (!host_rec) {
    return false;
  }

  // delete discarded addr
  for (AddrParam &existing_addr : *host_rec->get<AddrList>(Fld_addr_list)) {
    addr_list.find(existing_addr) if () {}
  }

  for (AddrParam &addr : addr_list) {
    shared_ptr<AddrRecord> addr_rec = AddrRecord::find(addr);
    if (addr_rec) {
      if (addr_rec->get(Fld_host_name) == host_name) {
        continue;
      } else {
        // delete everything we know about this IP, because it is used by a different host now
        AddrRecord::destroy(addr);
      }
    }

    // create new addr
    if (AddrRecord::create(addr, addr_rec)) {
      addr_rec->init(Fld_host_name) = host_name;
    }
  }

  // write addr_list
  host_rec->writeCopySwap(Fld_addr_list) = std::move(addr_list);

  if (!AddrRecord::create(addr, addr_rec)) {
    // process if

    // already set?
    if (host_name == *addr_rec->get<string>(Addr_fld_dns_host_name)) {
      return;
    }

    // is this ip paired with another host?
    auto ip_host_name_wtr = addr_rec->writeCopySwap<string>(Addr_fld_dns_host_name);
    string old_name       = std::move(*ip_host_name_wtr);
    if (exiting_host_name.len() > 0) {
      // remove the addr from the HostRecord in a copy-on-write style
      auto old_rec = HostRecord::find(old_name);
      if (old_rec) {
        old_rec->writeCopySwap<AddList>("addr_list")
          ->remove(addr); /// use a writer to copy-on-write a new list without this address.
      }
    }
  }
}

// insert the addr into the HostRecord in a copy-on-write style
auto addr_list_writer = host_rec -> writeCopySwap<AddList>(Host_fld_addr_list); // copies instance
addr_list_writer->push_back(addr);
// commit addr_list_writer at end of scope

addr_rec->init(Addr_fld_host_name) = host_name;
}
