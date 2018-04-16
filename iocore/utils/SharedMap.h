#pragma once
#include "stdint.h"
#include <cstddef>
#include <mutex>
// TODO: #include <shared_mutex>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

#include "ts/ink_assert.h"

#include "SharedAccess.h"

using namespace std;

/// Intended to provide a thread safe lookup.
// Only the part of the map is locked, for the duration of the lookup.
// The locks really only protect against rehashing the map
template <typename Key_t, typename Value_t, typename Mutex_t> struct PartitionedMap {
protected:
  using Map_t        = std::unordered_map<Key_t, Value_t>;
  using AccessLock_t = unique_lock<Mutex_t>; // TODO: shared_lock<Mutex_t>; // shared access when reading data
  using ResizeLock_t = unique_lock<Mutex_t>; // exclusive access when adding or removing data

public:
  PartitionedMap(size_t num_partitions) : part_maps(num_partitions), part_access(num_partitions) {}

  Map_t &
  getPartMap(const Key_t &key, ResizeLock_t &lock)
  {
    size_t hash   = std::hash<Key_t>()(key);
    auto part_idx = part_access.getIndex(hash);
    lock          = ResizeLock_t(part_access.getMutex(part_idx));

    return part_maps[part_idx];
  }

  /*TODO: shared_lock<Mutex_t>
  Map_t const &
  getPartMap(const Key_t &key, AccessLock_t &lock)
  {
    size_t hash   = std::hash<Key_t>()(key);
    auto part_idx = part_access.getIndex(hash);
    lock          = AccessLock_t(part_access.getMutex(part_idx));

    return part_maps[part_idx];
  }
*/
  /// returns a value reference.
  Value_t
  find(const Key_t &key)
  {
    // the map could rehash during a concurrent put, and mess with the elm
    AccessLock_t lck;
    Value_t val      = {};
    Map_t const &map = getPartMap(key, lck);
    auto elm         = map.find(key);
    if (elm != map.end()) {
      val = elm->second;
    }

    return val;
  }

  // lock access and read value
  Value_t operator[](const Key_t &key) const { return find(key); }

  // lock access and reference value
  Value_t &operator[](const Key_t &key)
  {
    ResizeLock_t lck;
    return getPartMap(key, lck)[key];
  }

  void
  put(const Key_t &key, Value_t &val)
  {
    ResizeLock_t lck;
    getPartMap(key, lck)[key] = val;
  }

  Value_t
  pop(const Key_t &key)
  {
    ResizeLock_t lck;
    Map_t &map = getPartMap(key, lck);

    Value_t val = map[key];
    map.erase(key);
    return val;
  }

  void
  clear()
  {
    for (int part_idx = 0; part_idx < part_access.size(); part_idx) {
      ResizeLock_t lck(part_access.getMutex(part_idx));
      part_maps[part_idx].clear();
    }
  }

  /**
   * @brief used inplace of an iterator.
   * @param callback - processes and element. Return true if we can abort iteration.
   */
  void
  visit(function<bool(Key_t const &, Value_t &)> callback)
  {
    for (int part_idx = 0; part_idx < part_access.size(); part_idx) {
      AccessLock_t lck(part_access.getMutex(part_idx));
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

/// SharedMap stores all values as shared pointers so you don't worry about data being destroyed while in use.
template <typename Key_t, typename Value_t> class SharedMap : public PartitionedMap<Key_t, shared_ptr<Value_t>, std::mutex>
{
  using Base_t = PartitionedMap<Key_t, shared_ptr<Value_t>, std::mutex>;
  friend Base_t;

public:
  SharedMap(size_t num_partitions) : Base_t(num_partitions) {}

  /// return true if it found existing value
  /// return false if allocated new value
  bool
  find_or_alloc(Key_t const &key, shared_ptr<Value_t> &val_ptr)
  {
    typename Base_t::ResizeLock_t lck;
    typename Base_t::Map_t &map = Base_t::getPartMap(key, lck);
    auto elm                    = map.find(key);
    if (elm != map.end()) {
      val_ptr = elm->second;
      return true;
    }
    val_ptr  = shared_ptr<Value_t>(new Value_t{});
    map[key] = val_ptr;
    return false;
  }
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

// TODO: remove this
uint32_t
Hash32fnv(std::string const &s)
{
  uint32_t hval = 0;
  for (const char &c : s) {
    hval *= 0x01000193;
    hval ^= (uint32_t)c;
  }
  return hval;
}

//////////////////////////////////////////////
/// KeyHashed - a key and a pre-hashed value to use for faster maps.
template <typename KeyParam> struct KeyHashed {
  KeyParam key;
  uint32_t hash;
  KeyHashed(KeyParam const &_key) : key(_key), hash(std::hash<KeyParam>()(_key)) {}
  KeyHashed(KeyParam const &_key, uint32_t _hash) : key(_key), hash(_hash) {}
};

template <typename KeyParam>
inline bool
operator==(KeyHashed<KeyParam> const &a, KeyHashed<KeyParam> const &b)
{
  return a.hash == b.hash && a.key == b.key;
}

namespace std
{
template <typename KeyParam> struct hash<KeyHashed<KeyParam>> {
  std::size_t
  operator()(const KeyHashed<KeyParam> &var) const
  {
    return var.hash;
  }
};
} // namespace std
/////////////////////////////////////////////
