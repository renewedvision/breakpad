// Copyright 2010 Google LLC
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

// stackwalk_common.cc: Module shared by the {micro,mini}dump_stackwalck
// executables to print the content of dumps (w/ stack traces) on the console.
//
// Author: Mark Mentovai

#include "processor/stackwalk_common.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <string>
#include <vector>

#include "common/stdio_wrapper.h"
#include "common/using_std_string.h"
#include "google_breakpad/processor/call_stack.h"
#include "google_breakpad/processor/code_module.h"
#include "google_breakpad/processor/code_modules.h"
#include "google_breakpad/processor/process_state.h"
#include "google_breakpad/processor/source_line_resolver_interface.h"
#include "google_breakpad/processor/stack_frame_cpu.h"
#include "processor/logging.h"
#include "processor/pathname_stripper.h"

namespace google_breakpad {

namespace {

using std::vector;
using std::unique_ptr;

// Separator character for machine readable output.
static const char kOutputSeparator = '|';

// PrintRegister prints a register's name and value to stdout.  It will print
// registers without linebreaks until the output passes |max_col| characters..
// For the first register in a set, pass 0 for |start_col|.  For registers in a
// set, pass the most recent return value of PrintRegister.
// The caller is responsible for printing the final newline after a set
// of registers is completely printed, regardless of the number of calls
// to PrintRegister.
static const int kTerminalWidth = 80;  // optimize for an 80-column terminal
static int PrintRegister(const char* name,
                         uint32_t value,
                         int start_col,
                         int max_col) {
  char buffer[64];
  snprintf(buffer, sizeof(buffer), " %6s: 0x%08x", name, value);

  if (start_col + static_cast<ssize_t>(strlen(buffer)) > max_col) {
    start_col = 0;
    printf("\n ");
  }
  fputs(buffer, stdout);

  return start_col + strlen(buffer);
}

// PrintRegister64 does the same thing, but for 64-bit registers.
static int PrintRegister64(const char* name,
                           uint64_t value,
                           int start_col,
                           int max_col) {
  char buffer[64];
  snprintf(buffer, sizeof(buffer), " %6s: 0x%016" PRIx64, name, value);

  if (start_col + static_cast<ssize_t>(strlen(buffer)) > max_col) {
    start_col = 0;
    printf("\n ");
  }
  fputs(buffer, stdout);

  return start_col + strlen(buffer);
}

// Structs used to simplify printing register data.
typedef struct {
  const char* name;
  const uint32_t* value;
  const int32_t validity_mask;
} Register32;

typedef struct {
  const char* name;
  const uint64_t* value;
  const uint64_t validity_mask;
} Register64;

// StripSeparator takes a string |original| and returns a copy
// of the string with all occurences of |kOutputSeparator| removed.
static string StripSeparator(const string& original) {
  string result = original;
  string::size_type position = 0;
  while ((position = result.find(kOutputSeparator, position)) != string::npos) {
    result.erase(position, 1);
  }
  position = 0;
  while ((position = result.find('\n', position)) != string::npos) {
    result.erase(position, 1);
  }
  return result;
}

// PrintStackContents prints the stack contents of the current frame to stdout.
static void PrintStackContents(const string& indent,
                               const StackFrame* frame,
                               const StackFrame* prev_frame,
                               const string& cpu,
                               const MemoryRegion* memory,
                               const CodeModules* modules,
                               SourceLineResolverInterface* resolver) {
  // Find stack range.
  int word_length = 0;
  uint64_t stack_begin = 0, stack_end = 0;
  if (cpu == "x86") {
    word_length = 4;
    const StackFrameX86* frame_x86 = static_cast<const StackFrameX86*>(frame);
    const StackFrameX86* prev_frame_x86 =
        static_cast<const StackFrameX86*>(prev_frame);
    if ((frame_x86->context_validity & StackFrameX86::CONTEXT_VALID_ESP) &&
        (prev_frame_x86->context_validity & StackFrameX86::CONTEXT_VALID_ESP)) {
      stack_begin = frame_x86->context.esp;
      stack_end = prev_frame_x86->context.esp;
    }
  } else if (cpu == "amd64") {
    word_length = 8;
    const StackFrameAMD64* frame_amd64 =
        static_cast<const StackFrameAMD64*>(frame);
    const StackFrameAMD64* prev_frame_amd64 =
        static_cast<const StackFrameAMD64*>(prev_frame);
    if ((frame_amd64->context_validity & StackFrameAMD64::CONTEXT_VALID_RSP) &&
        (prev_frame_amd64->context_validity &
         StackFrameAMD64::CONTEXT_VALID_RSP)) {
      stack_begin = frame_amd64->context.rsp;
      stack_end = prev_frame_amd64->context.rsp;
    }
  } else if (cpu == "arm") {
    word_length = 4;
    const StackFrameARM* frame_arm = static_cast<const StackFrameARM*>(frame);
    const StackFrameARM* prev_frame_arm =
        static_cast<const StackFrameARM*>(prev_frame);
    if ((frame_arm->context_validity &
         StackFrameARM::CONTEXT_VALID_SP) &&
        (prev_frame_arm->context_validity & StackFrameARM::CONTEXT_VALID_SP)) {
      stack_begin = frame_arm->context.iregs[13];
      stack_end = prev_frame_arm->context.iregs[13];
    }
  } else if (cpu == "arm64") {
    word_length = 8;
    const StackFrameARM64* frame_arm64 =
        static_cast<const StackFrameARM64*>(frame);
    const StackFrameARM64* prev_frame_arm64 =
        static_cast<const StackFrameARM64*>(prev_frame);
    if ((frame_arm64->context_validity &
         StackFrameARM64::CONTEXT_VALID_SP) &&
        (prev_frame_arm64->context_validity &
         StackFrameARM64::CONTEXT_VALID_SP)) {
      stack_begin = frame_arm64->context.iregs[31];
      stack_end = prev_frame_arm64->context.iregs[31];
    }
  } else if (cpu == "riscv") {
    word_length = 4;
    const StackFrameRISCV* frame_riscv =
        static_cast<const StackFrameRISCV*>(frame);
    const StackFrameRISCV* prev_frame_riscv =
        static_cast<const StackFrameRISCV*>(prev_frame);
    if ((frame_riscv->context_validity &
         StackFrameRISCV::CONTEXT_VALID_SP) &&
        (prev_frame_riscv->context_validity &
         StackFrameRISCV::CONTEXT_VALID_SP)) {
      stack_begin = frame_riscv->context.sp;
      stack_end = prev_frame_riscv->context.sp;
    }
  } else if (cpu == "riscv64") {
    word_length = 8;
    const StackFrameRISCV64* frame_riscv64 =
        static_cast<const StackFrameRISCV64*>(frame);
    const StackFrameRISCV64* prev_frame_riscv64 =
        static_cast<const StackFrameRISCV64*>(prev_frame);
    if ((frame_riscv64->context_validity &
         StackFrameRISCV64::CONTEXT_VALID_SP) &&
        (prev_frame_riscv64->context_validity &
         StackFrameRISCV64::CONTEXT_VALID_SP)) {
      stack_begin = frame_riscv64->context.sp;
      stack_end = prev_frame_riscv64->context.sp;
    }
  }
  if (!word_length || !stack_begin || !stack_end)
    return;

  // Print stack contents.
  printf("\n%sStack contents:", indent.c_str());
  for(uint64_t address = stack_begin; address < stack_end; ) {
    // Print the start address of this row.
    if (word_length == 4)
      printf("\n%s %08x", indent.c_str(), static_cast<uint32_t>(address));
    else
      printf("\n%s %016" PRIx64, indent.c_str(), address);

    // Print data in hex.
    const int kBytesPerRow = 16;
    string data_as_string;
    for (int i = 0; i < kBytesPerRow; ++i, ++address) {
      uint8_t value = 0;
      if (address < stack_end &&
          memory->GetMemoryAtAddress(address, &value)) {
        printf(" %02x", value);
        data_as_string.push_back(isprint(value) ? value : '.');
      } else {
        printf("   ");
        data_as_string.push_back(' ');
      }
    }
    // Print data as string.
    printf("  %s", data_as_string.c_str());
  }

  // Try to find instruction pointers from stack.
  printf("\n%sPossible instruction pointers:\n", indent.c_str());
  for (uint64_t address = stack_begin; address < stack_end;
       address += word_length) {
    StackFrame pointee_frame;

    // Read a word (possible instruction pointer) from stack.
    if (word_length == 4) {
      uint32_t data32 = 0;
      memory->GetMemoryAtAddress(address, &data32);
      pointee_frame.instruction = data32;
    } else {
      uint64_t data64 = 0;
      memory->GetMemoryAtAddress(address, &data64);
      pointee_frame.instruction = data64;
    }
    pointee_frame.module =
        modules->GetModuleForAddress(pointee_frame.instruction);

    // Try to look up the function name.
    std::deque<unique_ptr<StackFrame>> inlined_frames;
    if (pointee_frame.module)
      resolver->FillSourceLineInfo(&pointee_frame, &inlined_frames);

    // Print function name.
    auto print_function_name = [&](StackFrame* frame) {
      if (!frame->function_name.empty()) {
        if (word_length == 4) {
          printf("%s *(0x%08x) = 0x%08x", indent.c_str(),
                 static_cast<uint32_t>(address),
                 static_cast<uint32_t>(frame->instruction));
        } else {
          printf("%s *(0x%016" PRIx64 ") = 0x%016" PRIx64, indent.c_str(),
                 address, frame->instruction);
        }
        printf(
            " <%s> [%s : %d + 0x%" PRIx64 "]\n", frame->function_name.c_str(),
            PathnameStripper::File(frame->source_file_name).c_str(),
            frame->source_line, frame->instruction - frame->source_line_base);
      }
    };
    print_function_name(&pointee_frame);
    for (unique_ptr<StackFrame> &frame : inlined_frames)
      print_function_name(frame.get());
  }
  printf("\n");
}

// PrintStack prints the call stack in |stack| to stdout, in a reasonably
// useful form.  Module, function, and source file names are displayed if
// they are available.  The code offset to the base code address of the
// source line, function, or module is printed, preferring them in that
// order.  If no source line, function, or module information is available,
// an absolute code offset is printed.
//
// If |cpu| is a recognized CPU name, relevant register state for each stack
// frame printed is also output, if available.
static void PrintStack(const CallStack* stack,
                       const string& cpu,
                       bool output_stack_contents,
                       const MemoryRegion* memory,
                       const CodeModules* modules,
                       SourceLineResolverInterface* resolver) {
  int frame_count = stack->frames()->size();
  if (frame_count == 0) {
    printf(" <no frames>\n");
  }
  for (int frame_index = 0; frame_index < frame_count; ++frame_index) {
    const StackFrame* frame = stack->frames()->at(frame_index);
    printf("%2d  ", frame_index);

    uint64_t instruction_address = frame->ReturnAddress();

    if (frame->module) {
      printf("%s", PathnameStripper::File(frame->module->code_file()).c_str());
      if (!frame->function_name.empty()) {
        printf("!%s", frame->function_name.c_str());
        if (!frame->source_file_name.empty()) {
          string source_file = PathnameStripper::File(frame->source_file_name);
          printf(" [%s : %d + 0x%" PRIx64 "]",
                 source_file.c_str(),
                 frame->source_line,
                 instruction_address - frame->source_line_base);
        } else {
          printf(" + 0x%" PRIx64, instruction_address - frame->function_base);
        }
      } else {
        printf(" + 0x%" PRIx64,
               instruction_address - frame->module->base_address());
      }
    } else {
      printf("0x%" PRIx64, instruction_address);
    }
    printf("\n ");

    // Inlined frames don't have registers info.
    if (frame->trust != StackFrameAMD64::FRAME_TRUST_INLINE) {
      int sequence = 0;
      if (cpu == "x86") {
        const StackFrameX86* frame_x86 =
            reinterpret_cast<const StackFrameX86*>(frame);

        Register32 data[] = {
            {"eip", &(frame_x86->context.eip),
             StackFrameX86::CONTEXT_VALID_EIP},
            {"esp", &(frame_x86->context.esp),
             StackFrameX86::CONTEXT_VALID_ESP},
            {"ebp", &(frame_x86->context.ebp),
             StackFrameX86::CONTEXT_VALID_EBP},
            {"ebx", &(frame_x86->context.ebx),
             StackFrameX86::CONTEXT_VALID_EBX},
            {"esi", &(frame_x86->context.esi),
             StackFrameX86::CONTEXT_VALID_ESI},
            {"edi", &(frame_x86->context.edi),
             StackFrameX86::CONTEXT_VALID_EDI},
            {"eax", &(frame_x86->context.eax),
             StackFrameX86::CONTEXT_VALID_ALL},
            {"ecx", &(frame_x86->context.ecx),
             StackFrameX86::CONTEXT_VALID_ALL},
            {"edx", &(frame_x86->context.edx),
             StackFrameX86::CONTEXT_VALID_ALL},
            {"efl", &(frame_x86->context.eflags),
             StackFrameX86::CONTEXT_VALID_ALL},
        };
        for (int i = 0; i < 10; ++i) {
          if (frame_x86->context_validity & data[i].validity_mask) {
            sequence = PrintRegister(data[i].name, *(data[i].value), sequence,
                                     kTerminalWidth);
          }
        }
      } else if (cpu == "ppc") {
        const StackFramePPC* frame_ppc =
            reinterpret_cast<const StackFramePPC*>(frame);

        if (frame_ppc->context_validity & StackFramePPC::CONTEXT_VALID_SRR0) {
          sequence = PrintRegister("srr0", frame_ppc->context.srr0, sequence,
                                   kTerminalWidth);
        }
        if (frame_ppc->context_validity & StackFramePPC::CONTEXT_VALID_GPR1) {
          sequence = PrintRegister("r1", frame_ppc->context.gpr[1], sequence,
                                   kTerminalWidth);
        }
      } else if (cpu == "amd64") {
        const StackFrameAMD64* frame_amd64 =
            reinterpret_cast<const StackFrameAMD64*>(frame);

        Register64 data[] = {
            {"rax", &(frame_amd64->context.rax),
             StackFrameAMD64::CONTEXT_VALID_RAX},
            {"rdx", &(frame_amd64->context.rdx),
             StackFrameAMD64::CONTEXT_VALID_RDX},
            {"rcx", &(frame_amd64->context.rcx),
             StackFrameAMD64::CONTEXT_VALID_RCX},
            {"rbx", &(frame_amd64->context.rbx),
             StackFrameAMD64::CONTEXT_VALID_RBX},
            {"rsi", &(frame_amd64->context.rsi),
             StackFrameAMD64::CONTEXT_VALID_RSI},
            {"rdi", &(frame_amd64->context.rdi),
             StackFrameAMD64::CONTEXT_VALID_RDI},
            {"rbp", &(frame_amd64->context.rbp),
             StackFrameAMD64::CONTEXT_VALID_RBP},
            {"rsp", &(frame_amd64->context.rsp),
             StackFrameAMD64::CONTEXT_VALID_RSP},
            {"r8", &(frame_amd64->context.r8),
             StackFrameAMD64::CONTEXT_VALID_R8},
            {"r9", &(frame_amd64->context.r9),
             StackFrameAMD64::CONTEXT_VALID_R9},
            {"r10", &(frame_amd64->context.r10),
             StackFrameAMD64::CONTEXT_VALID_R10},
            {"r11", &(frame_amd64->context.r11),
             StackFrameAMD64::CONTEXT_VALID_R11},
            {"r12", &(frame_amd64->context.r12),
             StackFrameAMD64::CONTEXT_VALID_R12},
            {"r13", &(frame_amd64->context.r13),
             StackFrameAMD64::CONTEXT_VALID_R13},
            {"r14", &(frame_amd64->context.r14),
             StackFrameAMD64::CONTEXT_VALID_R14},
            {"r15", &(frame_amd64->context.r15),
             StackFrameAMD64::CONTEXT_VALID_R15},
            {"rip", &(frame_amd64->context.rip),
             StackFrameAMD64::CONTEXT_VALID_RIP},
        };
        for (int i = 0; i < 17; ++i) {
          if (frame_amd64->context_validity & data[i].validity_mask) {
            sequence = PrintRegister64(data[i].name, *(data[i].value), sequence,
                                       kTerminalWidth);
          }
        }
      } else if (cpu == "sparc") {
        const StackFrameSPARC* frame_sparc =
            reinterpret_cast<const StackFrameSPARC*>(frame);

        if (frame_sparc->context_validity & StackFrameSPARC::CONTEXT_VALID_SP) {
          sequence = PrintRegister("sp", frame_sparc->context.g_r[14], sequence,
                                   kTerminalWidth);
        }
        if (frame_sparc->context_validity & StackFrameSPARC::CONTEXT_VALID_FP) {
          sequence = PrintRegister("fp", frame_sparc->context.g_r[30], sequence,
                                   kTerminalWidth);
        }
        if (frame_sparc->context_validity & StackFrameSPARC::CONTEXT_VALID_PC) {
          sequence = PrintRegister("pc", frame_sparc->context.pc, sequence,
                                   kTerminalWidth);
        }

      } else if (cpu == "arm") {
        const StackFrameARM* frame_arm =
            reinterpret_cast<const StackFrameARM*>(frame);

        Register32 data[] = {
            {"r0", &(frame_arm->context.iregs[0]),
             StackFrameARM::CONTEXT_VALID_R0},
            {"r1", &(frame_arm->context.iregs[1]),
             StackFrameARM::CONTEXT_VALID_R1},
            {"r2", &(frame_arm->context.iregs[2]),
             StackFrameARM::CONTEXT_VALID_R2},
            {"r3", &(frame_arm->context.iregs[3]),
             StackFrameARM::CONTEXT_VALID_R3},
            {"r4", &(frame_arm->context.iregs[4]),
             StackFrameARM::CONTEXT_VALID_R4},
            {"r5", &(frame_arm->context.iregs[5]),
             StackFrameARM::CONTEXT_VALID_R5},
            {"r6", &(frame_arm->context.iregs[6]),
             StackFrameARM::CONTEXT_VALID_R6},
            {"r7", &(frame_arm->context.iregs[7]),
             StackFrameARM::CONTEXT_VALID_R7},
            {"r8", &(frame_arm->context.iregs[8]),
             StackFrameARM::CONTEXT_VALID_R8},
            {"r9", &(frame_arm->context.iregs[9]),
             StackFrameARM::CONTEXT_VALID_R9},
            {"r10", &(frame_arm->context.iregs[10]),
             StackFrameARM::CONTEXT_VALID_R10},
            {"r12", &(frame_arm->context.iregs[12]),
             StackFrameARM::CONTEXT_VALID_R12},
            {"fp", &(frame_arm->context.iregs[11]),
             StackFrameARM::CONTEXT_VALID_FP},
            {"sp", &(frame_arm->context.iregs[13]),
             StackFrameARM::CONTEXT_VALID_SP},
            {"lr", &(frame_arm->context.iregs[14]),
             StackFrameARM::CONTEXT_VALID_LR},
            {"pc", &(frame_arm->context.iregs[15]),
             StackFrameARM::CONTEXT_VALID_PC},
        };
        for (int i = 0; i < 16; ++i) {
          if (frame_arm->context_validity & data[i].validity_mask) {
            sequence = PrintRegister(data[i].name, *(data[i].value), sequence,
                                     kTerminalWidth);
          }
        }

      } else if (cpu == "arm64") {
        const StackFrameARM64* frame_arm64 =
            reinterpret_cast<const StackFrameARM64*>(frame);

        Register64 data[] = {
            {"x0", &(frame_arm64->context.iregs[0]),
             StackFrameARM64::CONTEXT_VALID_X0},
            {"x1", &(frame_arm64->context.iregs[1]),
             StackFrameARM64::CONTEXT_VALID_X1},
            {"x2", &(frame_arm64->context.iregs[2]),
             StackFrameARM64::CONTEXT_VALID_X2},
            {"x3", &(frame_arm64->context.iregs[3]),
             StackFrameARM64::CONTEXT_VALID_X3},
            {"x4", &(frame_arm64->context.iregs[4]),
             StackFrameARM64::CONTEXT_VALID_X4},
            {"x5", &(frame_arm64->context.iregs[5]),
             StackFrameARM64::CONTEXT_VALID_X5},
            {"x6", &(frame_arm64->context.iregs[6]),
             StackFrameARM64::CONTEXT_VALID_X6},
            {"x7", &(frame_arm64->context.iregs[7]),
             StackFrameARM64::CONTEXT_VALID_X7},
            {"x8", &(frame_arm64->context.iregs[8]),
             StackFrameARM64::CONTEXT_VALID_X8},
            {"x9", &(frame_arm64->context.iregs[9]),
             StackFrameARM64::CONTEXT_VALID_X9},
            {"x10", &(frame_arm64->context.iregs[10]),
             StackFrameARM64::CONTEXT_VALID_X10},
            {"x11", &(frame_arm64->context.iregs[11]),
             StackFrameARM64::CONTEXT_VALID_X11},
            {"x12", &(frame_arm64->context.iregs[12]),
             StackFrameARM64::CONTEXT_VALID_X12},
            {"x13", &(frame_arm64->context.iregs[13]),
             StackFrameARM64::CONTEXT_VALID_X13},
            {"x14", &(frame_arm64->context.iregs[14]),
             StackFrameARM64::CONTEXT_VALID_X14},
            {"x15", &(frame_arm64->context.iregs[15]),
             StackFrameARM64::CONTEXT_VALID_X15},
            {"x16", &(frame_arm64->context.iregs[16]),
             StackFrameARM64::CONTEXT_VALID_X16},
            {"x17", &(frame_arm64->context.iregs[17]),
             StackFrameARM64::CONTEXT_VALID_X17},
            {"x18", &(frame_arm64->context.iregs[18]),
             StackFrameARM64::CONTEXT_VALID_X18},
            {"x19", &(frame_arm64->context.iregs[19]),
             StackFrameARM64::CONTEXT_VALID_X19},
            {"x20", &(frame_arm64->context.iregs[20]),
             StackFrameARM64::CONTEXT_VALID_X20},
            {"x21", &(frame_arm64->context.iregs[21]),
             StackFrameARM64::CONTEXT_VALID_X21},
            {"x22", &(frame_arm64->context.iregs[22]),
             StackFrameARM64::CONTEXT_VALID_X22},
            {"x23", &(frame_arm64->context.iregs[23]),
             StackFrameARM64::CONTEXT_VALID_X23},
            {"x24", &(frame_arm64->context.iregs[24]),
             StackFrameARM64::CONTEXT_VALID_X24},
            {"x25", &(frame_arm64->context.iregs[25]),
             StackFrameARM64::CONTEXT_VALID_X25},
            {"x26", &(frame_arm64->context.iregs[26]),
             StackFrameARM64::CONTEXT_VALID_X26},
            {"x27", &(frame_arm64->context.iregs[27]),
             StackFrameARM64::CONTEXT_VALID_X27},
            {"x28", &(frame_arm64->context.iregs[28]),
             StackFrameARM64::CONTEXT_VALID_X28},
            // Registers with a dedicated or conventional purpose.
            {"fp", &(frame_arm64->context.iregs[29]),
             StackFrameARM64::CONTEXT_VALID_FP},
            {"lr", &(frame_arm64->context.iregs[30]),
             StackFrameARM64::CONTEXT_VALID_LR},
            {"sp", &(frame_arm64->context.iregs[31]),
             StackFrameARM64::CONTEXT_VALID_SP},
            {"pc", &(frame_arm64->context.iregs[32]),
             StackFrameARM64::CONTEXT_VALID_PC},
        };
        for (int i = 0; i < 33; ++i) {
          if (frame_arm64->context_validity & data[i].validity_mask) {
            sequence = PrintRegister64(data[i].name, *(data[i].value), sequence,
                                       kTerminalWidth);
          }
        }

      } else if ((cpu == "mips") || (cpu == "mips64")) {
        const StackFrameMIPS* frame_mips =
            reinterpret_cast<const StackFrameMIPS*>(frame);

        if (frame_mips->context_validity & StackFrameMIPS::CONTEXT_VALID_GP)
          sequence = PrintRegister64(
              "gp", frame_mips->context.iregs[MD_CONTEXT_MIPS_REG_GP], sequence,
              kTerminalWidth);
        if (frame_mips->context_validity & StackFrameMIPS::CONTEXT_VALID_SP)
          sequence = PrintRegister64(
              "sp", frame_mips->context.iregs[MD_CONTEXT_MIPS_REG_SP], sequence,
              kTerminalWidth);
        if (frame_mips->context_validity & StackFrameMIPS::CONTEXT_VALID_FP)
          sequence = PrintRegister64(
              "fp", frame_mips->context.iregs[MD_CONTEXT_MIPS_REG_FP], sequence,
              kTerminalWidth);
        if (frame_mips->context_validity & StackFrameMIPS::CONTEXT_VALID_RA)
          sequence = PrintRegister64(
              "ra", frame_mips->context.iregs[MD_CONTEXT_MIPS_REG_RA], sequence,
              kTerminalWidth);
        if (frame_mips->context_validity & StackFrameMIPS::CONTEXT_VALID_PC)
          sequence = PrintRegister64("pc", frame_mips->context.epc, sequence,
                                     kTerminalWidth);

        // Save registers s0-s7
        if (frame_mips->context_validity & StackFrameMIPS::CONTEXT_VALID_S0)
          sequence = PrintRegister64(
              "s0", frame_mips->context.iregs[MD_CONTEXT_MIPS_REG_S0], sequence,
              kTerminalWidth);
        if (frame_mips->context_validity & StackFrameMIPS::CONTEXT_VALID_S1)
          sequence = PrintRegister64(
              "s1", frame_mips->context.iregs[MD_CONTEXT_MIPS_REG_S1], sequence,
              kTerminalWidth);
        if (frame_mips->context_validity & StackFrameMIPS::CONTEXT_VALID_S2)
          sequence = PrintRegister64(
              "s2", frame_mips->context.iregs[MD_CONTEXT_MIPS_REG_S2], sequence,
              kTerminalWidth);
        if (frame_mips->context_validity & StackFrameMIPS::CONTEXT_VALID_S3)
          sequence = PrintRegister64(
              "s3", frame_mips->context.iregs[MD_CONTEXT_MIPS_REG_S3], sequence,
              kTerminalWidth);
        if (frame_mips->context_validity & StackFrameMIPS::CONTEXT_VALID_S4)
          sequence = PrintRegister64(
              "s4", frame_mips->context.iregs[MD_CONTEXT_MIPS_REG_S4], sequence,
              kTerminalWidth);
        if (frame_mips->context_validity & StackFrameMIPS::CONTEXT_VALID_S5)
          sequence = PrintRegister64(
              "s5", frame_mips->context.iregs[MD_CONTEXT_MIPS_REG_S5], sequence,
              kTerminalWidth);
        if (frame_mips->context_validity & StackFrameMIPS::CONTEXT_VALID_S6)
          sequence = PrintRegister64(
              "s6", frame_mips->context.iregs[MD_CONTEXT_MIPS_REG_S6], sequence,
              kTerminalWidth);
        if (frame_mips->context_validity & StackFrameMIPS::CONTEXT_VALID_S7)
          sequence = PrintRegister64(
              "s7", frame_mips->context.iregs[MD_CONTEXT_MIPS_REG_S7], sequence,
              kTerminalWidth);
      } else if (cpu == "riscv") {
        const StackFrameRISCV* frame_riscv =
            reinterpret_cast<const StackFrameRISCV*>(frame);

        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_PC)
          sequence = PrintRegister("pc", frame_riscv->context.pc, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_RA)
          sequence = PrintRegister("ra", frame_riscv->context.ra, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_SP)
          sequence = PrintRegister("sp", frame_riscv->context.sp, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_GP)
          sequence = PrintRegister("gp", frame_riscv->context.gp, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_TP)
          sequence = PrintRegister("tp", frame_riscv->context.tp, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_T0)
          sequence = PrintRegister("t0", frame_riscv->context.t0, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_T1)
          sequence = PrintRegister("t1", frame_riscv->context.t1, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_T2)
          sequence = PrintRegister("t2", frame_riscv->context.t2, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_S0)
          sequence = PrintRegister("s0", frame_riscv->context.s0, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_S1)
          sequence = PrintRegister("s1", frame_riscv->context.s1, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_A0)
          sequence = PrintRegister("a0", frame_riscv->context.a0, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_A1)
          sequence = PrintRegister("a1", frame_riscv->context.a1, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_A2)
          sequence = PrintRegister("a2", frame_riscv->context.a2, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_A3)
          sequence = PrintRegister("a3", frame_riscv->context.a3, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_A4)
          sequence = PrintRegister("a4", frame_riscv->context.a4, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_A5)
          sequence = PrintRegister("a5", frame_riscv->context.a5, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_A6)
          sequence = PrintRegister("a6", frame_riscv->context.a6, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_A7)
          sequence = PrintRegister("a7", frame_riscv->context.a7, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_S2)
          sequence = PrintRegister("s2", frame_riscv->context.s2, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_S3)
          sequence = PrintRegister("s3", frame_riscv->context.s3, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_S4)
          sequence = PrintRegister("s4", frame_riscv->context.s4, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_S5)
          sequence = PrintRegister("s5", frame_riscv->context.s5, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_S6)
          sequence = PrintRegister("s6", frame_riscv->context.s6, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_S7)
          sequence = PrintRegister("s7", frame_riscv->context.s7, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_S8)
          sequence = PrintRegister("s8", frame_riscv->context.s8, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_S9)
          sequence = PrintRegister("s9", frame_riscv->context.s9, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_S10)
          sequence = PrintRegister("s10", frame_riscv->context.s10, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_S11)
          sequence = PrintRegister("s11", frame_riscv->context.s11, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_T3)
          sequence = PrintRegister("t3", frame_riscv->context.t3, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_T4)
          sequence = PrintRegister("t4", frame_riscv->context.t4, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_T5)
          sequence = PrintRegister("t5", frame_riscv->context.t5, sequence,
                                   kTerminalWidth);
        if (frame_riscv->context_validity &
            StackFrameRISCV::CONTEXT_VALID_T6)
          sequence = PrintRegister("t6", frame_riscv->context.t6, sequence,
                                   kTerminalWidth);
      } else if (cpu == "riscv64") {
        const StackFrameRISCV64* frame_riscv64 =
            reinterpret_cast<const StackFrameRISCV64*>(frame);

        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_PC)
          sequence = PrintRegister64("pc", frame_riscv64->context.pc, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_RA)
          sequence = PrintRegister64("ra", frame_riscv64->context.ra, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_SP)
          sequence = PrintRegister64("sp", frame_riscv64->context.sp, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_GP)
          sequence = PrintRegister64("gp", frame_riscv64->context.gp, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_TP)
          sequence = PrintRegister64("tp", frame_riscv64->context.tp, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_T0)
          sequence = PrintRegister64("t0", frame_riscv64->context.t0, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_T1)
          sequence = PrintRegister64("t1", frame_riscv64->context.t1, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_T2)
          sequence = PrintRegister64("t2", frame_riscv64->context.t2, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_S0)
          sequence = PrintRegister64("s0", frame_riscv64->context.s0, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_S1)
          sequence = PrintRegister64("s1", frame_riscv64->context.s1, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_A0)
          sequence = PrintRegister64("a0", frame_riscv64->context.a0, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_A1)
          sequence = PrintRegister64("a1", frame_riscv64->context.a1, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_A2)
          sequence = PrintRegister64("a2", frame_riscv64->context.a2, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_A3)
          sequence = PrintRegister64("a3", frame_riscv64->context.a3, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_A4)
          sequence = PrintRegister64("a4", frame_riscv64->context.a4, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_A5)
          sequence = PrintRegister64("a5", frame_riscv64->context.a5, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_A6)
          sequence = PrintRegister64("a6", frame_riscv64->context.a6, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_A7)
          sequence = PrintRegister64("a7", frame_riscv64->context.a7, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_S2)
          sequence = PrintRegister64("s2", frame_riscv64->context.s2, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_S3)
          sequence = PrintRegister64("s3", frame_riscv64->context.s3, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_S4)
          sequence = PrintRegister64("s4", frame_riscv64->context.s4, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_S5)
          sequence = PrintRegister64("s5", frame_riscv64->context.s5, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_S6)
          sequence = PrintRegister64("s6", frame_riscv64->context.s6, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_S7)
          sequence = PrintRegister64("s7", frame_riscv64->context.s7, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_S8)
          sequence = PrintRegister64("s8", frame_riscv64->context.s8, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_S9)
          sequence = PrintRegister64("s9", frame_riscv64->context.s9, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_S10)
          sequence = PrintRegister64("s10", frame_riscv64->context.s10,
                                     sequence, kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_S11)
          sequence = PrintRegister64("s11", frame_riscv64->context.s11,
                                     sequence, kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_T3)
          sequence = PrintRegister64("t3", frame_riscv64->context.t3, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_T4)
          sequence = PrintRegister64("t4", frame_riscv64->context.t4, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_T5)
          sequence = PrintRegister64("t5", frame_riscv64->context.t5, sequence,
                                     kTerminalWidth);
        if (frame_riscv64->context_validity &
            StackFrameRISCV64::CONTEXT_VALID_T6)
          sequence = PrintRegister64("t6", frame_riscv64->context.t6, sequence,
                                     kTerminalWidth);
      }
    }
    printf("\n    Found by: %s\n", frame->trust_description().c_str());

