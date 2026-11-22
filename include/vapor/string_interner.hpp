#pragma once
#include <stdint.h>
#include <string.h>
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/expected.hpp>
#include <vapor/hash.hpp>
#include <vapor/hash_map.hpp>
#include <vapor/string_view.hpp>
#include <vapor/vector.hpp>

// StringInterner: stores each distinct string once and hands out a small
// integer id for it.
//
// Why: programs that deal with many repeated names (identifiers in a parser,
// property names, tags, resource paths) waste memory keeping a copy of the
// same text everywhere, and compare them by walking characters. If every
// name is interned, equal names have equal ids, so comparing is one integer
// comparison, ids can index arrays directly, and the text exists exactly once.
//
// How:
//   StringInterner names;
//   Expected<uint32_t, CommonError> a = names.Intern("position");
//   Expected<uint32_t, CommonError> b = names.Intern("position");
//   // a.value() == b.value()
//   StringView text = names.Get(a.value());   // stable until names is
//                                             // destroyed or cleared
//   names.Find("rotation");                    // Expected id, ValueNotFound
//
// Interning can fail only when memory runs out (OutOfMemory) or after about
// 4 billion distinct strings (CapacityExceeded); on failure nothing changes.
// The views returned by Get() never move, even while more strings are added,
// and are NUL-terminated (`data()` is a valid C string). Not thread-safe.

namespace vapor {

class StringInterner {
public:
  typedef uint32_t Id;

private:
  struct Chunk {
    char *data;
    size_t size;
  };

  Allocator *m_alloc;
  Vector<Chunk> m_chunks;
  size_t m_used;
  Vector<StringView> m_views;
  HashMap<StringView, Id> m_map;

  static size_t ChunkSize() noexcept { return 4096; }

  // Copies `text` (plus a NUL) into stable storage.
  Expected<const char *, CommonError> Store(StringView text) noexcept {
    size_t need = text.size() + 1;
    if (text.size() >= VAPOR_SIZE_MAX / 2)
      return MakeUnexpected(CommonError::OutOfMemory);
    if (m_chunks.empty() || m_chunks.back().size - m_used < need) {
      size_t sz = need > ChunkSize() ? need : ChunkSize();
      void *p = m_alloc->Allocate(sz, 1);
      if (!p)
        return MakeUnexpected(CommonError::OutOfMemory);
      Chunk c = {static_cast<char *>(p), sz};
      if (!m_chunks.push_back(c)) {
        m_alloc->Deallocate(p, sz);
        return MakeUnexpected(CommonError::OutOfMemory);
      }
      m_used = 0;
    }
    char *dst = m_chunks.back().data + m_used;
    if (!text.empty())
      memcpy(dst, text.data(), text.size());
    dst[text.size()] = '\0';
    m_used += need;
    return static_cast<const char *>(dst);
  }

  void FreeChunks() noexcept {
    for (size_t i = 0; i < m_chunks.size(); ++i)
      m_alloc->Deallocate(m_chunks[i].data, m_chunks[i].size);
    m_chunks.clear();
    m_used = 0;
  }

public:
  explicit StringInterner(
      Allocator &alloc = DefaultAllocator::GetDefault()) noexcept
      : m_alloc(&alloc), m_chunks(alloc), m_used(0), m_views(alloc),
        m_map(alloc) {}

  StringInterner(const StringInterner &) = delete;
  StringInterner &operator=(const StringInterner &) = delete;
  StringInterner(StringInterner &&o) noexcept
      : m_alloc(o.m_alloc), m_chunks(Move(o.m_chunks)), m_used(o.m_used),
        m_views(Move(o.m_views)), m_map(Move(o.m_map)) {
    o.m_used = 0;
  }
  ~StringInterner() { FreeChunks(); }

  VAPOR_NODISCARD size_t size() const noexcept { return m_views.size(); }
  VAPOR_NODISCARD bool empty() const noexcept { return m_views.empty(); }

  // Returns the id of `text`, adding it first if it is new.
  VAPOR_NODISCARD Expected<Id, CommonError> Intern(StringView text) noexcept {
    typename HashMap<StringView, Id>::iterator it = m_map.find(text);
    if (it != m_map.end())
      return it->second;
    if (m_views.size() >= static_cast<size_t>(0xFFFFFFFFu))
      return MakeUnexpected(CommonError::CapacityExceeded);

    // Make every fallible step happen before anything is recorded.
    Expected<void, CommonError> r = m_views.reserve(m_views.size() + 1);
    if (!r)
      return MakeUnexpected(r.error());
    r = m_map.reserve(m_views.size() + 1);
    if (!r)
      return MakeUnexpected(r.error());
    Expected<const char *, CommonError> stored = Store(text);
    if (!stored)
      return MakeUnexpected(stored.error());

    StringView view(stored.value(), text.size());
    Id id = static_cast<Id>(m_views.size());
    // Room was reserved above, so these cannot fail.
    Expected<void, CommonError> p1 = m_views.push_back(view);
    (void)p1;
    typename HashMap<StringView, Id>::InsertResult p2 =
        m_map.try_emplace(view, id);
    (void)p2;
    return id;
  }

  VAPOR_NODISCARD Expected<Id, CommonError>
  Find(StringView text) const noexcept {
    typename HashMap<StringView, Id>::const_iterator it = m_map.find(text);
    if (it == m_map.end())
      return MakeUnexpected(CommonError::ValueNotFound);
    return it->second;
  }

  // The text for an id; aborts for an id that was never returned.
  VAPOR_NODISCARD StringView Get(Id id) const noexcept {
    return m_views.at(id);
  }
  VAPOR_NODISCARD Expected<StringView, CommonError>
  checked_get(Id id) const noexcept {
    if (id >= m_views.size())
      return MakeUnexpected(CommonError::OutOfBounds);
    return m_views[id];
  }

  // Forgets all strings. Previously returned views become invalid.
  void clear() noexcept {
    m_map.clear();
    m_views.clear();
    FreeChunks();
  }
};

} // namespace vapor
