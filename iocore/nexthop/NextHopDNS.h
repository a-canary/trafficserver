#include "NextHopHost.h"

using namespace std;

static const string Host_fld_name_add_list = "addr_list";
static CopySwapFieldId Host_fld_addr_list  = HostRecord::scheme.addField<AddrList, COPYSWAP>(Host_fld_name_add_list);

static const string Addr_fld_name_host_name = "host_name";
static CopySwapFieldId Addr_fld_host_name   = AddrRecord::scheme.addField<AddrList, COPYSWAP>(Addr_fld_name_host_name);

void
pair_host_addr(string const &host_name, shared_ptr<HostRecord> &host_rec, IpAddr const &addr, shared_ptr<AddrRecord> &addr_rec)
{
  // insert the addr into the HostRecord in a copy-on-write style
  auto addr_list_writer = host_rec->writeCopySwap<AddList>(Host_fld_addr_list); // copies instance
  addr_list_writer->push_back(addr);
  // commit addr_list_writer at end of scope

  addr_rec->init(Addr_fld_host_name) = host_name;
}

{ // remove the addr from the HostRecord in a copy-on-write style
  auto addr_list_writer = NameRecords[this->host_name].writeCopySwap<AddList>("addr_list"); /// creates new addr list instance
  addr_list_writer->remove(addr);
}