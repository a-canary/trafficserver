#pragma once
#include "NextHopHost.h"
#include "PartitionedMap.h"

/**
 * These containers allow fast lookup of host data.
 * It does not support iteration. Maintain your own container of HostNamePtr to iterate.
 * Use HostName::PropBlockDeclare(..., init, destroy) if you need to catch when host are allocated by other systems.
 */

namespace NextHop
{
}; // namespace NextHop
