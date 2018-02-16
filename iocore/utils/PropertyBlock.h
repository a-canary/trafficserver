#pragma once
#ifndef _PropertyBlock_h
#define _PropertyBlock_h

#include "stdint.h"
#include <cstddef>
#include <atomic>
#include <cstring>
#include <vector>
#include <type_traits>

#include "ts/ink_assert.h"

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
template <class Derived_t> ///< static members instance pey type.
struct PropertyBlock {
public:
  using self_t = PropertyBlock<Derived_t>;
  /// callback format used to PropBlock init and destroy
  using PropertyFunc_t = void(Derived_t *, void *);

  /// number of bytes from 'this', or the bitset index
  using Offset_t = uint32_t;

  /// Add a data type to schema.
  /**
   * @param prop_count the number of instances of this data type to allocate.
   * @param init an initialization function for property
   * @param destroy a destruction function for property
   * Note: if prop_count is 0, the init and destroy are still called per instance.
   */
  template <typename Property_t>
  static Offset_t
  PropBlockDeclare(size_t prop_count, PropertyFunc_t *init, PropertyFunc_t *destroy)
  {
    ink_assert(s_instance_count == 0); // it's too late, we already started allocating.
    static_assert(std::is_same<Property_t, bool>::value == false,
                  "Use PropBlockDeclareBit so we can pack bits, and we can't return a pointer to a bit.");

    Offset_t offset = sizeof(Derived_t) + s_properties_total_size;
    s_properties_total_size += prop_count * sizeof(Property_t);
    if (init || destroy) {
      for (Offset_t head = offset; prop_count > 0; head += sizeof(Property_t)) {
        getSchema().push_back((Block){head, init, destroy});
        --prop_count;
      }
    }
    return offset;
  }

  template <class Property_t>
  static Offset_t
  PropBlockDeclare(size_t prop_count)
  {
    static auto default_init    = [](Derived_t *, void *ptr) { new (ptr) Property_t; };
    static auto default_destroy = [](Derived_t *, void *ptr) { reinterpret_cast<Property_t *>(ptr)->~Property_t(); };
    // use default constructors of class
    return PropBlockDeclare<Property_t>(prop_count, default_init, default_destroy);
  }

  template <typename Property_t>
  Property_t const &
  propRead(Offset_t offset) const
  {
    ink_assert(Derived_t::hasReadAccess(static_cast<const Derived_t *>(this))); // the Derived_t probably needs a thread lock
    ink_assert(propGetBit(initialized) == true); // the Derived_t needs to call propBlockInit() during construction.
    return *static_cast<Property_t *>(propBlockGetPtr(offset));
  }

  template <typename Property_t>
  Property_t &
  propWrite(Offset_t offset)
  {
    ink_assert(Derived_t::hasWriteAccess(static_cast<Derived_t *>(this))); // the Derived_t probably needs a thread lock
    ink_assert(propGetBit(initialized) == true); // the Derived_t needs to call propBlockInit() during construction.
    return *static_cast<Property_t *>(propBlockGetPtr(offset));
  }

  static Offset_t PropBlockDeclareBit(int bit_count = 1) // all bits init to 0. there is no init & destroy.
  {
    ink_assert(s_instance_count == 0); // it's too late, we already started allocating.
    Offset_t offset = s_bits_size;
    s_bits_size += bit_count;
    return offset;
  }

  bool
  propGetBit(Offset_t offset) const
  {
    ink_assert(Derived_t::hasReadAccess(static_cast<const Derived_t *>(this))); // the Derived_t probably needs a thread lock
    ink_assert(offset < status_bits::num_status_bits ||
               propGetBit(initialized) == true); // the Derived_t needs to call propBlockInit() during construction.
    char &c = *(char *)propBlockGetPtr(sizeof(Derived_t) + s_properties_total_size + offset / 8);
    return (c & (1 << (offset % 8))) != 0;
  }

