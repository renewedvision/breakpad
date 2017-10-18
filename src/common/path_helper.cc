// Copyright 2017, Google Inc.
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

#include "common/path_helper.h"

#include <utility>

namespace google_breakpad {

using std::pair;

namespace {

// Split |path| into a pair of components, following rules described
// in dirname(3) and basename(3).
pair<string, string> SplitPath(const string& path) {
  if (path.empty())
    return pair<string, string>(".", ".");
  if (path == "/")
    return pair<string, string>("/", "/");
  if (path.size() == 1)
    return pair<string, string>(".", path);

  size_t end_pos = path.find_last_not_of('/');
  if (end_pos == string::npos)
    return pair<string,string>("/", "/");

  size_t slash_pos = path.rfind('/', end_pos);
  if (slash_pos == string::npos)
    return pair<string, string>(".", path);

  return pair<string, string>(path.substr(0, slash_pos ? slash_pos : 1),
                              path.substr(slash_pos + 1, end_pos - slash_pos));
}

}  // namespace

string BaseName(const string& path) {
  return SplitPath(path).second;
}

string DirName(const string& path) {
  return SplitPath(path).first;
}

}  // namespace google_breakpad
