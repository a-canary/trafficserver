#pragma once
#include "stdint.h"
#include <cstddef>
#include <atomic>
#include <cstring>
#include <unordered_map>
#include <type_traits>
#include <mutex>

#include "ts/ink_assert.h"
#include "SharedAccess.h"

using namespace std;

/// copies a shared data, them overwrites the
// TODO move this to new file.
using WriterMutex_t = std::mutex;
using WriterLock_t  = unique_lock<WriterMutex_t>;
static LockPool<WriterMutex_t> copy_swap_access_locks(64);
static LockPool<WriterMutex_t> copy_swap_write_locks(64);

template <typename T> class writer_ptr : public unique_ptr<T>
{
private:
  WriterLock_t write_lock;       // block other writers
  const shared_ptr<T> &swap_loc; // update this pointer when done

public:
  writer_ptr() {}
  writer_ptr(const shared_ptr<T> &data_ptr) : swap_loc(data_ptr)
  {
    // get write access to the memory address
    write_lock = WriterLock_t(copy_swap_write_locks.getMutex(static_cast<size_t>(&data_ptr)));
    // copy the data
    this = unique_ptr<T>{new T(*data_ptr)};
  }

  void
  abort()
  {
    write_lock.release();
    delete this;
  }

  ~writer_ptr()
  {
    if (!write_lock) {
      return;
    }

    // get a swap lock for the pointer
    WriterLock_t access_lock = WriterLock_t(copy_swap_access_locks.getMutex(static_cast<size_t>(&swap_loc)));

    // point the existing reader_ptr to the writer_ptr
    swap_loc = reinterpret_cast<unique_ptr<T>>(this);
  }
};

/// runtime defined static container
/** The size of this structure is actually zero, so it will not change the size of your derived class.
 * But new and delete are overriden to use allocate enough bytes of the derived type + properties added
 *
 * Also all bools are packed to save space using the *Bit methods.
 *
 * This is templated so static variables are instanced per Derived type. B/c we need to have different
 * size property sets.
 */

/*

atomic<T> &a = HostRecords[host_name][atomic_field]

const T *b = HostRecords[host_name][cow_field];
const T *c = HostRecords[host_name][const_field];

Writer<T> d = HostRecords->writeLock(host_name);
HostRecords->writeCommit(d);


+Add field::is_atomic to allow get, block get and writeLock
+when allocating non-atomic type, allocate a size of shared_ptr, Writer class will


*/

/**
 * @brief Allows code (and Plugins) to declare member variables during system init.
 *
 * @tparam Derived_t - the class that you want to extend at runtime.
 *
 * This is focused on thread safe data types that allow minimally blocked reading.
 */