  void
  propPutBit(Offset_t offset, bool val)
  {
    ink_assert(Derived_t::hasWriteAccess(static_cast<Derived_t *>(this))); // the Derived_t probably needs a thread lock
    ink_assert(offset < status_bits::num_status_bits ||
               propGetBit(initialized) == true); // the Derived_t needs to call propBlockInit() during construction.
    char &c = *(char *)propBlockGetPtr(sizeof(Derived_t) + s_properties_total_size + offset / 8);
    if (val) {
      c |= (1 << (offset % 8));
    } else {
      c &= ~(1 << (offset % 8));
    }
  }

  ///
  /// lifetime management
  ///
  /** If you have to override the new/delete of the derived class be sure to
   * call these PropertyBlock::alloc(sz) and PropertyBlock::free(ptr)
   */

  /// allocate a new object with properties
  void *
  operator new(size_t size)
  {
    // allocate one block
    const size_t alloc_size = size + s_properties_total_size + (s_bits_size + 7) / 8;
    void *ptr               = ::operator new(alloc_size);
    memset(ptr, 0, alloc_size);

    return ptr;
  }

  PropertyBlock() { s_instance_count++; }
  // we have to all propBlockInit after the Derived contructor. IDK how to do that implicitly.

  ~PropertyBlock()
  {
    s_instance_count--;
    propBlockDestroy();
  }

  static bool
  resetSchema()
  {
    if (s_instance_count > 0) {
      // free instances before calling this so we don't leak memory
      return false;
    }
    s_properties_total_size = 0;
    s_bits_size             = status_bits::num_status_bits;
    getSchema().clear();
    return true;
  }

protected:
  void
  propBlockInit()
  {
    // is already constructed? prevents recursion
    if (propGetBit(initialized)) {
      return;
    }
    propPutBit(initialized, true);
    for (Block &block : getSchema()) {
      if (block.m_init) {
        block.m_init(static_cast<Derived_t *>(this), propBlockGetPtr(block.m_offset));
      }
    }
  }

  void
  propBlockDestroy()
  {
    // is already destroyed? prevents recursion
    if (propGetBit(destroyed)) {
      return;
    }
    propPutBit(destroyed, true);
    for (Block &block : getSchema()) {
      if (block.m_destroy) {
        block.m_destroy(static_cast<Derived_t *>(this), propBlockGetPtr(block.m_offset));
      }
    }
  }

  /// returns a pointer at the given offset
  inline void *
  propBlockGetPtr(Offset_t offset) const
  {
    return ((char *)this) + offset;
  }

  static bool
  hasReadAccess(const PropertyBlock *pb)
  {
    // Derived_t should override this static member function if it uses locking
    return true;
  }
  static bool
  hasWriteAccess(const PropertyBlock *pb)
  {
    // Derived_t should override this static member function if it uses locking
    return true;
  }

private:
  /// total size of all declared property structures
  static uint32_t s_properties_total_size;

  enum status_bits {
    initialized, // true if the propBlock is initialized, false if destroyed.
    destroyed,
    num_status_bits
  };
  /// total bits of all declared propertyBits
  static uint32_t s_bits_size;

  /// the number of class instances in use.
  static std::atomic_uint s_instance_count;

  struct Block {
    Offset_t m_offset;
    PropertyFunc_t *m_init;    ///< called after new
    PropertyFunc_t *m_destroy; ///< called before delete

    // if we need to save and load, then we should add names to the blocks.
  };
  static std::vector<Block> &
  getSchema()
  {
    static std::vector<Block> s_schema;
    return s_schema;
  }
};

/// total size of all declared property structures
template <class Derived_t> uint32_t PropertyBlock<Derived_t>::s_properties_total_size = 0;

/// total bits of all declared propertyBits
template <class Derived_t> uint32_t PropertyBlock<Derived_t>::s_bits_size = PropertyBlock<Derived_t>::status_bits::num_status_bits;

/// the number of class instances in use.
template <class Derived_t> std::atomic_uint PropertyBlock<Derived_t>::s_instance_count{0};

// template <class Derived_t> std::vector<PropertyBlock<Derived_t>::Block> PropertyBlock<Derived_t>::s_schema;
// ^ this doesn't work. So I created getSchema() to define a static member.

#endif // _PropertyBlock_h