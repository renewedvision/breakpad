#ifndef CLIENT_LINUX_MINIDUMP_WRITER_LINUX_LIVE_CORE_DUMPER_H_
#define CLIENT_LINUX_MINIDUMP_WRITER_LINUX_LIVE_CORE_DUMPER_H_

#include <vector>

#include "client/linux/minidump_writer/linux_dumper.h"
#include "common/linux/elf_core_dump.h"

namespace google_breakpad {

// This allocates memory on the heap, so don't use from the exception handler.
class LinuxLiveCoreDumper : public LinuxDumper {
 public:
  LinuxLiveCoreDumper(pid_t pid, int core_fd);
  ~LinuxLiveCoreDumper() override;

  LinuxLiveCoreDumper(const LinuxLiveCoreDumper&) = delete;
  LinuxLiveCoreDumper& operator=(const LinuxLiveCoreDumper&) = delete;

  // public LinuxDumper implementation:
  bool Init() override;
  bool IsPostMortem() const override;
  bool ThreadsSuspend() override;
  bool ThreadsResume() override;
  bool GetThreadInfoByIndex(size_t index, ThreadInfo* info) override;
  bool CopyFromProcess(void* dest, pid_t child, const void* src,
                       size_t length) override;
  bool BuildProcPath(char* path, pid_t pid, const char* node) const override;

 protected:
  // protected LinuxDumper implementation:
  bool EnumerateThreads() override;

  bool ReadFromCore(off_t target_offset, void* buffer, size_t bytes_to_read);

  const int core_fd_;
  off_t core_offset_ = 0;
  std::vector<ElfCoreDump::Phdr> phdrs_;
  std::vector<std::unique_ptr<unsigned char[]>> phdr_segments_;
  wasteful_vector<ThreadInfo> thread_infos_;
};

}  // namespace google_breakpad

#endif  // CLIENT_LINUX_MINIDUMP_WRITER_LINUX_LIVE_CORE_DUMPER_H_