    // Print stack contents.
    if (output_stack_contents && frame_index + 1 < frame_count) {
      const string indent("    ");
      PrintStackContents(indent, frame, stack->frames()->at(frame_index + 1),
                         cpu, memory, modules, resolver);
    }
  }
}

// PrintStackMachineReadable prints the call stack in |stack| to stdout,
// in the following machine readable pipe-delimited text format:
// thread number|frame number|module|function|source file|line|offset
//
// Module, function, source file, and source line may all be empty
// depending on availability.  The code offset follows the same rules as
// PrintStack above.
static void PrintStackMachineReadable(int thread_num, const CallStack* stack) {
  int frame_count = stack->frames()->size();
  for (int frame_index = 0; frame_index < frame_count; ++frame_index) {
    const StackFrame* frame = stack->frames()->at(frame_index);
    printf("%d%c%d%c", thread_num, kOutputSeparator, frame_index,
           kOutputSeparator);

    uint64_t instruction_address = frame->ReturnAddress();

    if (frame->module) {
      assert(!frame->module->code_file().empty());
      printf("%s", StripSeparator(PathnameStripper::File(
                     frame->module->code_file())).c_str());
      if (!frame->function_name.empty()) {
        printf("%c%s", kOutputSeparator,
               StripSeparator(frame->function_name).c_str());
        if (!frame->source_file_name.empty()) {
          printf("%c%s%c%d%c0x%" PRIx64,
                 kOutputSeparator,
                 StripSeparator(frame->source_file_name).c_str(),
                 kOutputSeparator,
                 frame->source_line,
                 kOutputSeparator,
                 instruction_address - frame->source_line_base);
        } else {
          printf("%c%c%c0x%" PRIx64,
                 kOutputSeparator,  // empty source file
                 kOutputSeparator,  // empty source line
                 kOutputSeparator,
                 instruction_address - frame->function_base);
        }
      } else {
        printf("%c%c%c%c0x%" PRIx64,
               kOutputSeparator,  // empty function name
               kOutputSeparator,  // empty source file
               kOutputSeparator,  // empty source line
               kOutputSeparator,
               instruction_address - frame->module->base_address());
      }
    } else {
      // the printf before this prints a trailing separator for module name
      printf("%c%c%c%c0x%" PRIx64,
             kOutputSeparator,  // empty function name
             kOutputSeparator,  // empty source file
             kOutputSeparator,  // empty source line
             kOutputSeparator,
             instruction_address);
    }
    printf("\n");
  }
}

