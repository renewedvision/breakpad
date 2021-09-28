// Copyright (c) 2021 Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef COMMON_STRINGREF_H__
#define COMMON_STRINGREF_H__

#include "common/using_std_string.h"
#include "cstring"

namespace google_breakpad {
/// StringRef - Represent a constant reference to a string, i.e. a character
/// array and a length, which need not be null terminated.
///
/// This class does not own the string data, it is expected to be used in
/// situations where the character data resides in some other buffer, whose
/// lifetime extends past that of the StringRef. For this reason, it is not in
/// general safe to store a StringRef.
class StringRef {
 private:
  /// The start of the string, in an external buffer.
  const char* data_ = nullptr;

  /// The length of the string.
  size_t length_ = 0;

  // Workaround memcmp issue with null pointers (undefined behavior)
  // by providing a specialized version
  static int compareMemory(const char* Lhs, const char* Rhs, size_t Length) {
    if (Length == 0) {
      return 0;
    }
    return ::memcmp(Lhs, Rhs, Length);
  }

 public:
  /// Construct an empty string ref.
  /*implicit*/ StringRef() = default;

  /// Disable conversion from nullptr.  This prevents things like
  /// if (S == nullptr)
  StringRef(nullptr_t) = delete;

  /// Construct a string ref from a cstring.
  /*implicit*/ constexpr StringRef(const char* Str)
      : data_(Str), length_(Str ? strlen(Str) : 0) {}

  /// Construct a string ref from a pointer and length.
  /*implicit*/ constexpr StringRef(const char* data, size_t length)
      : data_(data), length_(length) {}

  /// Construct a string ref from an std::string.
  /*implicit*/ StringRef(const string& Str)
      : data_(Str.data()), length_(Str.length()) {}

  /// Disallow accidental assignment from a temporary std::string.
  ///
  /// The declaration here is extra complicated so that `stringRef = {}`
  /// and `stringRef = "abc"` continue to select the move assignment operator.
  template <typename T>
  std::enable_if_t<std::is_same<T, std::string>::value, StringRef>& operator=(
      T&& Str) = delete;

  string str() const {
    if (!data_)
      return string();
    return string(data_, length_);
  }

  /// data - Get a pointer to the start of the string (which may not be null
  /// terminated).
  const char* data() const { return data_; }

  /// empty - Check if the string is empty.
  bool empty() const { return length_ == 0; }

  /// size - Get the string size.
  size_t size() const { return length_; }

  bool equals(StringRef RHS) const {
    return (length_ == RHS.length_ &&
            compareMemory(data_, RHS.data_, RHS.length_) == 0);
  }

  /// compare - Compare two strings; the result is -1, 0, or 1 if this string
  /// is lexicographically less than, equal to, or greater than the \p RHS.
  int compare(StringRef RHS) const {
    // Check the prefix for a mismatch.
    if (int Res =
            compareMemory(data_, RHS.data_, std::min(length_, RHS.length_)))
      return Res < 0 ? -1 : 1;

    // Otherwise the prefixes match, so we only need to check the lengths.
    if (length_ == RHS.length_)
      return 0;
    return length_ < RHS.length_ ? -1 : 1;
  }
};

inline bool operator==(StringRef LHS, StringRef RHS) {
  return LHS.equals(RHS);
}

inline bool operator!=(StringRef LHS, StringRef RHS) {
  return !(LHS == RHS);
}

inline bool operator<(StringRef LHS, StringRef RHS) {
  return LHS.compare(RHS) == -1;
}

inline bool operator<=(StringRef LHS, StringRef RHS) {
  return LHS.compare(RHS) != 1;
}

inline bool operator>(StringRef LHS, StringRef RHS) {
  return LHS.compare(RHS) == 1;
}

inline bool operator>=(StringRef LHS, StringRef RHS) {
  return LHS.compare(RHS) != -1;
}

inline string& operator+=(string& buffer, StringRef string) {
  return buffer.append(string.data(), string.size());
}

}  // namespace google_breakpad
#endif
