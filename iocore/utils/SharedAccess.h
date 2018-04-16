#pragma once
#include <mutex>
#include <vector>
#include <memory>

using namespace std;

/// Intended to make datasets thread safe by assigning locks to stripes of data.
/** Allocates a fixed number of locks and retrives them with a hash. */
template <typename Mutex_t> struct LockPool {
  using Index_t = uint8_t;

  LockPool(size_t num_locks) : mutexes(num_locks) {}

  Index_t
  getIndex(size_t key_hash) const
  {
    return key_hash % size();
  }

  Mutex_t &
  getMutex(Index_t index)
  {
    return mutexes[index];
  }

  Mutex_t &
  getMutex(size_t key_hash)
  {
    return mutexes[key_hash % size()];
  }

  size_t
  size() const
  {
    return mutexes.size();
  }

  void
  lockAll()
  {
    for (Mutex_t &m : mutexes) {
      m.lock();
    }
  }

  void
  unlockAll()
  {
    for (Mutex_t &m : mutexes) {
      m.unlock();
    }
  }

private:
  vector<Mutex_t> mutexes;

  LockPool(){};
};