// Prints the callstack in |stack| to stdout using the Apple Crash Report
// format.
static void PrintStackAppleCrashReport(int thread_num, const CallStack* stack) {
  int frame_count = stack->frames()->size();
  for (int frame_index = 0; frame_index < frame_count; ++frame_index) {
    const StackFrame* frame = stack->frames()->at(frame_index);
    printf("%-4d", frame_index);

    uint64_t instruction_address = frame->ReturnAddress();
    if (frame->module) {
      assert(!frame->module->code_file().empty());
      printf("%-31s",
             StripSeparator(PathnameStripper::File(frame->module->code_file()))
                 .c_str());

      printf("0x%016" PRIx64, instruction_address);
      printf(" 0x%09" PRIx64, frame->module->base_address());
      printf(" + %llu", instruction_address - frame->module->base_address());
    }
    printf("\n");
  }
}

// Prints the "Thread State" section of an Apple Crash Report.
void PrintThreadStateAppleCrashReport(const ProcessState& process_state) {
  string cpu = process_state.system_info()->cpu;
  printf("Thread %d crashed with %s Thread State:\n",
         process_state.requesting_thread(), cpu == "arm64" ? "ARM-64" : "ARM");

  const CallStack* stack =
      process_state.threads()->at(process_state.requesting_thread());
  int frame_count = stack->frames()->size();
  if (frame_count == 0) {
    printf(" <no frames>\n");
  }
  const StackFrame* frame = stack->frames()->at(0);
  int sequence = 0;

  if (cpu == "arm") {
    const StackFrameARM* frame_arm =
        reinterpret_cast<const StackFrameARM*>(frame);

    Register32 data[] = {
        {"r0", &(frame_arm->context.iregs[0]), StackFrameARM::CONTEXT_VALID_R0},
        {"r1", &(frame_arm->context.iregs[1]), StackFrameARM::CONTEXT_VALID_R1},
        {"r2", &(frame_arm->context.iregs[2]), StackFrameARM::CONTEXT_VALID_R2},
        {"r3", &(frame_arm->context.iregs[3]), StackFrameARM::CONTEXT_VALID_R3},
        {"r4", &(frame_arm->context.iregs[4]), StackFrameARM::CONTEXT_VALID_R4},
        {"r5", &(frame_arm->context.iregs[5]), StackFrameARM::CONTEXT_VALID_R5},
        {"r6", &(frame_arm->context.iregs[6]), StackFrameARM::CONTEXT_VALID_R6},
        {"r7", &(frame_arm->context.iregs[7]), StackFrameARM::CONTEXT_VALID_R7},
        {"r8", &(frame_arm->context.iregs[8]), StackFrameARM::CONTEXT_VALID_R8},
        {"r9", &(frame_arm->context.iregs[9]), StackFrameARM::CONTEXT_VALID_R9},
        {"r10", &(frame_arm->context.iregs[10]),
         StackFrameARM::CONTEXT_VALID_R10},
        {"r12", &(frame_arm->context.iregs[12]),
         StackFrameARM::CONTEXT_VALID_R12},
        {"fp", &(frame_arm->context.iregs[11]),
         StackFrameARM::CONTEXT_VALID_FP},
        {"sp", &(frame_arm->context.iregs[13]),
         StackFrameARM::CONTEXT_VALID_SP},
        {"lr", &(frame_arm->context.iregs[14]),
         StackFrameARM::CONTEXT_VALID_LR},
        {"pc", &(frame_arm->context.iregs[15]),
         StackFrameARM::CONTEXT_VALID_PC},
    };

    printf(" ");
    for (int i = 0; i < 16; ++i) {
      const int width = 80;  // 4 columns wide
      if (frame_arm->context_validity & data[i].validity_mask) {
        sequence =
            PrintRegister(data[i].name, *(data[i].value), sequence, width);
      }
    }
  } else if (cpu == "arm64") {
    const StackFrameARM64* frame_arm64 =
        reinterpret_cast<const StackFrameARM64*>(frame);

    Register64 data[] = {
        {"x0", &(frame_arm64->context.iregs[0]),
         StackFrameARM64::CONTEXT_VALID_X0},
        {"x1", &(frame_arm64->context.iregs[1]),
         StackFrameARM64::CONTEXT_VALID_X1},
        {"x2", &(frame_arm64->context.iregs[2]),
         StackFrameARM64::CONTEXT_VALID_X2},
        {"x3", &(frame_arm64->context.iregs[3]),
         StackFrameARM64::CONTEXT_VALID_X3},
        {"x4", &(frame_arm64->context.iregs[4]),
         StackFrameARM64::CONTEXT_VALID_X4},
        {"x5", &(frame_arm64->context.iregs[5]),
         StackFrameARM64::CONTEXT_VALID_X5},
        {"x6", &(frame_arm64->context.iregs[6]),
         StackFrameARM64::CONTEXT_VALID_X6},
        {"x7", &(frame_arm64->context.iregs[7]),
         StackFrameARM64::CONTEXT_VALID_X7},
        {"x8", &(frame_arm64->context.iregs[8]),
         StackFrameARM64::CONTEXT_VALID_X8},
        {"x9", &(frame_arm64->context.iregs[9]),
         StackFrameARM64::CONTEXT_VALID_X9},
        {"x10", &(frame_arm64->context.iregs[10]),
         StackFrameARM64::CONTEXT_VALID_X10},
        {"x11", &(frame_arm64->context.iregs[11]),
         StackFrameARM64::CONTEXT_VALID_X11},
        {"x12", &(frame_arm64->context.iregs[12]),
         StackFrameARM64::CONTEXT_VALID_X12},
        {"x13", &(frame_arm64->context.iregs[13]),
         StackFrameARM64::CONTEXT_VALID_X13},
        {"x14", &(frame_arm64->context.iregs[14]),
         StackFrameARM64::CONTEXT_VALID_X14},
        {"x15", &(frame_arm64->context.iregs[15]),
         StackFrameARM64::CONTEXT_VALID_X15},
        {"x16", &(frame_arm64->context.iregs[16]),
         StackFrameARM64::CONTEXT_VALID_X16},
        {"x17", &(frame_arm64->context.iregs[17]),
         StackFrameARM64::CONTEXT_VALID_X17},
        {"x18", &(frame_arm64->context.iregs[18]),
         StackFrameARM64::CONTEXT_VALID_X18},
        {"x19", &(frame_arm64->context.iregs[19]),
         StackFrameARM64::CONTEXT_VALID_X19},
        {"x20", &(frame_arm64->context.iregs[20]),
         StackFrameARM64::CONTEXT_VALID_X20},
        {"x21", &(frame_arm64->context.iregs[21]),
         StackFrameARM64::CONTEXT_VALID_X21},
        {"x22", &(frame_arm64->context.iregs[22]),
         StackFrameARM64::CONTEXT_VALID_X22},
        {"x23", &(frame_arm64->context.iregs[23]),
         StackFrameARM64::CONTEXT_VALID_X23},
        {"x24", &(frame_arm64->context.iregs[24]),
         StackFrameARM64::CONTEXT_VALID_X24},
        {"x25", &(frame_arm64->context.iregs[25]),
         StackFrameARM64::CONTEXT_VALID_X25},
        {"x26", &(frame_arm64->context.iregs[26]),
         StackFrameARM64::CONTEXT_VALID_X26},
        {"x27", &(frame_arm64->context.iregs[27]),
         StackFrameARM64::CONTEXT_VALID_X27},
        {"x28", &(frame_arm64->context.iregs[28]),
         StackFrameARM64::CONTEXT_VALID_X28},

        // Registers with a dedicated or conventional purpose.
        {"fp", &(frame_arm64->context.iregs[29]),
         StackFrameARM64::CONTEXT_VALID_FP},
        {"lr", &(frame_arm64->context.iregs[30]),
         StackFrameARM64::CONTEXT_VALID_LR},
        {"sp", &(frame_arm64->context.iregs[31]),
         StackFrameARM64::CONTEXT_VALID_SP},
        {"pc", &(frame_arm64->context.iregs[32]),
         StackFrameARM64::CONTEXT_VALID_PC},
    };

    printf(" ");
    for (int i = 0; i < 33; ++i) {
      const int width = 110;  // four wide
      if (frame_arm64->context_validity & data[i].validity_mask) {
        sequence =
            PrintRegister64(data[i].name, *(data[i].value), sequence, width);
      }

      if (i == 30) {
        sequence = 0;
        printf("\n ");
      }
    }
  }

  printf("\n\n");
}

