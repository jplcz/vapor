#pragma once
#include <vapor/allocator.hpp>
#include <vapor/common.hpp>
#include <vapor/expected.hpp>
#include <vapor/string.hpp>
#include <vapor/string_builder.hpp>
#include <vapor/string_view.hpp>
#include <vapor/vector.hpp>

// Lexical path helpers (no file system access).
//
// Why: building and taking apart file paths with ad-hoc string slicing is a
// common source of bugs ("a//b", trailing slashes, ".." escaping a directory).
// These functions work on text only: they never touch the disk and never
// follow symbolic links, so the answers are predictable and testable.
//
// How:
//   PathBasename("/usr/lib/libm.so.6")  -> "libm.so.6"
//   PathDirname("/usr/lib/libm.so.6")   -> "/usr/lib"
//   PathExtension("archive.tar.gz")     -> ".gz"
//   PathStem("archive.tar.gz")          -> "archive.tar"
//   PathJoin("a/b", "c")                -> "a/b/c"  (Expected<String>)
//   PathNormalize("a/./b/../c//d")      -> "a/c/d"  (Expected<String>)
//
// Only '/' separates components (the POSIX form, also accepted by Windows
// APIs). The View-returning functions give a StringView into the argument;
// they follow Python's os.path rules: "a/b/" has an empty basename.

namespace vapor {

VAPOR_NODISCARD inline bool PathIsAbsolute(StringView p) noexcept {
  return !p.empty() && p[0] == '/';
}

// Text after the last '/' ("" when the path ends with '/').
VAPOR_NODISCARD inline StringView PathBasename(StringView p) noexcept {
  size_t i = p.size();
  while (i > 0 && p[i - 1] != '/')
    --i;
  return StringView(p.data() + i, p.size() - i);
}

// Everything before the last '/', without trailing slashes; "" for a bare
// name and "/" for a file in the root.
VAPOR_NODISCARD inline StringView PathDirname(StringView p) noexcept {
  size_t i = p.size();
  while (i > 0 && p[i - 1] != '/')
    --i;
  size_t end = i; // `end` is just past the separator
  while (end > 1 && p[end - 1] == '/')
    --end;
  return StringView(p.data(), end);
}

// Extension of the basename including the dot (".gz"); "" when there is none
// or the only dot starts the name (".bashrc").
VAPOR_NODISCARD inline StringView PathExtension(StringView p) noexcept {
  StringView base = PathBasename(p);
  size_t i = base.size();
  while (i > 0 && base[i - 1] != '.')
    --i;
  if (i <= 1) // no dot, or a leading dot only
    return StringView(base.data() + base.size(), 0);
  return StringView(base.data() + i - 1, base.size() - i + 1);
}

// Basename without its extension.
VAPOR_NODISCARD inline StringView PathStem(StringView p) noexcept {
  StringView base = PathBasename(p);
  return StringView(base.data(), base.size() - PathExtension(p).size());
}

// Joins with a single '/'; if `b` is absolute it replaces `a`.
VAPOR_NODISCARD inline Expected<String, CommonError>
PathJoin(StringView a, StringView b,
         Allocator &alloc = DefaultAllocator::GetDefault()) {
  StringBuilder sb(alloc);
  Expected<void, CommonError> r;
  if (PathIsAbsolute(b) || a.empty()) {
    r = sb.Append(b);
  } else {
    r = sb.Append(a);
    if (r && a[a.size() - 1] != '/' && !b.empty())
      r = sb.Append('/');
    if (r)
      r = sb.Append(b);
  }
  if (!r)
    return MakeUnexpected(r.error());
  return sb.Take();
}

// Removes "." components and repeated slashes and resolves ".." where
// possible. ".." that would climb above the root of an absolute path is
// dropped; in a relative path leading ".." components are kept. An empty
// result is ".".
VAPOR_NODISCARD inline Expected<String, CommonError>
PathNormalize(StringView p, Allocator &alloc = DefaultAllocator::GetDefault()) {
  Vector<StringView> parts(alloc);
  bool absolute = PathIsAbsolute(p);
  size_t i = 0;
  while (i < p.size()) {
    while (i < p.size() && p[i] == '/')
      ++i;
    size_t start = i;
    while (i < p.size() && p[i] != '/')
      ++i;
    StringView comp(p.data() + start, i - start);
    if (comp.empty() || comp == StringView("."))
      continue;
    if (comp == StringView("..")) {
      if (!parts.empty() && !(parts.back() == StringView(".."))) {
        parts.pop_back();
      } else if (!absolute) {
        Expected<void, CommonError> r = parts.push_back(comp);
        if (!r)
          return MakeUnexpected(r.error());
      }
      continue;
    }
    Expected<void, CommonError> r = parts.push_back(comp);
    if (!r)
      return MakeUnexpected(r.error());
  }
  StringBuilder sb(alloc);
  Expected<void, CommonError> r;
  if (absolute)
    r = sb.Append('/');
  for (size_t k = 0; r && k < parts.size(); ++k) {
    if (k != 0)
      r = sb.Append('/');
    if (r)
      r = sb.Append(parts[k]);
  }
  if (r && parts.empty() && !absolute)
    r = sb.Append('.');
  if (!r)
    return MakeUnexpected(r.error());
  return sb.Take();
}

} // namespace vapor
