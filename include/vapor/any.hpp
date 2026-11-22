#pragma once
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/detail/storage.hpp>
#include <vapor/expected.hpp>
#include <vapor/reference_wrapper.hpp>
#include <vapor/type_traits.hpp>
#include <vapor/utility.hpp>

// Any: a container that holds one value of any type, and TypeId<T>(), a
// run-time identifier of a type, both without RTTI (run-time type information)
// or exceptions.
//
// Why: sometimes the type of a value is only known by the code that stored it:
// a property bag, a plugin context pointer, user data attached to a node, a
// message payload that is handed through generic code. `void *` loses the
// type and the cleanup; Any remembers both, so a wrong guess is caught
// instead of being undefined behaviour.
//
// How:
//
//   Expected<Any, CommonError> a = Any::Make<int>(42);   // may allocate
//   Any &x = a.value();
//   int *p = AnyCast<int>(&x);      // nullptr when it holds something else
//   x.is<int>();   x.has_value();   x.reset();
//   x.emplace<String>("text");      // replaces the contents, Expected
//
// Small types (up to three pointers in size) are stored inside the Any;
// larger ones in one allocation from the allocator you pass to Make/emplace.
// Any is move-only, because copying a held value may allocate and fail. If
// the held type is copyable, Clone() makes a copy (Expected); otherwise it
// returns UnsupportedOperation. Not thread-safe.
//
// TypeId<T>() returns a pointer that is unique per type (T, const T and T&
// are all different). It is stable inside one program image; do not store it
// in files or compare it across shared-library boundaries that carry their own
// copy of the header.

namespace vapor {

template <typename T> VAPOR_NODISCARD const void *TypeId() noexcept {
  static const char id = 0;
  return &id;
}

class Any {
  static constexpr size_t kInlineSize = 3 * sizeof(void *);

  struct VTable {
    const void *type;
    bool is_inline;
    void (*destroy)(Any &self);
    // Moves the value of `src` into the empty `dst` and leaves `src` empty.
    void (*move_to)(Any &dst, Any &src);
    // Copies the value; returns UnsupportedOperation for non-copyable types.
    Expected<void, CommonError> (*clone_to)(const Any &src, Any &dst);
  };

  const VTable *m_vt;
  Allocator *m_alloc;
  alignas(void *) unsigned char m_buf[kInlineSize];

  template <typename T> struct FitsInline {
    static constexpr bool value =
        sizeof(T) <= kInlineSize && alignof(T) <= alignof(void *);
  };

  void *HeapPtr() const noexcept {
    void *p;
    memcpy(&p, m_buf, sizeof p);
    return p;
  }
  void SetHeapPtr(void *p) noexcept { memcpy(m_buf, &p, sizeof p); }
  void *Storage() noexcept {
    return m_vt->is_inline ? static_cast<void *>(m_buf) : HeapPtr();
  }
  const void *Storage() const noexcept {
    return m_vt->is_inline ? static_cast<const void *>(m_buf) : HeapPtr();
  }

  template <typename T, bool Inline> struct Ops;

  template <typename T>
  static Expected<void, CommonError> CloneImpl(const Any &src, Any &dst,
                                               TrueType) {
    Expected<ReferenceWrapper<T>, CommonError> r =
        dst.template emplace<T>(*static_cast<const T *>(src.Storage()));
    if (!r)
      return MakeUnexpected(r.error());
    return {};
  }
  template <typename T>
  static Expected<void, CommonError> CloneImpl(const Any &, Any &, FalseType) {
    return MakeUnexpected(CommonError::UnsupportedOperation);
  }

  template <typename T> static const VTable *InlineTable() noexcept {
    static const VTable vt = {TypeId<T>(), true, &Ops<T, true>::Destroy,
                              &Ops<T, true>::MoveTo, &Ops<T, true>::CloneTo};
    return &vt;
  }
  template <typename T> static const VTable *HeapTable() noexcept {
    static const VTable vt = {TypeId<T>(), false, &Ops<T, false>::Destroy,
                              &Ops<T, false>::MoveTo, &Ops<T, false>::CloneTo};
    return &vt;
  }

public:
  Any() noexcept : m_vt(nullptr), m_alloc(nullptr) {}
  Any(Any &&o) noexcept : m_vt(nullptr), m_alloc(o.m_alloc) {
    if (o.m_vt)
      o.m_vt->move_to(*this, o);
  }
  Any &operator=(Any &&o) noexcept {
    if (this != &o) {
      reset();
      m_alloc = o.m_alloc;
      if (o.m_vt)
        o.m_vt->move_to(*this, o);
    }
    return *this;
  }
  Any(const Any &) = delete;
  Any &operator=(const Any &) = delete;
  ~Any() { reset(); }