// ContainsModule checks whether a given |module| is in the vector
// |modules_without_symbols|.
static bool ContainsModule(
    const vector<const CodeModule*>* modules,
    const CodeModule* module) {
  assert(modules);
  assert(module);
  vector<const CodeModule*>::const_iterator iter;
  for (iter = modules->begin(); iter != modules->end(); ++iter) {
    if (module->debug_file().compare((*iter)->debug_file()) == 0 &&
        module->debug_identifier().compare((*iter)->debug_identifier()) == 0) {
      return true;
    }
  }
  return false;
}

// PrintModule prints a single |module| to stdout.
// |modules_without_symbols| should contain the list of modules that were
// confirmed to be missing their symbols during the stack walk.
static void PrintModule(
    const CodeModule* module,
    const vector<const CodeModule*>* modules_without_symbols,
    const vector<const CodeModule*>* modules_with_corrupt_symbols,
    uint64_t main_address) {
  string symbol_issues;
  if (ContainsModule(modules_without_symbols, module)) {
    symbol_issues = "  (WARNING: No symbols, " +
        PathnameStripper::File(module->debug_file()) + ", " +
        module->debug_identifier() + ")";
  } else if (ContainsModule(modules_with_corrupt_symbols, module)) {
    symbol_issues = "  (WARNING: Corrupt symbols, " +
        PathnameStripper::File(module->debug_file()) + ", " +
        module->debug_identifier() + ")";
  }
  uint64_t base_address = module->base_address();
  printf("0x%08" PRIx64 " - 0x%08" PRIx64 "  %s  %s%s%s\n",
         base_address, base_address + module->size() - 1,
         PathnameStripper::File(module->code_file()).c_str(),
         module->version().empty() ? "???" : module->version().c_str(),
         main_address != 0 && base_address == main_address ? "  (main)" : "",
         symbol_issues.c_str());
}

