#ifndef _PartitionedMap_h
#define _PartitionedMap_h

#include "stdint.h"
#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <thread>

#include "ts/ink_assert.h"

/// Intended to make datasets thread safe by assigning locks to partitions of data.
/** Allocates a fixed number of locks and retrives them with a hash. */
template <typename Mutex_t> struct LockPool {
  using Index_t = uint8_t;

  LockPool(size_t num_locks) : mutexes(num_locks) {}

  Index_t
  getIndex(size_t key_hash) const
  {
    return key_hash % m_size;
  }

  Mutex_t &
  getMutex(Index_t index) const
  {
    return mutexes[index];
  }

  Mutex_t &
  getMutex(size_t key_hash) const
  {
    return mutexes[key_hash % m_size];
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

/// Intended to provide a thread safe lookup.
// Only the part of the map is locked, for the duration of the lookup.
// The locks really only protect against rehashing the map
template <typename Key_t, typename Value_t, typename Mutex_t> struct PartitionedMap {
private:
  using Map_t  = std::unordered_map<size_t, Value_t>;
  using Lock_t = unique_lock<Mutex_t>;

public:
  PartitionedMap(size_t num_partitions) : part_maps(num_partitions), part_access(num_partitions) {}

  Lock_t &&
  lock(const decltype(LockPool)::Index_t &part_idx)
  {
    return Lock_t(part_access.getMutex(part_idx));
  }

  Map_t &
  getPartMap(const Key_t &key, Lock_t &scope_lock)
  {
    size_t hash   = std::hash<Key_t>()(key);
    auto part_idx = part_access.getIndex(hash);
    lock          = lock(part_idx);

    return part_maps[part_idx];
  }

  /// returns a value reference.
  Value_t
  find(const Key_t &key) const
  {
    // the map could rehash during a concurrent put, and mess with the elm
    Lock_t lck;
    Map_t &map  = getPartMap(key, lck);
    Value_t val = {};
    auto elm    = map.find(key);
    if (elm != map.end()) {
      val = elm->second();
    }

    return val;
  }

  // lock access and read value
  Value_t operator[](const Key_t &key) const { return find(key); }

  // lock access and reference value
  Value_t &operator[](const Key_t &key)
  {
    Lock_t lck;
    return getPartMap(key, lck)[key];
  }

  void
  put(const Key_t &key, Value_t &val)
  {
    Lock_t lck;
    getPartMap(key, lck)[key] = val;
  }

  Value_t &&
  pop(const Key_t &key)
  {
    Lock_t lck;
    Map_t &map = getPartMap(key, lck);

    Value_t val = map[key];
    map.erase(key);
    return val;
  }

  void
  clear()
  {
    for (int part_idx = 0; part_idx < part_access.size(); part_idx) {
      Lock_t lck;
      Map_t &map = getPartMap(key, lck);
      map.clear();
    }
  }

  /**
   * @brief used inplace of an iterator.
   * @param callback - processes and element. Return true if we can abort iteration.
   */
  void
  visit(std::function<bool(Key_t const &, Value_t &)> callback)
  {
    for (int part_idx = 0; part_idx < part_access.size(); part_idx) {
      Lock_t lck(part_access.getMutex(part_idx));

      for (Value_t val : part_maps[part_idx]) {
        bool done = callback(val);
        if (done) {
          return;
        }
      }
    }
  }

private:
  vector<Map_t> part_maps;
  LockPool<Mutex_t> part_access;
};
/// If you are keying on a custom class, you will need to define std::hash<Key>()
// this macro makes it easy.
#define std_hasher_macro(T, var, hash_var_expr) \
  namespace std                                 \
  {                                             \
    template <> struct hash<T> {                \
      std::size_t                               \
      operator()(const T &var) const            \
      {                                         \
        return hash_var_expr;                   \
      }                                         \
    };                                          \
  }

#endif //_PartitionedMap_h