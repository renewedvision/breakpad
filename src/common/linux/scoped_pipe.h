// Copyright 2022 Google LLC
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
//     * Neither the name of Google LLC nor the names of its
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

#ifndef COMMON_LINUX_SCOPED_PIPE_H_
#define COMMON_LINUX_SCOPED_PIPE_H_

#include <stdint.h>
#include <string>

#include "src/testing/googletest/include/gtest/gtest_prod.h"

namespace google_breakpad {

// Small RAII wrapper for a pipe pair.
//
// Example:
//   ScopedPipe tmp;
//   std::string line;
//   if (tmp.Init() && tmp.Write(bytes, bytes_len)) {
//     tmp.CloseWriteFd();
//     while (tmp.ReadLine(&line)) {
//       std::cerr << line << std::endl;
//     }
//   }
class ScopedPipe {
 public:
  ScopedPipe();
  ~ScopedPipe();

  // Creates the pipe pair - returns false on error.
  bool Init();

  // Close the read pipe. This only needs to be used when the read pipe needs to
  // be closed earlier.
  void CloseReadFd();

  // Close the write pipe. This only needs to be used when the write pipe needs
  // to be closed earlier.
  void CloseWriteFd();

  // Reads characters until newline or end of pipe. On read failure this will
  // close the read pipe, but continue to return true and read buffered lines
  // until the internal buffering is exhausted. This will block if there is no
  // data available on the read pipe.
  bool ReadLine(std::string& line);

  // Calls the dup2 system call to replace any existing open file descriptor
  // with number new_fd with a copy of the current write end file descriptor
  // for the pipe.
  int Dup2WriteFd(int new_fd);

 private:
  friend class ScopedPipeTest;
  FRIEND_TEST(ScopedPipeTest, WriteAndClose);
  FRIEND_TEST(ScopedPipeTest, MultipleWrites);

  int GetReadFd() const {
    return fds_[0];
  }

  int GetWriteFd() const {
    return fds_[1];
  }

  // Writes bytes to the write end of the pipe, returns false and closes write
  // pipe on failure.
  bool WriteForTesting(const void* bytes, size_t bytes_len);

  int fds_[2];
  std::string read_buffer_;
};

}  // namespace google_breakpad

#endif // COMMON_LINUX_SCOPED_PIPE_H_