// PrintModules prints the list of all loaded |modules| to stdout.
// |modules_without_symbols| should contain the list of modules that were
// confirmed to be missing their symbols during the stack walk.
static void PrintModules(
    const CodeModules* modules,
    const vector<const CodeModule*>* modules_without_symbols,
    const vector<const CodeModule*>* modules_with_corrupt_symbols) {
  if (!modules)
    return;

  printf("\n");
  printf("Loaded modules:\n");

  uint64_t main_address = 0;
  const CodeModule* main_module = modules->GetMainModule();
  if (main_module) {
    main_address = main_module->base_address();
  }

  unsigned int module_count = modules->module_count();
  for (unsigned int module_sequence = 0;
       module_sequence < module_count;
       ++module_sequence) {
    const CodeModule* module = modules->GetModuleAtSequence(module_sequence);
    PrintModule(module, modules_without_symbols, modules_with_corrupt_symbols,
                main_address);
  }
}

// PrintModulesMachineReadable outputs a list of loaded modules,
// one per line, in the following machine-readable pipe-delimited
// text format:
// Module|{Module Filename}|{Version}|{Debug Filename}|{Debug Identifier}|
// {Base Address}|{Max Address}|{Main}
static void PrintModulesMachineReadable(const CodeModules* modules) {
  if (!modules)
    return;

  uint64_t main_address = 0;
  const CodeModule* main_module = modules->GetMainModule();
  if (main_module) {
    main_address = main_module->base_address();
  }

  unsigned int module_count = modules->module_count();
  for (unsigned int module_sequence = 0;
       module_sequence < module_count;
       ++module_sequence) {
    const CodeModule* module = modules->GetModuleAtSequence(module_sequence);
    uint64_t base_address = module->base_address();
    printf("Module%c%s%c%s%c%s%c%s%c0x%08" PRIx64 "%c0x%08" PRIx64 "%c%d\n",
           kOutputSeparator,
           StripSeparator(PathnameStripper::File(module->code_file())).c_str(),
           kOutputSeparator, StripSeparator(module->version()).c_str(),
           kOutputSeparator,
           StripSeparator(PathnameStripper::File(module->debug_file())).c_str(),
           kOutputSeparator,
           StripSeparator(module->debug_identifier()).c_str(),
           kOutputSeparator, base_address,
           kOutputSeparator, base_address + module->size() - 1,
           kOutputSeparator,
           main_module != NULL && base_address == main_address ? 1 : 0);
  }
}

