#pragma once
#include <stddef.h>
#include <vapor/allocator.hpp>
#include <vapor/array_ops.hpp>
#include <vapor/common.hpp>
#include <vapor/expected.hpp>
#include <vapor/span.hpp>

namespace vapor {

enum class VectorError { OutOfMemory, OutOfBounds, EmptyVector };

template <typename T> class VAPOR_NODISCARD Vector {
public:
  typedef T value_type;
  typedef T *pointer;
  typedef const T *const_pointer;
  typedef T &reference;
  typedef const T &const_reference;
  typedef size_t size_type;

private:
  Allocator *m_allocator;
  pointer m_data;
  size_type m_length;
  size_type m_capacity;

  Expected<void, VectorError> AllocateBuffer(size_type cap) noexcept {
    if (cap == 0)
      return {};

    void *raw = m_allocator->Allocate(cap * sizeof(T), alignof(T));
    if (!raw)
      return MakeUnexpected(VectorError::OutOfMemory);

    m_data = static_cast<pointer>(raw);
    m_capacity = cap;
    return {};
  }

  struct CloneableTag {};
  struct CopyableTag {};

  template <typename U>
  VAPOR_NODISCARD Expected<Vector, VectorError>
  CloneImpl(CloneableTag, U * = nullptr) const & noexcept {
    Vector copy(*m_allocator);
    if (m_length > 0) {
      auto res = copy.reserve(m_capacity);
      if (!res)
        return MakeUnexpected(VectorError::OutOfMemory);

      for (size_type i = 0; i < m_length; ++i) {
        // Call the element's explicit .Clone() method
        auto clone_item = m_data[i].Clone();
        if (!clone_item)
          return MakeUnexpected(VectorError::OutOfMemory);

        m_allocator->Construct(&copy.m_data[i], static_cast<T &&>(*clone_item));
      }
      copy.m_length = m_length;
    }
    return copy;
  }

  template <typename U>
  VAPOR_NODISCARD Expected<Vector, VectorError>
  CloneImpl(CopyableTag, U * = nullptr) const & noexcept {
    Vector copy(*m_allocator);
    if (m_length > 0) {
      auto res = copy.reserve(m_capacity);
      if (!res)
        return MakeUnexpected(VectorError::OutOfMemory);

      for (size_type i = 0; i < m_length; ++i) {
        // Standard copy construction placement new
        m_allocator->Construct(&copy.m_data[i], m_data[i]);
      }
      copy.m_length = m_length;
    }
    return copy;
  }

  template <bool Condition, typename TrueType, typename FalseType>
  struct inline_conditional {
    typedef TrueType type;
  };

  template <typename TrueType, typename FalseType>
  struct inline_conditional<false, TrueType, FalseType> {
    typedef FalseType type;
  };

public:
  explicit Vector(Allocator &alloc = DefaultAllocator::GetDefault()) noexcept
      : m_allocator(&alloc), m_data(nullptr), m_length(0), m_capacity(0) {}

  ~Vector() {
    clear();
    if (m_data) {
      m_allocator->Deallocate(m_data);
    }
  }

  // Move Semantics (Zero allocation swap)
  Vector(Vector &&other) noexcept
      : m_allocator(other.m_allocator), m_data(other.m_data),
        m_length(other.m_length), m_capacity(other.m_capacity) {
    other.m_data = nullptr;
    other.m_length = 0;
    other.m_capacity = 0;
  }

  Vector &operator=(Vector &&other) noexcept {
    if (this != &other) {
      clear();
      if (m_data)
        m_allocator->Deallocate(m_data);

      m_allocator = other.m_allocator;
      m_data = other.m_data;
      m_length = other.m_length;
      m_capacity = other.m_capacity;

      other.m_data = nullptr;
      other.m_length = 0;
      other.m_capacity = 0;
    }
    return *this;
  }

  // Explicitly delete standard copies to avoid unexpected allocations
  Vector(const Vector &) = delete;
  Vector &operator=(const Vector &) = delete;

  VAPOR_NODISCARD Expected<Vector, VectorError> Clone() const & noexcept {
    // Compile-time tag selection based on your hybrid IsCloneable trait
    typedef typename inline_conditional<IsCloneable<T>::value, CloneableTag,
                                        CopyableTag>::type SelectionTag;
    return CloneImpl<T>(SelectionTag{});
  }

  VAPOR_NODISCARD Expected<Vector, VectorError> Clone() const && noexcept {
    return Vector(std::move(*const_cast<Vector *>(this)));
  }

  // --- Capacity Modifications ---

  Expected<void, VectorError> reserve(size_type new_capacity) noexcept {
    if (new_capacity <= m_capacity)
      return {};

    // If there's no active buffer, allocate a fresh one
    if (!m_data) {
      return AllocateBuffer(new_capacity);
    }

    // For arbitrary types T, we allocate a new storage window and
    // move-construct elements into it to preserve structural safety.
    void *raw = m_allocator->Allocate(new_capacity * sizeof(T), alignof(T));
    if (!raw)
      return MakeUnexpected(VectorError::OutOfMemory);

    pointer new_data = static_cast<pointer>(raw);
    for (size_type i = 0; i < m_length; ++i) {
      m_allocator->Construct(&new_data[i], std::move(m_data[i]));
      m_allocator->Destroy(&m_data[i]);
    }

    m_allocator->Deallocate(m_data);
    m_data = new_data;
    m_capacity = new_capacity;
    return {};
  }

  // --- Data Mutators ---

  template <typename... Args>
  Expected<void, VectorError> emplace_back(Args &&...args) noexcept {
    if (m_length >= m_capacity) {
      size_type new_cap = m_capacity == 0 ? 4 : m_capacity * 2;
      auto res = reserve(new_cap);
      if (!res)
        return MakeUnexpected(res.error());
    }

    m_allocator->Construct(&m_data[m_length], static_cast<Args &&>(args)...);
    m_length++;
    return {};
  }

  Expected<void, VectorError> push_back(const T &value) noexcept {
    return emplace_back(value);
  }

  Expected<void, VectorError> push_back(T &&value) noexcept {
    return emplace_back(std::move(value));
  }

  void pop_back() noexcept {
    if (m_length > 0) {
      m_length--;
      m_allocator->Destroy(&m_data[m_length]);
    }
  }

  Expected<void, VectorError> erase(size_type pos) noexcept {
    if (pos >= m_length)
      return MakeUnexpected(VectorError::OutOfBounds);

    m_allocator->Destroy(&m_data[pos]);

    // Shift remaining items down via move assignments
    for (size_type i = pos; i < m_length - 1; ++i) {
      m_data[i] = std::move(m_data[i + 1]);
    }

    // Destroy the redundant left-over tracking slot at the back
    m_allocator->Destroy(&m_data[m_length - 1]);
    m_length--;
    return {};
  }

  void clear() noexcept {
    for (size_type i = 0; i < m_length; ++i) {
      m_allocator->Destroy(&m_data[i]);
    }
    m_length = 0;
  }

  // --- Element Accessors ---

  VAPOR_NODISCARD size_type size() const noexcept { return m_length; }
  VAPOR_NODISCARD size_type capacity() const noexcept { return m_capacity; }
  VAPOR_NODISCARD bool empty() const noexcept { return m_length == 0; }
  VAPOR_NODISCARD const_pointer data() const noexcept { return m_data; }
  VAPOR_NODISCARD pointer data() noexcept { return m_data; }

  // Conversion straight to a non-owning Span window
  VAPOR_NODISCARD operator Span<T>() noexcept {
    return Span<T>(m_data, m_length);
  }
  VAPOR_NODISCARD operator Span<const T>() const noexcept {
    return Span<const T>(m_data, m_length);
  }

  VAPOR_NODISCARD Expected<pointer, VectorError> try_data() & noexcept {
    if (!m_data || m_length == 0)
      return MakeUnexpected(VectorError::EmptyVector);
    return m_data;
  }

  // --- Checked Exception-Free Element References ---

  VAPOR_NODISCARD
  VAPOR_CXX20_CONSTEXPR Expected<ReferenceWrapper<T>, VectorError>
  checked_at(size_type pos) noexcept {
    if (pos >= m_length)
      return MakeUnexpected(VectorError::OutOfBounds);
    return ReferenceWrapper<T>(m_data[pos]);
  }

  VAPOR_NODISCARD
  VAPOR_CXX20_CONSTEXPR Expected<ReferenceWrapper<const T>, VectorError>
  checked_at(size_type pos) const noexcept {
    if (pos >= m_length)
      return MakeUnexpected(VectorError::OutOfBounds);
    return ReferenceWrapper<const T>(m_data[pos]);
  }
};

template <typename T> struct IsCloneable<Vector<T>> {
  static const bool value = true;
};

} // namespace vapor
