// Copyright (c) 2022 Google Inc.
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

// disassembler_objdump.h: Disassembler that invokes objdump for disassembly.
//
// Author: Mark Brand

#ifndef GOOGLE_BREAKPAD_PROCESSOR_DISASSEMBLER_OBJDUMP_H_
#define GOOGLE_BREAKPAD_PROCESSOR_DISASSEMBLER_OBJDUMP_H_

#include <string>

#include "common/using_std_string.h"
#include "google_breakpad/common/breakpad_types.h"
#include "google_breakpad/processor/dump_context.h"
#include "google_breakpad/processor/memory_region.h"

namespace google_breakpad {
class DisassemblerObjdump {
 public:
  DisassemblerObjdump(uint32_t cpu, const MemoryRegion* memory_region, uint64_t address);
  ~DisassemblerObjdump();

  bool CalculateSrcAddress(const DumpContext& context, uint64_t& address);
  bool CalculateDestAddress(const DumpContext& context, uint64_t& address);

  const string& operation() const {
    return operation_;
  }

  const string& dest() const {
    return dest_;
  }

  const string& src() const {
    return src_;
  }

 private:
  friend class DisassemblerObjdumpForTest;

  static bool DisassembleInstruction(uint32_t architecture,
                                     const uint8_t* raw_bytes,
                                     unsigned int raw_bytes_len,
                                     string& instruction);
  static bool TokenizeInstruction(const string& instruction, string& operation, string& dest, string& src);
  static bool CalculateAddress(const DumpContext& context, const string& operation, uint64_t& address);

  string operation_ = "";
  string dest_ = "";
  string src_ = "";
};
} // namespace google_breakpad

#endif  // GOOGLE_BREAKPAD_PROCESSOR_DISASSEMBLER_OBJDUMP_H_