  template <typename T, typename... Args>
  VAPOR_NODISCARD static Expected<Any, CommonError> Make(Args &&...args) {
    return MakeIn<T>(DefaultAllocator::GetDefault(), Forward<Args>(args)...);
  }
  template <typename T, typename... Args>
  VAPOR_NODISCARD static Expected<Any, CommonError> MakeIn(Allocator &alloc,
                                                           Args &&...args) {
    Any a;
    a.m_alloc = &alloc;
    Expected<ReferenceWrapper<T>, CommonError> r =
        a.emplace<T>(Forward<Args>(args)...);
    if (!r)
      return MakeUnexpected(r.error());
    return Move(a);
  }

  // Destroys the current value, then constructs a T from `args` (using the
  // allocator the Any already has, or the default one). On failure the Any is
  // empty.
  template <typename T, typename... Args>
  Expected<ReferenceWrapper<T>, CommonError> emplace(Args &&...args) {
    static_assert(!IsReference<T>::value, "Any cannot hold a reference");
    static_assert(IsSame<T, RemoveCVT<T>>::value,
                  "Any cannot hold a cv-qualified type");
    Allocator *alloc = m_alloc ? m_alloc : &DefaultAllocator::GetDefault();
    reset();
    m_alloc = alloc;
    return EmplaceImpl<T>(IntegralConstant<bool, FitsInline<T>::value>(),
                          Forward<Args>(args)...);
  }

  void reset() noexcept {
    if (m_vt) {
      m_vt->destroy(*this);
      m_vt = nullptr;
    }
  }

  VAPOR_NODISCARD bool has_value() const noexcept { return m_vt != nullptr; }
  VAPOR_NODISCARD const void *type_id() const noexcept {
    return m_vt ? m_vt->type : nullptr;
  }
  template <typename T> VAPOR_NODISCARD bool is() const noexcept {
    return m_vt && m_vt->type == TypeId<T>();
  }

  VAPOR_NODISCARD Expected<Any, CommonError> Clone() const {
    Any out;
    out.m_alloc = m_alloc;
    if (!m_vt)
      return Move(out);
    Expected<void, CommonError> r = m_vt->clone_to(*this, out);
    if (!r)
      return MakeUnexpected(r.error());
    return Move(out);
  }

  void swap(Any &o) noexcept {
    Any tmp(Move(o));
    o = Move(*this);
    *this = Move(tmp);
  }

  // Raw access used by AnyCast.
  template <typename T> T *Get() noexcept {
    return is<T>() ? static_cast<T *>(Storage()) : nullptr;
  }
  template <typename T> const T *Get() const noexcept {
    return is<T>() ? static_cast<const T *>(Storage()) : nullptr;
  }

private:
  template <typename T, typename... Args>
  Expected<ReferenceWrapper<T>, CommonError> EmplaceImpl(TrueType,
                                                         Args &&...args) {
    T *p = detail::ConstructAt<T>(static_cast<void *>(m_buf),
                                  Forward<Args>(args)...);
    m_vt = InlineTable<T>();
    return ReferenceWrapper<T>(*p);
  }
  template <typename T, typename... Args>
  Expected<ReferenceWrapper<T>, CommonError> EmplaceImpl(FalseType,
                                                         Args &&...args) {
    void *mem = m_alloc->Allocate(sizeof(T), alignof(T));
    if (!mem)
      return MakeUnexpected(CommonError::OutOfMemory);
    T *p = detail::ConstructAt<T>(mem, Forward<Args>(args)...);
    SetHeapPtr(mem);
    m_vt = HeapTable<T>();
    return ReferenceWrapper<T>(*p);
  }
};

template <typename T> struct Any::Ops<T, true> {
  static void Destroy(Any &self) {
    detail::DestroyAt(static_cast<T *>(self.Storage()));
  }
  static void MoveTo(Any &dst, Any &src) {
    detail::ConstructAt<T>(static_cast<void *>(dst.m_buf),
                           Move(*static_cast<T *>(src.Storage())));
    dst.m_vt = src.m_vt;
    src.reset();
  }
  static Expected<void, CommonError> CloneTo(const Any &src, Any &dst) {
    return CloneImpl<T>(src, dst, IsCopyConstructible<T>());
  }
};
template <typename T> struct Any::Ops<T, false> {
  static void Destroy(Any &self) {
    void *mem = self.HeapPtr();
    detail::DestroyAt(static_cast<T *>(mem));
    self.m_alloc->Deallocate(mem, sizeof(T));
  }
  static void MoveTo(Any &dst, Any &src) {
    dst.SetHeapPtr(src.HeapPtr());
    dst.m_vt = src.m_vt;
    src.m_vt = nullptr; // ownership of the allocation moved
  }
  static Expected<void, CommonError> CloneTo(const Any &src, Any &dst) {
    return CloneImpl<T>(src, dst, IsCopyConstructible<T>());
  }
};

// Pointer to the held T, or nullptr when `a` is null or holds another type.
template <typename T> VAPOR_NODISCARD T *AnyCast(Any *a) noexcept {
  return a ? a->template Get<T>() : nullptr;
}
template <typename T> VAPOR_NODISCARD const T *AnyCast(const Any *a) noexcept {
  return a ? a->template Get<T>() : nullptr;
}

} // namespace vapor
