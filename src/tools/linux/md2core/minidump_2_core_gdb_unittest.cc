// Copyright (c) 2010 Google Inc.
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>


#include "breakpad_googletest_includes.h"
#include "client/linux/handler/exception_handler.h"
#include "common/tests/auto_tempdir.h"
#include "google_breakpad/processor/minidump.h"

using namespace google_breakpad;
using std::string;

string GetStdoutFromCommand(string cmd) {
  string data;
  FILE * stream;
  const int max_buffer = 256;
  char buffer[max_buffer];
  cmd.append(" 2>&1");

  stream = popen(cmd.c_str(), "r");
  if (stream) {
    while (!feof(stream)) {
      if (fgets(buffer, max_buffer, stream) != NULL) {
        data.append(buffer);
      }
    }
    pclose(stream);
  }
  return data;
}

// Test that a heap memory region can be inspected in gdb
TEST(Minidump2CoreGdbTest, HeapMemory) {
  // Get some heap memory.
  const uint32_t MEM_SIZE = 100;
  char* memory = new char[MEM_SIZE];
  ASSERT_TRUE(memory);

  snprintf(memory, MEM_SIZE,
           "Hello, this is a test string to see if a heap object "
           "can be indeed traced in the dump file");

  AutoTempDir temp_dir;
  ExceptionHandler handler(
      MinidumpDescriptor(temp_dir.path()), NULL, NULL, NULL, true, -1);

  // Add the memory region to the list of memory to be included.
  handler.RegisterAppMemory(memory, MEM_SIZE);

  ASSERT_TRUE(handler.WriteMinidump());

  const char *md = handler.minidump_descriptor().path();

  ASSERT_FALSE(system(string("du -h ").append(md).c_str()));

  // Convert to core
  ASSERT_FALSE(system(string("./src/tools/linux/md2core/minidump-2-core ")
    .append(md).append(" -o ").append(md).append(".core").c_str()));

  ASSERT_NE(GetStdoutFromCommand(string("gdb ")
    .append("src/tools/linux/md2core/minidump_2_core_unittest ")
    .append(md).append(".core ").append("--batch ")
    .append("-ex \"frame 1\" ")
    .append("-ex \"p memory\" "))
    .find("\"Hello, this is a test string to see if a heap object "
            "can be indeed traced in the dump file\""), string::npos);

  delete[] memory;
}