// Prints a list of loaded modules to stdout using the Apple Crash Report
// format.
static void PrintModulesAppleCrashReport(const ProcessState& process_state) {
  string cpu = process_state.system_info()->cpu;
  const CodeModules* modules = process_state.modules();

  if (!modules)
    return;

  printf("Binary Images:\n");

  uint64_t main_address = 0;
  const CodeModule* main_module = modules->GetMainModule();
  if (main_module) {
    main_address = main_module->base_address();
  }

  unsigned int module_count = modules->module_count();
  for (unsigned int module_sequence = 0; module_sequence < module_count;
       ++module_sequence) {
    const CodeModule* module = modules->GetModuleAtSequence(module_sequence);
    uint64_t base_address = module->base_address();
    string debug_id = module->debug_identifier();
    debug_id.erase(debug_id.length() - 1, string::npos);

    printf("0x%09" PRIx64 " - 0x%09" PRIx64, base_address,
           base_address + module->size() - 1);
    printf(" %s %s <%s> %s\n",
           PathnameStripper::File(module->code_file()).c_str(), cpu.c_str(),
           debug_id.c_str(), module->code_file().c_str());
  }
  printf("\n");
}
}  // namespace

void PrintProcessState(const ProcessState& process_state,
                       bool output_stack_contents,
                       bool output_requesting_thread_only,
                       SourceLineResolverInterface* resolver) {
  // Print OS and CPU information.
  string cpu = process_state.system_info()->cpu;
  string cpu_info = process_state.system_info()->cpu_info;
  printf("Operating system: %s\n", process_state.system_info()->os.c_str());
  printf("                  %s\n",
         process_state.system_info()->os_version.c_str());
  printf("CPU: %s\n", cpu.c_str());
  if (!cpu_info.empty()) {
    // This field is optional.
    printf("     %s\n", cpu_info.c_str());
  }
  printf("     %d CPU%s\n",
         process_state.system_info()->cpu_count,
         process_state.system_info()->cpu_count != 1 ? "s" : "");
  printf("\n");

  // Print GPU information
  string gl_version = process_state.system_info()->gl_version;
  string gl_vendor = process_state.system_info()->gl_vendor;
  string gl_renderer = process_state.system_info()->gl_renderer;
  printf("GPU:");
  if (!gl_version.empty() || !gl_vendor.empty() || !gl_renderer.empty()) {
    printf(" %s\n", gl_version.c_str());
    printf("     %s\n", gl_vendor.c_str());
    printf("     %s\n", gl_renderer.c_str());
  } else {
    printf(" UNKNOWN\n");
  }
  printf("\n");

  // Print crash information.
  if (process_state.crashed()) {
    printf("Crash reason:  %s\n", process_state.crash_reason().c_str());
    printf("Crash address: 0x%" PRIx64 "\n", process_state.crash_address());
  } else {
    printf("No crash\n");
  }

  string assertion = process_state.assertion();
  if (!assertion.empty()) {
    printf("Assertion: %s\n", assertion.c_str());
  }

  // Compute process uptime if the process creation and crash times are
  // available in the dump.
  if (process_state.time_date_stamp() != 0 &&
      process_state.process_create_time() != 0 &&
      process_state.time_date_stamp() >= process_state.process_create_time()) {
    printf("Process uptime: %d seconds\n",
           process_state.time_date_stamp() -
               process_state.process_create_time());
  } else {
    printf("Process uptime: not available\n");
  }

  // If the thread that requested the dump is known, print it first.
  int requesting_thread = process_state.requesting_thread();
  if (requesting_thread != -1) {
    printf("\n");
    printf("Thread %d (%s)\n",
          requesting_thread,
          process_state.crashed() ? "crashed" :
                                    "requested dump, did not crash");
    PrintStack(process_state.threads()->at(requesting_thread), cpu,
               output_stack_contents,
               process_state.thread_memory_regions()->at(requesting_thread),
               process_state.modules(), resolver);
  }

  if (!output_requesting_thread_only) {
    // Print all of the threads in the dump.
    int thread_count = process_state.threads()->size();
    for (int thread_index = 0; thread_index < thread_count; ++thread_index) {
      if (thread_index != requesting_thread) {
        // Don't print the crash thread again, it was already printed.
        printf("\n");
        printf("Thread %d\n", thread_index);
        PrintStack(process_state.threads()->at(thread_index), cpu,
                  output_stack_contents,
                  process_state.thread_memory_regions()->at(thread_index),
                  process_state.modules(), resolver);
      }
    }
  }

  PrintModules(process_state.modules(),
               process_state.modules_without_symbols(),
               process_state.modules_with_corrupt_symbols());
}

