#include "NextHopHost.h"

/// our internal lookup maps

// thread safe map: name -> name_rec
static PartitionedMap<NameParam, shared_ptr<NameRecord>, std::mutex> NameRecords;
//^ we use shared_ptr here to prevent delete while in use.
