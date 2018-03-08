#ifndef _Record_h
#define _Record_h

#include "stdint.h"
#include <cstddef>
#include <atomic>
#include <cstring>
#include <vector>
#include <type_traits>

#include "ts/ink_assert.h"
#include "ts/MT_hashtable.h"

/// runtime defined static container
/** The size of this structure is actually zero, so it will not change the size of your derived class.
 * But new and delete are overriden to use allocate enough bytes of the derived type + properties added
 * at run time using ioBufferAllocator.
 *
 * Also all bools are packed to save space using the *Bit methods.
 *
 * This is templated so static variables are instanced per Derived type. B/c we need to have different
 * size property sets.
 */

//     DBSchema::DBField; // a column; a run-time defined data type

namespace DB
{
struct Field;
struct Schema; // a collection of fields; a run-time defined memory map to interpret a record's memory
struct Record; // a row; a thread-locked block of type-erased memory
struct Index;  // a thread-safe map of Record shared pointers

using std;
using Offset_t = uint16_t;

struct Field {
  using Func_t = void(void *);

  Offset_t offset;
  Func_t *construct_fn;
  Func_t *destruct_fn;
  string name;
};

struct Schema {
  uint32_t total_structs_size;

  /// total bits of all declared propertyBits
  uint32_t num_packed_bits = DBRecStatusBits::num_status_bits;

  /// the number of class instances in use.
  atomic_uint instance_count;

  vector<Field> fields;

  vector<IndexManager> index_managers;
};

// make a static member of Record to define the update, add? erase?

struct IndexManager {
  vector<Function<void(self_type const &, self_type const &)>> db_index_updater;
};

template <typename Field_t>
Offset_t
addField(Schema &schema, const string &field_name)
{
  ink_assert_release(schema.instance_count == 0); // it's too late, we already started allocating.
  static_assert(is_same<Field_t, bool>::value == false,
                "Use PropBlockDeclareBit so we can pack bits, and we can't return a pointer to a bit.");
  static_assert(is_trivially_copyable<Field_t, bool>::value == true,
                "because we are type erasing, it's easier if all data is trival.");

  Offset_t offset = schema.total_structs_size;
  schema.total_structs_size += sizeof(Field_t);

  // use default constructors of class
  static auto consturct_fn = [](void *ptr) { new (ptr) Field_t; };
  static auto destruct_fn  = [](void *ptr) { reinterpret_cast<Field_t *>(ptr)->~Field_t(); };

  schema.fields.emplace_back({offset, consturct_fn, destruct_fn, field_name});
  return offset;
}

Offset_t addBit(Schema &schema, const string &name) // all bits init to 0.
{
  ink_assert_release(schema.instance_count == 0); // it's too late, we already started allocating.
  Offset_t offset = schema.num_packed_bits;
  schema.num_packed_bits++;
  return offset;
}

template <typename Field_t, class Record_t>
shared_ptr<DBIndex<Field_t, Record_t>>
addIndex(Schema &schema, Offset_t offset)
{
  ink_assert_release(m_instance_count == 0); // it's too late, we already started allocating.
  static_assert(is_same<Field_t, bool>::value == false,
                "Use PropBlockDeclareBit so we can pack bits, and we can't return a pointer to a bit.");
  static_assert(is_trivially_copyable<Field_t, bool>::value == true,
                "because we are type erasing, it's easier if all data is trival.");

  auto db_index = std::make_shared(new DBIndex<Field_t, Record_t>());

  static auto replace_fn = [db_index, offset](shared_ptr<Record_t> const &rec_old, shared_ptr<Record_t> const &rec_new) {
    Field_t const &fval_old = rec_old[offset];
    Field_t const &fval_new = rec_new[offset];
    if (fval_old == fval_new) {
      db_index->put(rec);
    } else {
      db_index->replace(fval_old, fval_new, rec_new);
    }
  };

  index_managers.emplace_back({db_index, offset});
  return db_index;
}

}; // namespace DB

