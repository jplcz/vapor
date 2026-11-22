#pragma once
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/expected.hpp>
#include <vapor/string.hpp>
#include <vapor/string_builder.hpp>
#include <vapor/string_util.hpp>
#include <vapor/string_view.hpp>

// Functions that produce a new String from a StringView.
//
// Why: StringView is read-only, so "replace this word", "make it lower case"
// need a new buffer, and allocating can fail. These helpers do the common
// cases once, report OutOfMemory through Expected instead of throwing, and
// accept an allocator.
//
// How:
//   Expected<String, CommonError> s = ReplaceAll("a-b-c", "-", "+"); // a+b+c
//   Expected<String, CommonError> l = ToLowerCase("HeLLo");          // hello
//   Expected<String, CommonError> r = Repeat("ab", 3);               // ababab
//
// Case conversion is ASCII only (bytes of multi-byte UTF-8 characters are
// left untouched), so it is locale independent and safe on any text.

namespace vapor {

// Replaces every non-overlapping occurrence of `from` (scanned left to right)
// by `to`. An empty `from` returns an unchanged copy.
VAPOR_NODISCARD inline Expected<String, CommonError>
ReplaceAll(StringView text, StringView from, StringView to,
           Allocator &alloc = DefaultAllocator::GetDefault()) {
  StringBuilder sb(alloc);
  size_t pos = 0;
  while (from.size() != 0) {
    size_t hit = Find(text, from, pos);
    if (hit == StringView::npos)
      break;
    Expected<void, CommonError> r =
        sb.Append(StringView(text.data() + pos, hit - pos));
    if (r)
      r = sb.Append(to);
    if (!r)
      return MakeUnexpected(r.error());
    pos = hit + from.size();
  }
  Expected<void, CommonError> r =
      sb.Append(StringView(text.data() + pos, text.size() - pos));
  if (!r)
    return MakeUnexpected(r.error());
  return sb.Take();
}

VAPOR_NODISCARD inline Expected<String, CommonError>
ToLowerCase(StringView text,
            Allocator &alloc = DefaultAllocator::GetDefault()) {
  StringBuilder sb(alloc);
  for (size_t i = 0; i < text.size(); ++i) {
    Expected<void, CommonError> r = sb.Append(AsciiToLower(text[i]));
    if (!r)
      return MakeUnexpected(r.error());
  }
  return sb.Take();
}

VAPOR_NODISCARD inline Expected<String, CommonError>
ToUpperCase(StringView text,
            Allocator &alloc = DefaultAllocator::GetDefault()) {
  StringBuilder sb(alloc);
  for (size_t i = 0; i < text.size(); ++i) {
    Expected<void, CommonError> r = sb.Append(AsciiToUpper(text[i]));
    if (!r)
      return MakeUnexpected(r.error());
  }
  return sb.Take();
}

VAPOR_NODISCARD inline Expected<String, CommonError>
Repeat(StringView text, size_t count,
       Allocator &alloc = DefaultAllocator::GetDefault()) {
  if (text.size() != 0 && count > VAPOR_SIZE_MAX / 2 / text.size())
    return MakeUnexpected(CommonError::OutOfMemory);
  StringBuilder sb(alloc);
  // Reserve first so an impossible request fails at once, not after filling
  // memory.
  Expected<void, CommonError> rr = sb.reserve(text.size() * count);
  if (!rr)
    return MakeUnexpected(rr.error());
  for (size_t i = 0; i < count; ++i) {
    Expected<void, CommonError> r = sb.Append(text);
    if (!r)
      return MakeUnexpected(r.error());
  }
  return sb.Take();
}

} // namespace vapor