template <typename Derived_t> struct SharedExtendible {
  using SharedExtendible_t = SharedExtendible<Derived_t>;

  enum FieldAccessEnum { ATOMIC, BIT, CONST, COPYSWAP, NUM_ACCESS_TYPES }; ///< all types must allow unblocking MT read access

  /////////////////////////////////////////////////////////////////////
  /// strongly type the FieldId to avoid branching and switching
  struct BaseFieldId {
    uint8_t offset;
    static const int INVALID = 255;
    BaseFieldId(uint8_t _offset) : offset(_offset) {}

    static BaseFieldId
    find(string const &field_name)
    {
      auto field_iter = schema.fields.find(field_name);
      ink_release_assert(field_iter != schema.fields.end());
      return field_iter->offset.offset;
    }
  };

  template <FieldAccessEnum FieldAccess_e, typename Field_t> struct FieldId : private BaseFieldId {
    FieldId() : BaseFieldId(BaseFieldId::INVALID) {}
    FieldId(BaseFieldId const &id) : BaseFieldId(id.offset) {}
    friend SharedExtendible_t;
  };
  using BitFieldId = FieldId<BIT, bool>;

  /////////////////////////////////////////////////////////////////////
  /// defines a runtime "member variable", element of the blob
  struct FieldSchema {
    using Func_t = void(void *);

    FieldAccessEnum access; ///< which API is used to access the data
    uint8_t offset;         ///< pointer to the memory used for BaseFieldId
    Func_t *construct_fn;   ///< the data type's constructor
    Func_t *destruct_fn;    ///< the data type's destructor
  };

  /////////////////////////////////////////////////////////////////////
  /// manages the subdata structures
  class Schema
  {
    friend SharedExtendible_t;

  private:
    unordered_map<string, FieldSchema> fields;        ///< defined elements of the blob
    unordered_map<uint32_t, uint32_t> mem_size_count; ///< bytes to allocate for substructures (fields)
    unordered_map<uint32_t, uint32_t> mem_offsets;    ///< bytes to allocate for substructures (fields)
    uint32_t bit_count;                               ///< total bits to be packed down
    uint32_t alloc_size;                              ///< bytes to allocate

    atomic_uint instance_count; ///< the number of class instances in use.

  public:
    /// Constructor
    Schema() : bit_count(0), alloc_size(sizeof(Derived_t)), instance_count(0) {}

    /// Add a new Field to this record type
    template <FieldAccessEnum Access_t, typename Field_t>
    bool
    addField(FieldId<Access_t, Field_t> &field_id, const string &field_name)
    {
      static_assert(Access_t == BIT || is_same<Field_t, bool>::value == false,
                    "Use BitField so we can pack bits, they are still atomic.");
      static_assert(Access_t != COPYSWAP || is_copy_constructible<Field_t>::value == true,
                    "can't use copyswap with a copy constructor.");

      ink_release_assert(instance_count == 0); // it's too late, we already started allocating.

      if (Access_t == BIT) {
        field_id.offset = bit_count;             // get bit offset.
        ++bit_count;                             // inc bit offset
        mem_size_count[0] = (bit_count + 7) / 8; // round up to bytes
      }
      if (Access_t == ATOMIC) {
        size_t size     = max(sizeof(atomic<Field_t>), alignof(atomic<Field_t>));
        field_id.offset = size * mem_size_count[size]++; // get memory offset.
                                                         // and increase memory counter
      } else {
        field_id.offset = sizeof(Field_t) * mem_size_count[sizeof(Field_t)]++; // get memory offset.
                                                                               // and increase memory counter
      }

      // update mem offsets
      {
        for (auto elm1 : mem_size_count) {
          uint32_t &mem_offset = mem_offsets[elm1.first];
          mem_offset           = sizeof(Derived_t) + sizeof(Derived_t) % 8;
          for (auto elm2 : mem_size_count) {
            if (elm1.first < elm2.first) {
              mem_offset += elm2.first * elm2.second; // shift by size and count
            }
          }
        }

        alloc_size = mem_offsets[0] + mem_size_count[0];
      }

      // capture the default constructors of the data type
      static auto consturct_fn = [](void *ptr) { new (ptr) Field_t; };
      static auto destruct_fn  = [](void *ptr) { static_cast<Field_t *>(ptr)->~Field_t(); };

      fields[field_name] = FieldSchema{Access_t, field_id.offset, consturct_fn, destruct_fn};
      return true;
    }

    bool
    reset()
    {
      if (instance_count > 0) {
        // free instances before calling this so we don't leak memory
        return false;
      }
      mem_size_count.clear();
      mem_offsets.clear();
      fields.clear();
      bit_count  = 0;
      alloc_size = 0;
      return true;
    }

    void
    call_construct(char *ext_as_char_ptr)
    {
      instance_count++;
      memset(ext_as_char_ptr + mem_offsets[0], 0, mem_size_count[0]);

      for (auto const &elm : fields) {
        FieldSchema const &field_schema = elm.second;
        if (field_schema.access != BIT) {
          field_schema.construct_fn(ext_as_char_ptr + mem_offsets[field_schema.access] + field_schema.offset);
        }
      }
    }

    void
    call_destruct(char *ext_as_char_ptr)
    {
      for (auto const &elm : fields) {
        FieldSchema const &field_schema = elm.second;
        if (field_schema.access != BIT) {
          field_schema.destruct_fn(ext_as_char_ptr + mem_offsets[field_schema.access] + field_schema.offset);
        }
      }
      instance_count--;
    }
  }; // end Schema struct

  //////////////////////////////////////////
  // SharedExtendible static data
  /// one schema instance per Derived_t to define contained fields
  static Schema schema;

  //////////////////////////////////////////
  /// SharedExtendible Methods

  /// return a reference to an atomic field (read, write or other atomic operation)
  template <typename Field_t>
  atomic<Field_t> &
  get(FieldId<ATOMIC, Field_t> const &field)
  {
    return new (this_as_char_ptr() + schema.mem_offsets[sizeof(Field_t)] + field.offset) atomic<Field_t>;
  }

  /// atomically read a bit value
  bool const
  get(BitFieldId field) const
  {
    return readBit(field);
  }

  /// atomically read a bit value
  bool const
  readBit(BitFieldId field) const
  {
    const char &c   = *(this_as_char_ptr() + schema.mem_offsets[0] + field.offset / 8);
    const char mask = 1 << (field.offset % 8);
    return (c & mask) != 0;
  }

  /// atomically write a bit value
  void
  writeBit(BitFieldId field, bool const val)
  {
    char &c         = *(this_as_char_ptr() + schema.mem_offsets[0] + field.offset / 8);
    const char mask = 1 << (field.offset % 8);
    if (val) {
      c |= mask;
    } else {
      c &= ~mask;
    }
  }

  /// return a reference to an const field
  template <typename Field_t>
  Field_t const & // value is not expected to change, or be freed while 'this' exists.
  get(FieldId<CONST, Field_t> field) const
  {
    return *static_cast<const Field_t *>(this_as_char_ptr() + schema.mem_offsets[sizeof(Field_t)] + field.offset);
  }

  /// return a reference to an const field that is non-const for initialization purposes
  template <typename Field_t>
  Field_t &
  init(FieldId<CONST, Field_t> field)
  {
    return *static_cast<Field_t *>(this_as_char_ptr() + schema.mem_offsets[sizeof(Field_t)] + field.offset);
  }

  /// return a shared pointer to last committed field value
  template <typename Field_t>
  const shared_ptr<Field_t> // shared_ptr so the value can be updated while in use.
  get(FieldId<COPYSWAP, Field_t> field) const
  {
    WriterLock_t access_lock(copy_swap_access_locks.getMutex(this + field.offset));
    return static_cast<const shared_ptr<Field_t>>(this_as_char_ptr() + schema.mem_offsets[sizeof(Field_t)] + field.offset);
  }

  /// return a writer created from the last committed field value
  template <typename Field_t>
  writer_ptr<Field_t> &&
  writeCopySwap(FieldId<COPYSWAP, Field_t> field)
  {
    auto data_ptr = static_cast<const shared_ptr<Field_t>>(this_as_char_ptr() + schema.mem_offsets[sizeof(Field_t)] + field.offset);
    return writer_ptr<Field_t>(data_ptr);
  }

  ///
  /// lifetime management
  ///

  /// allocate a new object with properties
  void *
  operator new(size_t size)
  {
    // allocate one block for all the memory, including the derived_t members
    return ::operator new(schema.alloc_size);
  }

  /// construct all fields
  SharedExtendible() { schema.call_construct(this_as_char_ptr()); }

  /// don't allow copy construct, that doesn't allow atomicity
  SharedExtendible(SharedExtendible &) = delete;

  /// destruct all fields
  ~SharedExtendible() { schema.call_destruct(this_as_char_ptr()); }

private:
  char *
  this_as_char_ptr()
  {
    return static_cast<char *>(static_cast<void *>(this));
  }
  char const *
  this_as_char_ptr() const
  {
    return static_cast<const char *>(static_cast<const void *>(this));
  }
};

template <typename Derived_t> typename SharedExtendible<Derived_t>::Schema SharedExtendible<Derived_t>::schema;