bool
reset(Schema &schema)
{
  if (schema.instance_count > 0) {
    // free instances before calling this so we don't leak memory
    return false;
  }
  schema.total_structs_size = 0;
  schema.num_packed_bits    = 0;
  schema.fields.clear();
  return true;
}

size_t
recordSize(Schema const &schema)
{
  return schema.total_structs_size + (schema.m_num_packed_bits + 7) / 8;
}

//////////////////////////////////////////
//
//
temeplate<Schema &(*GetSchema)()> struct Record {
  using self_type     = Record<GetSchema>;
  using self_ptr_type = shared_ptr<self_type>;
  static Schema &
  getSchema()
  {
    return GetSchema();
  }
  // returns a typed const reference to a field (read only)
  template <typename Field_t>
  Field_t const &[](Offset_t offset) const {
    char const *ptr = static_cast<char const *>(this);
    return *static_cast<Field_t const *>(ptr + offset));
  }

  // returns a typed reference to a field (writable)
  template <typename Field_t>
  Field_t &operator[](Offset_t offset)
  {
    char *ptr = static_cast<char *>(this);
    return *static_cast<Field_t *>(ptr + offset));
  }

  bool
  unpackBit(Offset_t offset) const
  {
    char const *c = this[getSchema().total_structs_size] c += offset / 8;
    return (c & (1 << (offset % 8))) != 0;
  }

  void
  packBit(Offset_t offset, bool val)
  {
    char const *c = this[getSchema().total_structs_size] c += offset / 8;
    if (val) {
      *c |= (1 << (offset % 8));
    } else {
      *c &= ~(1 << (offset % 8));
    }
  }

  ///
  /// lifetime management
  ///

  /// allocate a new object with properties
  void *
  operator new(size_t size)
  {
    // allocate one block
    const size_t alloc_size = size + recordSize(getSchema());
    void *ptr               = ::operator new(alloc_size);

    return ptr;
  }

  Record()
  {
    getSchema().instance_count++;
    memset(ptr, 0, alloc_size);

    for (Field &field : getSchema().fields()) {
      field.construct(this[field.offset]);
    }
  }

  Record(Record const &rec)
  {
    // all fields are trivially copyable, so no need to construct
    memcpy(this, &rec, getSchema().recordSize());
  }

  ~Record()
  {
    for (Field &field : getSchema().fields()) {
      field.destruct(this[field.offset]);
    }
    getSchema().instance_count--;
  }

  class Writer : public unique_ptr<self_type>
  {
  protected:
    Lock_t lock;
    const shared_ptr<self_type> current;
    ~Writer() { lock.unlock(); }
    void
    free()
    {
      delete this;
    }
    bool
    commit()
    {
      const shared_ptr rec(get());
      for (Schema::IndexManager const &index_mgr : self_type::getSchema().index_managers) {
        const void *fval_curr = current.get()<char>[index_mgr.offset];
        const void *fval_new  = rec<char>[index_mgr.offset];
        if (memcmp(fval_curr, fval_new, index_mgr.size) == 0)) {
          index_mgr.db_index.put(fval_new, rec);
        }
        else {
          index_mgr.db_index.replace(fval_old, fval_new, rec_new);
        }
      }
    }
  };

  Writer
  writeLock()
  {
    Writer w = new Value_t(*elm);
    w.lock   = lock;
  }

  void
  writeCommit(const Key_t &key, Writer &w)
  {
    void replace(self_ptr_type const &rec_new)
    {
      Field_t const &fval_old = rec_old[offset];
      Field_t const &fval_new = rec_new[offset];
      if (fval_old == fval_new) {
        db_index.put(rec);
      } else {
        db_index.replace(fval_old, fval_new, rec_new);
      }
    }
  }
};

/// Intended to provide a thread safe lookup. Only the part of the map is locked, for the duration of the lookup.
template <typename Key_t, class Record_t>
struct DBIndex : private PartitionedMap<Key_t, const std::shared_ptr<Record_t>, std::mutex> {
};

#endif // _Record_h