#ifndef _PartitionedMap_h
#define _PartitionedMap_h

#include "stdint.h"
#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <thread>

#include "ts/ink_assert.h"

using Mutex     = std::recursive_mutex;
using LockGuard = std::lock_guard<Mutex>;

inline bool
threadHasLock(Mutex &m)
{
  return pthread_t(m.native_handle()) == pthread_self();
}

/**
 * @brief Low overhead implementation of a recursive read write lock.
 *
 * This is does deadlock if you try to acquire both a read and then a write
 * lock on the same thread.
 *
 * I copied the API from std::shared_mutex, so this can be used with std::shared_lock,
 * std::unique_lock, or std::lock_guard.
 */
class RWLock : public std::recursive_mutex
{
private:
  std::atomic<uint16_t> m_active_readers;

public:
  /// write lock
  void
  lock()
  {
    std::recursive_mutex::lock();
    for (int i = 0; m_active_readers; i++) {
      ink_assert(i != 1000); // you can deadlock here if this thread already has a read lock.
      std::this_thread::yield();
    }
  }

  bool
  try_lock()
  {
    if (!std::recursive_mutex::try_lock()) {
      return false;
    }
    if (m_active_readers) {
      std::recursive_mutex::unlock();
      return false;
    }
    return true;
  }

  /// write unlock
  void
  unlock()
  {
    std::recursive_mutex::unlock();
  }

  /// read lock
  void
  lock_shared()
  {
    std::recursive_mutex::lock();
    m_active_readers++;
    std::recursive_mutex::unlock();
  }

  /// read unlock
  void
  unlock_shared()
  {
    m_active_readers--;
  }

  /// has write lock
  bool
  has_lock()
  {
    return threadHasLock(*this) && m_active_readers == 0;
  }

#if DEBUG
  /// has read lock, ONLY use this for asserts
  bool
  has_lock_shared()
  {
    return native_handle() == native_handle_type() && m_active_readers;
  }
#endif
};

/// Intended to make datasets thread safe by assigning locks to partitions of data.
/** Allocates a fixed number of locks and retrives them with a hash. */
template <typename Mutex_t> struct LockPool {
  using Index_t = uint8_t;

  LockPool(size_t num_locks) : m_size(num_locks), m_mutex(new Mutex_t[num_locks]) {}

  Index_t
  getIndex(size_t key_hash) const
  {
    return key_hash % m_size;
  }

  Mutex_t &
  getMutex(Index_t index) const
  {
    return m_mutex[index];
  }

  size_t
  size() const
  {
    return m_size;
  }

private:
  const size_t m_size = 0;
  Mutex_t *m_mutex;

  LockPool(){};
};

/// Intended to provide a thread safe lookup. Uses one lock for the duration of the lookup.
/** we intend to use blocking locks, and for the lookup to be as fast as possible.
 * So this code will hash the key before acquiring the lock.
 *
 * @tparam Key_t      - the key of this table. Must implement std::lock<Key_t>.
 * @tparam ValuePtr_t - a pointer to the value
 */
template <typename Key_t, typename ValuePtr_t> struct LookupMap {
public:
  using Map_t = std::unordered_map<size_t, ValuePtr_t>;

  LookupMap(float _max_load_factor = 16.0f) { m_map.max_load_factor(_max_load_factor); };

  /// returns a value reference.
  ValuePtr_t
  get(const Key_t &key)
  {
    // because this a blocking lock, do the hash first
    size_t hash = std::hash<Key_t>()(key);

    m_mutex.lock();

    auto elm         = m_map.find(hash);
    const bool found = (elm != m_map.end());
    ValuePtr_t ptr   = found ? *elm : ValuePtr_t{};

    m_mutex.unlock();

    return ptr;
  }

  void
  put(const Key_t &key, ValuePtr_t &val)
  {
    // because this a blocking lock, do the hash first
    size_t hash = std::hash<Key_t>()(key);

    m_mutex.lock();

    m_map[hash] = val;

    m_mutex.unlock();
  }

  void
  erase(const Key_t &key)
  {
    size_t hash = std::hash<Key_t>()(key);

    m_mutex.lock();

    m_map.erase(hash);

    m_mutex.unlock();
  }

private:
  Map_t m_map;
  Mutex m_mutex; ///< this lock only protects the map, NOT the data it points to.
};

/// Intended to provide a thread safe lookup. Only the part of the map is locked, for the duration of the lookup.
template <typename Key_t, typename ValuePtr_t, typename Mutex_t> struct PartitionedMap {
private:
  using Map_t = std::unordered_map<size_t, ValuePtr_t>;

public:
  PartitionedMap(size_t num_partitions) : m_maps(new Map_t[num_partitions]), m_lock_pool(num_partitions) {}

  /// returns a value reference.
  ValuePtr_t
  get(const Key_t &key)
  {
    size_t hash                = std::hash<Key_t>()(key);
    auto part_idx              = m_lock_pool.getIndex(hash);
    Mutex *map_partition_mutex = m_lock_pool.getMutex(part_idx);

    map_partition_mutex->lock();

    Map_t &map     = m_maps[part_idx];
    ValuePtr_t ptr = {};
    auto elm       = map.find(hash);
    if (elm != map.end()) {
      ptr = *elm;
    }

    map_partition_mutex->unlock();

    return ptr;
  }

  void
  put(const Key_t &key, ValuePtr_t &val)
  {
    size_t hash                = std::hash<Key_t>()(key);
    auto part_idx              = m_lock_pool.getIndex(hash);
    Mutex *map_partition_mutex = m_lock_pool.getMutex(part_idx);

    map_partition_mutex->lock();

    m_maps[part_idx][hash] = val;

    map_partition_mutex->unlock();
  }

  ValuePtr_t
  visit(bool (*func)(ValuePtr_t, void *), void *ref)
  {
    for (int part_idx = 0; part_idx < m_lock_pool.size(); part_idx) {
      LockGuard part_lock(m_lock_pool.getMutex(part_idx));

      for (ValuePtr_t val : m_maps[part_idx]) {
        bool done = func(val, ref);
        if (done) {
          return val;
        }
      }
    }
  }

  ValuePtr_t
  visit(std::function<bool(ValuePtr_t &)> lambda)
  {
    for (int part_idx = 0; part_idx < m_lock_pool.size(); part_idx) {
      LockGuard lock(m_lock_pool.getMutex(part_idx));

      for (ValuePtr_t val : m_maps[part_idx]) {
        bool done = lambda(val);
        if (done) {
          return val;
        }
      }
    }
  }

private:
  Map_t m_maps[];
  LockPool<Mutex_t> m_lock_pool;
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