void PrintProcessStateMachineReadable(const ProcessState& process_state) {
  // Print OS and CPU information.
  // OS|{OS Name}|{OS Version}
  // CPU|{CPU Name}|{CPU Info}|{Number of CPUs}
  // GPU|{GPU version}|{GPU vendor}|{GPU renderer}
  printf("OS%c%s%c%s\n", kOutputSeparator,
         StripSeparator(process_state.system_info()->os).c_str(),
         kOutputSeparator,
         StripSeparator(process_state.system_info()->os_version).c_str());
  printf("CPU%c%s%c%s%c%d\n", kOutputSeparator,
         StripSeparator(process_state.system_info()->cpu).c_str(),
         kOutputSeparator,
         // this may be empty
         StripSeparator(process_state.system_info()->cpu_info).c_str(),
         kOutputSeparator,
         process_state.system_info()->cpu_count);
  printf("GPU%c%s%c%s%c%s\n", kOutputSeparator,
         StripSeparator(process_state.system_info()->gl_version).c_str(),
         kOutputSeparator,
         StripSeparator(process_state.system_info()->gl_vendor).c_str(),
         kOutputSeparator,
         StripSeparator(process_state.system_info()->gl_renderer).c_str());

  int requesting_thread = process_state.requesting_thread();

  // Print crash information.
  // Crash|{Crash Reason}|{Crash Address}|{Crashed Thread}
  printf("Crash%c", kOutputSeparator);
  if (process_state.crashed()) {
    printf("%s%c0x%" PRIx64 "%c",
           StripSeparator(process_state.crash_reason()).c_str(),
           kOutputSeparator, process_state.crash_address(), kOutputSeparator);
  } else {
    // print assertion info, if available, in place of crash reason,
    // instead of the unhelpful "No crash"
    string assertion = process_state.assertion();
    if (!assertion.empty()) {
      printf("%s%c%c", StripSeparator(assertion).c_str(),
             kOutputSeparator, kOutputSeparator);
    } else {
      printf("No crash%c%c", kOutputSeparator, kOutputSeparator);
    }
  }

  if (requesting_thread != -1) {
    printf("%d\n", requesting_thread);
  } else {
    printf("\n");
  }

  PrintModulesMachineReadable(process_state.modules());

  // blank line to indicate start of threads
  printf("\n");

  // If the thread that requested the dump is known, print it first.
  if (requesting_thread != -1) {
    PrintStackMachineReadable(requesting_thread,
                              process_state.threads()->at(requesting_thread));
  }

  // Print all of the threads in the dump.
  int thread_count = process_state.threads()->size();
  for (int thread_index = 0; thread_index < thread_count; ++thread_index) {
    if (thread_index != requesting_thread) {
      // Don't print the crash thread again, it was already printed.
      PrintStackMachineReadable(thread_index,
                                process_state.threads()->at(thread_index));
    }
  }
}

