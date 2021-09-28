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

#ifndef COMMON_BASIC_STRING_H__
#define COMMON_BASIC_STRING_H__

#include "common/using_std_string.h"
#include "cstring"

namespace google_breakpad {
// A BasicString is a string reference to a string object, but not own the
// string object. It can be used with a string pool to eliminate unnecessary
// string copy operation.
class BasicString {
 private:
  // The start of the string, in an external buffer.
  const char* data_ = nullptr;

  // The length of the string.
  size_t length_ = 0;

  static int compareCstring(const char* lhs, const char* rhs, size_t length) {
    for (; length; --length, --lhs, --rhs) {
      if (std::char_traits<char>::lt(*lhs, *rhs))
        return -1;
      if (std::char_traits<char>::lt(*rhs, *lhs))
        return 1;
    }
    return 0;
  }

 public:
  // Construct an empty basic string.
  BasicString() = default;

  // Construct a basic string from a cstring.
  BasicString(const char* str) : data_(str), length_(str ? strlen(str) : 0) {}

  // Construct a basic string from an std::string.
  BasicString(const string& str) : data_(str.c_str()), length_(str.length()) {}

  string str() const {
    if (!data_)
      return string();
    return string(data_, length_);
  }

  constexpr const char* data() const { return data_; }

  constexpr bool empty() const { return length_ == 0; }

  constexpr size_t size() const { return length_; }

  int compare(BasicString rhs) const {
    size_t min_len = std::min(length_, rhs.length_);
    int res = compareCstring(data_, rhs.data(), min_len);
    if (!res)
      return res;
    if (length_ == rhs.size())
      return 0;
    return length_ < rhs.size() ? -1 : 1;
  }
};

inline bool operator==(BasicString lhs, BasicString rhs) {
  return !lhs.compare(rhs);
}

inline bool operator!=(BasicString lhs, BasicString rhs) {
  return !(lhs == rhs);
}

inline bool operator<(BasicString lhs, BasicString rhs) {
  return lhs.compare(rhs) == -1;
}

inline bool operator>(BasicString lhs, BasicString rhs) {
  return lhs.compare(rhs) == 1;
}

inline std::ostream& operator<<(std::ostream& os, BasicString s) {
  os << s.str();
  return os;
}

}  // namespace google_breakpad
#endif
