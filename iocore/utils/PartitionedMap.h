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
  using Map_t = std::unordered_map<size_t, Value_t>;

public:
  PartitionedMap(size_t num_partitions) : part_maps(num_partitions), part_access(num_partitions) {}

  unique_lock &&
  lock(const decltype(LockPool)::Index_t &part_idx)
  {
    return unique_lock(part_access.getMutex(part_idx));
  }

  unique_lock &&
  lock(const Key_t &key)
  {
    return lock(part_access.getIndex(std::hash<Key_t>()(key)));
  }

  /// returns a value reference.
  Value_t
  find(const Key_t &key)
  {
    size_t hash     = std::hash<Key_t>()(key);
    auto part_idx   = part_access.getIndex(hash);
    unique_lock lck = lock(part_idx);

    // the map could rehash during a concurrent put, and mess with the elm

    Map_t &map  = part_maps[part_idx];
    Value_t val = {};
    auto elm    = map.find(key);
    if (elm != map.end()) {
      val = elm->second();
    }

    return val;
  }

  Value_t operator[](const Key_t &key) { return get(key); }

  void
  put(const Key_t &key, Value_t &val)
  {
    size_t hash     = std::hash<Key_t>()(key);
    auto part_idx   = part_access.getIndex(hash);
    unique_lock lck = lock(part_idx);

    part_maps[part_idx][key] = val;
  }

  Value_t &&
  pop(const Key_t &key)
  {
    size_t hash     = std::hash<Key_t>()(key);
    auto part_idx   = part_access.getIndex(hash);
    unique_lock lck = lock(part_idx);

    Value_t val = part_maps[part_idx][key];
    part_maps[part_idx].erase(key);
    return val;
  }

  void
  clear()
  {
    for (int part_idx = 0; part_idx < part_access.size(); part_idx) {
      LockGuard lock(part_access.getMutex(part_idx));
      part_maps[part_idx].clear();
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
      LockGuard lock(part_access.getMutex(part_idx));

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

/// COWMap intends to allow infinate readers, and multiple concurrent writers interface to a hashtable.
/* This data structure enforces copy-on-write on all elements, and an interface for blocking or try immediate writing.
 */
// The table only contains element pointers, and will lock access when retriving or storing the pointer.
template <typename Key_t, typename Value_t> class COWMap
{
  using COWMap_t = COWMap<Key_t, Value_t>;

  static_assert(is_copy_constructible<Value_t>::value == true, "Can't copy-on-write if we can't copy construct.");

  /**@param num_read_locks - increase this to reduce access lock contention. More partitions will cost more memory.
   * @param num_write_locks - increase this to reduce write lock contention. */
  COWMap(size_t num_read_locks = 64, size_t num_write_locks = 32) : write_locks(num_write_locks), map(num_read_locks);

  /// Get read access to a element
  const shared_ptr<Value_t> &
  get(Key_t const &key) const
  {
    return map.get(key);
  }

  /// try to get write access and
  putTry(Key_t const &key, const shared_ptr<Value_t> &current, Value_t *&write_value)
  {
    unique_lock try_lock = unique_lock(write_locks.getMutex(write_locks.getIndex(hash<Key_t>()(key)), try_to_lock_t);  
    
    if (try_lock == false) {
      return false;
    }
    if (map.get() != current) { /// the data changed since you looked at it. Try again.
      return false;
    }
    map.put(shared_ptr<Value_t>(write_value));
  }

  /// Get write access to an element
  /*  This does not block read access, but makes a copy to write to.
   * Call writeCommit when finished to update the stored data.
   */
  Writer &&
  writerLock(Key_t const &key)
  {
    // blocks until write lock is acquired for this key
    unique_lock lock = unique_lock(write_locks.getMutex(write_locks.getIndex(hash<Key_t>()(key)));  
    // auto release the lock when writer is destroyed

    Value_t const *existing_ptr = map.get(key).get();
    
    // copy the current shared data, so the readers can keep using it.
    // or allocate a new instance if we are inserting.
    Value_t *val_ptr = existing_ptr ? new Value_t(*existing_val_ptr) : new Value_t();
    
    // keep it in a unique pointer, with lock.
    return Writer(key, lock, val_ptr);
  }

  void
  pop(Key_t const &key)
  {
    unique_lock lock(write_locks.getMutex(write_locks.getIndex(hash<Key_t>()(key)));  
    return map.pop(key);
  }

  // a read-only visitor
  void
  visit(std::function<bool(Key_t const &, const shared_ptr<Value_t> &)> callback)
  {
    map.visit(callback);
  }

  void
  clear()
  {
    write_locks.lockAll();
    map.clear();
    write_locks.unlockAll();
  }

  /// combine unique_ptr and lock to serialize write operations
  /** The writer class will copy the element for editing, then store the element pointer to the map on destruction.
   *  Everything will clean up automatically.
   */
  class Writer : public unique_ptr<Value_t>
  {
  private:
    Key_t const &key; // remember where to put it.
    unique_lock lock; // block other writers
    COWMap_t *map;

  public:
    Writer() {}
    Writer(Key_t const &key, unique_lock &&lock, COWMap_t &map, Value_t *val_ptr)
      : key(key), lock(lock), map(map), unique_ptr<Value_t>(val_ptr)
    {
    }

    void
    abortCommit()
    {
      lock.release();
      delete this;
    }

    ~Writer()
    {
      if (!lock) {
        return;
      }
      // convert the writer to a reader (aka unique to shared ptr)
      const shared_ptr<Value_t> val_ptr(std::move(reinterpret_cast<unique_ptr<Value_t>>(this)));

      // store the new pointer
      map->put(key, val_ptr);
    }
  };

private:
  // once stored all data is read only
  PartitionedMap<Key_t, const shared_ptr<Value_t>> map;
  // one lock per bin of keys.
  static LockPool<std::mutex> write_locks;
}

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