void PrintProcessStateAppleCrashReport(const ProcessState& process_state) {
  //
  // Print Apple Crash Report header.
  //
  printf("Incident Identifier: 04C9BA62-4E89-4442-9B4D-CD0C77D7D8B9\n");
  printf("CrashReporter Key:   255bba4521a5fd9591cf59e36ded07c28bb82f02\n");
  printf("Hardware model:      UNKNOWN\n");
  printf("Process:             %s [%d]\n",
         PathnameStripper::File(
             process_state.modules()->GetMainModule()->code_file())
             .c_str(),
         process_state.process_id());
  printf("Path:                %s\n",
         process_state.modules()->GetMainModule()->code_file().c_str());

  // Bundle ID is not available in the minidump, so hard-code some values for OS
  // X and iOS.
  string bundle_id = (process_state.system_info()->os == "iOS")
                         ? "com.google.chrome.ios"
                         : "com.google.chrome";
  printf("Identifier:          %s\n", bundle_id.c_str());
  printf("Version:             UNKNOWN\n");

  string cpu = process_state.system_info()->cpu;
  printf("Code Type:           %s (Native)\n",
         process_state.system_info()->cpu == "arm64" ? "ARM-64" : "ARM");
  printf("Parent Process:      launchd [1]\n\n");

  char dateBuf[64];
  time_t crash = (time_t)process_state.time_date_stamp();
  struct tm* crash_time = localtime(&crash);
  strftime(dateBuf, 64, "%Y-%m-%d %k:%M:%S.000 %z", crash_time);
  printf("Date/Time:           %s\n", dateBuf);

  crash = (time_t)process_state.process_create_time();
  crash_time = localtime(&crash);
  strftime(dateBuf, 64, "%Y-%m-%d %k:%M:%S.000 %z", crash_time);
  printf("Launch Time:         %s\n", dateBuf);

  // TODO(rohitrao): What about Mac crashes?
  // TODO(rohitrao): Add parens around the build number.
  printf("OS Version:          %s %s\n",
         process_state.system_info()->os.c_str(),
         process_state.system_info()->os_version.c_str());
  printf("Report Version:      104\n\n");

  printf("Exception Type:  %s\n", process_state.crash_reason().c_str());

  // TODO(rohitrao): What goes here?
  printf("Exception Codes: %s\n", "");
  printf("Triggered by Thread:  %d\n\n", process_state.requesting_thread());

  //
  // Print thread information and stack traces.
  //
  int requesting_thread = process_state.requesting_thread();
  int thread_count = process_state.threads()->size();
  for (int thread_index = 0; thread_index < thread_count; ++thread_index) {
    bool crashed = requesting_thread == thread_index && process_state.crashed();

    printf("Thread %d%s:\n", thread_index, crashed ? " Crashed" : "");
    PrintStackAppleCrashReport(thread_index,
                               process_state.threads()->at(thread_index));
    printf("\n");
  }

  PrintThreadStateAppleCrashReport(process_state);
  PrintModulesAppleCrashReport(process_state);
}

}  // namespace google_breakpad
