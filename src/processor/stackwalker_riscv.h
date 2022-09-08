/* stackwalker_riscv.h: riscv-specific stackwalker.
 *
 * Provides stack frames given riscv register context and a memory region
 * corresponding to a riscv stack.
 *
 * Author: Iacopo Colonnelli
 */


#ifndef PROCESSOR_STACKWALKER_RISCV_H__
#define PROCESSOR_STACKWALKER_RISCV_H__

#include "google_breakpad/common/minidump_format.h"
#include "google_breakpad/processor/stackwalker.h"

namespace google_breakpad {

class CodeModules;

class StackwalkerRISCV : public Stackwalker {
public:
  // Context is a riscv context object that gives access to riscv-specific
  // register state corresponding to the innermost called frame to be
  // included in the stack.  The other arguments are passed directly
  // through to the base Stackwalker constructor.
  StackwalkerRISCV(const SystemInfo* system_info,
                   const MDRawContextRISCV* context,
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
  StackFrameRISCV* GetCallerByCFIFrameInfo(
      const vector<StackFrame*>& frames, CFIFrameInfo* cfi_frame_info);

  // Use the frame pointer. The caller takes ownership of the returned frame.
  // Return NULL on failure.
  StackFrameRISCV* GetCallerByFramePointer(
      const vector<StackFrame*>& frames);

  // Scan the stack for plausible return addresses. The caller takes ownership
  // of the returned frame. Return NULL on failure.
  StackFrameRISCV* GetCallerByStackScan(
      const vector<StackFrame*>& frames);

  // Stores the CPU context corresponding to the innermost stack frame to
  // be returned by GetContextFrame.
  const MDRawContextRISCV* context_;

  // Validity mask for youngest stack frame. This is always
  // CONTEXT_VALID_ALL in real use; it is only changeable for the sake of
  // unit tests.
  int context_frame_validity_;
};

}  // namespace google_breakpad

#endif // PROCESSOR_STACKWALKER_RISCV_H__
