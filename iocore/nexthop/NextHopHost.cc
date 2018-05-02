#include "NextHopHost.h"

using namespace NextHop;

// Define a std::hash<>() for the key types
std_hasher_macro(IpAddr, ip, ip.hash());

/// internal lookup maps

// thread safe map: host_name -> host_rec
static const int Host_lock_pool_size = 64;
SharedMap<KeyHashed<HostParam>, HostRecord> HostRecord::map(Host_lock_pool_size);

// thread safe map: IpAddr -> addr_rec
static const int Addr_lock_pool_size = 64;
SharedMap<KeyHashed<AddrParam>, AddrRecord> AddrRecord::map(Addr_lock_pool_size);