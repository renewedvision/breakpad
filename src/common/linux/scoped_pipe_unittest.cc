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

// scoped_pipe_unittest.cc: Unit tests for google_breakpad::ScopedPipe.

#include "common/linux/scoped_pipe.h"

#include "breakpad_googletest_includes.h"

using google_breakpad::ScopedPipe;

TEST(ScopedPipeTest, WriteAndClose) {
  const char* test_data = "One\nTwo\nThree";
  ScopedPipe pipe;
  ASSERT_TRUE(pipe.Init());
  ASSERT_TRUE(pipe.Write(test_data, strlen(test_data)));
  pipe.CloseWriteFd();

  std::string line;
  ASSERT_TRUE(pipe.ReadLine(line));
  ASSERT_EQ(line, "One");
  ASSERT_TRUE(pipe.ReadLine(line));
  ASSERT_EQ(line, "Two");
  ASSERT_TRUE(pipe.ReadLine(line));
  ASSERT_EQ(line, "Three");
  ASSERT_FALSE(pipe.ReadLine(line));
}

TEST(ScopedPipeTest, MultipleWrites) {
  const char* test_data_one = "One\n";
  const char* test_data_two = "Two\n";
  ScopedPipe pipe;
  std::string line;

  ASSERT_TRUE(pipe.Init());
  ASSERT_TRUE(pipe.Write(test_data_one, strlen(test_data_one)));
  ASSERT_TRUE(pipe.ReadLine(line));
  ASSERT_EQ(line, "One");

  ASSERT_TRUE(pipe.Write(test_data_two, strlen(test_data_two)));
  ASSERT_TRUE(pipe.ReadLine(line));
  ASSERT_EQ(line, "Two");
}
