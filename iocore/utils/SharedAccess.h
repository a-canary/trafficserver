#pragma once
#include <mutex>
#include <vector>
#include <memory>

using namespace std;

/// Intended to make datasets thread safe by assigning locks to stripes of data, kind of like a bloom filter.
/** Allocates a fixed number of locks and retrives one with a hash. */
template <typename Mutex_t> struct LockPool {
  LockPool(size_t num_locks) : mutexes(num_locks) {}

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

  /// please use the other constructor to define how many locks you want.
  LockPool(){};
};
