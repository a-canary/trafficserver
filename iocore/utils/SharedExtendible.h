#pragma once
#include "stdint.h"
#include <cstddef>
#include <atomic>
#include <cstring>
#include <vector>
#include <type_traits>

#include "ts/ink_assert.h"

using namespace std;

/// copies a shared data, them overwrites the
// TODO move this to new file.
static LockPool<std::mutex> copy_swap_access_locks(64);
static LockPool<std::mutex> copy_swap_write_locks(64);

template <typename T> class writer_ptr : public unique_ptr<Value_t>
{
private:
  unique_lock write_lock;        // block other writers
  const shared_ptr<T> &swap_loc; // update this pointer when done

public:
  writer_ptr() {}
  writer_ptr(const shared_ptr<T> &data_ptr) : swap_loc(data_ptr)
  {
    // get a copy lock for the memory address
    write_lock = unique_lock(copy_swap_write_locks.getMutex(static_cast<size_t>(&data_ptr)));
    // copy
    unique_ptr<Value_t>(new T(*data_ptr));
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
    unique_lock access_lock = unique_lock(copy_swap_access_locks.getMutex(static_cast<size_t>(&swap_loc)));

    // point the existing reader_ptr to the writer_ptr
    swap_loc = reinterpret_cast<unique_ptr<Value_t>>(this);
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

    BaseFieldId(string const &field_name)
    {
      auto field_iter = schema.fields.find(field_name);
      ink_release_assert(field_iter != schema.fields.end());
      offset = field_iter->offset.offset;
    }
  };
  struct AtomicFieldId : FieldBaseId {
  };
  struct BitFieldId : FieldBaseId {
  };
  struct ConstFieldId : FieldBaseId {
  };
  struct CopySwapFieldId : FieldBaseId {
  };

  /////////////////////////////////////////////////////////////////////
  /// defines a runtime "member variable", element of the blob
  struct FieldSchema {
    using Func_t = void(void *);

    FieldAccessEnum access; ///< which API is used to access the data
    FieldBaseId offset;     ///< pointer to the memory
    Func_t *construct_fn;   ///< the data type's constructor
    Func_t *destruct_fn;    ///< the data type's destructor
  };

  /////////////////////////////////////////////////////////////////////
  /// manages the subdata structures
  class Schema
  {
  private:
    map<string, FieldSchema> fields;        ///< defined elements of the blob
    uint32_t mem_sizes[NUM_ACCESS_TYPES];   ///< bytes to allocate for substructures (fields)
    uint32_t mem_offsets[NUM_ACCESS_TYPES]; ///< first byte offset of that access type
    uint32_t bit_count;                     ///< total bits to be packed down
    uint32_t alloc_size;                    ///< bytes to allocate

    atomic_uint instance_count; ///< the number of class instances in use.

    /// Constructor
    Schema() : num_packed_bits(0), alloc_size(0), instance_count(0)
    {
      mem_sizes_atomic = mem_sizes_const = mem_sizes_copyswap = mem_sizes_bits = sizeof(Derived_t);
    }

    /// Add a new Field to this record type
    template <FieldAccessEnum Access_t, typename Field_t>
    BaseFieldId
    addField(const string &field_name)
    {
      static_assert(Access_t == BIT || is_same<Field_t, bool>::value == false,
                    "Use BitField so we can pack bits, they are still atomic.");
      static_assert(Access_t != COPYSWAP || is_copy_constructible<Field_t>::value == true,
                    "can't use copyswap with a copy constructor.");

      ink_assert_release(instance_count == 0); // it's too late, we already started allocating.

      BaseFieldId field_id;
      if (Access_t == BIT) {
        field_id.offset = bit_count;          // get bit offset.
        ++bit_count;                          // inc bit offset
        mem_sizes[BIT] = (bit_count + 7) / 8; // round up to bytes
      } else {
        field_id.offset = mem_sizes[Access_t];  // get memory offset.
        mem_sizes[Access_t] += sizeof(Field_t); // increase memory counter
      }

      // update mem offsets
      {
        uint32_t mem_offset = sizeof(Derived_t);
        for (int e = 0; e < NUM_ACCESS_TYPES; e++) {
          mem_offsets[e] = mem_offset;
          mem_offset += mem_size[e];
        }
        alloc_size = mem_offset;
      }

      // capture the default constructors of the data type
      static auto consturct_fn = [](void *ptr) { new (ptr) Field_t; };
      static auto destruct_fn  = [](void *ptr) { static_cast<Field_t *>(ptr)->~Field_t(); };

      fields[field_name] = FieldSchema{offset, consturct_fn, destruct_fn, field_name};
      return offset;
    }
    //////////////////
    // API for adding fields that returns a strongly typed FieldId
  public:
    template <typename Field_t>
    AtomicFieldId
    addAtomicField(const string &field_name)
    {
      return addField<ATOMIC, atomic<Field_t>>(field_name);
    }

    template <typename Field_t>
    BitFieldId
    addBitField(const string &field_name)
    {
      return addField<BIT, Field_t>(field_name);
    }

    template <typename Field_t>
    ConstFieldId
    addConstField(const string &field_name)
    {
      return addField<CONST, Field_t>(field_name);
    }

    template <typename Field_t>
    CopySwapFieldId
    addCopySwapField(const string &field_name)
    {
      return addField<COPYSWAP, const shared_ptr<Field_t>>(field_name);
    }

    bool
    reset()
    {
      if (instance_count > 0) {
        // free instances before calling this so we don't leak memory
        return false;
      }
      for (auto &mem_size : mem_sizes) {
        mem_size = 0;
      }
      bit_count = 0;
      fields.clear();
      return true;
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
  get(AtomicFieldId field)
  {
    return static_cast<atomic<Field_t>>(this_ptr() + schema.mem_offsets[ATOMIC] + field.offset));
  }

  /// atomically read a bit value
  template <typename Field_t>
  bool const
  get(BitFieldId field) const
  {
    return readBit(field);
  }

  /// atomically read a bit value
  bool const
  readBit(BitFieldId field) const
  {
    const atomic_char &c  = static_cast<const atomic_char*>(this_ptr() + schema.mem_offsets[BIT] + field.offset / 8));
    const char mask       = 1 << (field.offset % 8);
    return (c.load() & mask) != 0;
  }

  /// atomically write a bit value
  void
  writeBit(BitFieldId field, bool const val)
  {
    atomic_char &c  = static_cast<const atomic_char*>(this_ptr() + schema.mem_offsets[BIT] + field.offset / 8));
    const char mask = 1 << (offset % 8);
    if (val) {
      c.fetch_or(mask);
    } else {
      c.fetch_and(~mask);
    }
  }

  /// return a reference to an const field
  template <typename Field_t>
  Field_t const & // value is not expected to change, or be freed while 'this' exists.
  get(ConstFieldId field) const
  {
    return *static_cast<const Field_t*>(this_ptr() + schema.mem_offsets[CONST] + field.offset));
  }

  /// return a reference to an const field that is non-const for initialization purposes
  template <typename Field_t>
  Field_t &
  init(ConstFieldId field)
  {
    return *static_cast<Field_t*>(this_ptr() + schema.mem_offsets[CONST] + field.offset));
  }

  /// return a shared pointer to last committed field value
  template <typename Field_t>
  const shared_ptr<Field_t> // shared_ptr so the value can be updated while in use.
  get(CopySwapFieldId field) const
  {
    unique_lock access_lock(copy_swap_access_locks.getMutex(this + field.offset));
    return static_cast<const shared_ptr<Field_t>>(this_ptr() + schema.mem_offsets[COPYSWAP] + field.offset));
  }

  /// return a writer created from the last committed field value
  template <typename Field_t>
  writer_ptr<Field_t> &&
  writeCopySwap(CopySwapFieldId field)
  {
    auto data_ptr = static_cast<const shared_ptr<Field_t>>(this_ptr() + schema.mem_offsets[COPYSWAP] + field.offset));
    return writer_ptr(data_ptr);
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
  SharedExtendible()
  {
    schema.instance_count++;
    memset(this_ptr() + mem_offset[BIT], 0, mem_size[BIT]);

    for (Field &field : schema.fields()) {
      if (field.access != BIT) {
        field.construct_fn(this_ptr() + mem_offset[field.access] + field.offset);
      }
    }
  }

  /// don't allow copy construct, that doesn't allow atomicity
  SharedExtendible(SharedExtendible &) = delete;

  /// destruct all fields
  ~SharedExtendible()
  {
    for (Field &field : schema.fields()) {
      if (field.access != BIT) {
        field.destruct_fn(this_ptr() + mem_offset[field.access] + field.offset);
      }
    }
    schema.instance_count--;
  }

private:
  char *
  this_ptr()
  {
    return static_cast<char *>(this);
  }
  char const *
  this_ptr() const
  {
    return static_cast<char const *>(this);
  }
};

template <typename Derived_t> Schema SharedExtendible<Derived_t>::schema;
