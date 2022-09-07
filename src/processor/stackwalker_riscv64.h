/* stackwalker_riscv64.h: riscv64-specific stackwalker.
 *
 * Provides stack frames given riscv64 register context and a memory region
 * corresponding to a riscv64 stack.
 *
 * Author: Iacopo Colonnelli
 */

#ifndef PROCESSOR_STACKWALKER_RISCV64_H__
#define PROCESSOR_STACKWALKER_RISCV64_H__

#include "google_breakpad/common/minidump_format.h"
#include "google_breakpad/processor/stackwalker.h"

namespace google_breakpad {

class CodeModules;

class StackwalkerRISCV64 : public Stackwalker {
public:
  // Context is a riscv context object that gives access to riscv-specific
  // register state corresponding to the innermost called frame to be
  // included in the stack.  The other arguments are passed directly
  // through to the base Stackwalker constructor.
  StackwalkerRISCV64(const SystemInfo* system_info,
                     const MDRawContextRISCV64* context,
                     MemoryRegion* memory,
                     const CodeModules* modules,
                     StackFrameSymbolizer* frame_symbolizer);

  // Change the context validity mask of the frame returned by
  // GetContextFrame to VALID. This is only for use by unit tests; the
  // default behavior is correct for all application code.
  void SetContextFrameValidity(int valid) {
    context_frame_validity_ = valid;
  }

private:
  // Implementation of Stackwalker, using riscv context and stack conventions.
  virtual StackFrame* GetContextFrame();
  virtual StackFrame* GetCallerFrame(
      const CallStack* stack, bool stack_scan_allowed);

  // Use cfi_frame_info (derived from STACK CFI records) to construct
  // the frame that called frames.back(). The caller takes ownership
  // of the returned frame. Return NULL on failure.
  StackFrameRISCV64* GetCallerByCFIFrameInfo(
      const vector<StackFrame*>& frames, CFIFrameInfo* cfi_frame_info);

  // Use the frame pointer. The caller takes ownership of the returned frame.
  // Return NULL on failure.
  StackFrameRISCV64* GetCallerByFramePointer(
      const vector<StackFrame*>& frames);

  // Scan the stack for plausible return addresses. The caller takes ownership
  // of the returned frame. Return NULL on failure.
  StackFrameRISCV64* GetCallerByStackScan(
      const vector<StackFrame*>& frames);

  // Stores the CPU context corresponding to the innermost stack frame to
  // be returned by GetContextFrame.
  const MDRawContextRISCV64* context_;

  // Validity mask for youngest stack frame. This is always
  // CONTEXT_VALID_ALL in real use; it is only changeable for the sake of
  // unit tests.
  int context_frame_validity_;
};

}  // namespace google_breakpad

#endif // PROCESSOR_STACKWALKER_RISCV64_H__
