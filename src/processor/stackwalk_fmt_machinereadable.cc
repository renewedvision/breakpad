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

// stackwalk_fmt_*.cc: Modules shared by the {micro,mini}dump_stackwalck
// executables to print the content of dumps (w/ stack traces) on the console.
//
// Author: Mark Mentovai

#include "processor/stackwalk_fmt_machinereadable.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

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

}  // namespace


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

}  // namespace google_breakpad
