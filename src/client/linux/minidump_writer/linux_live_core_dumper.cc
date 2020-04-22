#include "client/linux/minidump_writer/linux_live_core_dumper.h"

#include <fcntl.h>
#include <sys/procfs.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include "common/linux/eintr_wrapper.h"
#include "common/linux/linux_libc_support.h"

namespace google_breakpad {

LinuxLiveCoreDumper::LinuxLiveCoreDumper(pid_t pid, int core_fd)
    : LinuxDumper(pid),
      core_fd_(core_fd),
      thread_infos_(&allocator_, 8) {}

LinuxLiveCoreDumper::~LinuxLiveCoreDumper() {}

bool LinuxLiveCoreDumper::Init() {
  // Read the ELF header.
  ElfCoreDump::Ehdr ehdr;
  if (!ReadFromCore(0 /* offset */, &ehdr, sizeof(ehdr))) {
    fprintf(stderr, "Could not read the ELF header\n");
    return false;
  }
  if (ehdr.e_ident[0] != ELFMAG0 ||
      ehdr.e_ident[1] != ELFMAG1 ||
      ehdr.e_ident[2] != ELFMAG2 ||
      ehdr.e_ident[3] != ELFMAG3 ||
      ehdr.e_ident[4] != ElfCoreDump::kClass ||
      ehdr.e_version != EV_CURRENT ||
      ehdr.e_type != ET_CORE) {
    fprintf(stderr, "Invalid ELF header\n");
    return false;
  }

  // Read the program headers.
  phdrs_.resize(ehdr.e_phnum);
  for (unsigned i = 0; i < ehdr.e_phnum; ++i) {
    off_t poffset = ehdr.e_phoff + ehdr.e_phentsize*i;
    if (!ReadFromCore(poffset, &phdrs_[i], sizeof(ElfCoreDump::Phdr))) {
      fprintf(stderr, "Could not read the ELF program header %d\n", i);
      return false;
    }
  }

  // Read the file data for the program headers.
  phdr_segments_.resize(ehdr.e_phnum);
  for (unsigned i = 0; i < ehdr.e_phnum; ++i) {
    if (phdrs_[i].p_type == PT_NOTE) {
      phdr_segments_[i] = std::make_unique<unsigned char[]>(phdrs_[i].p_filesz);
      if (!ReadFromCore(phdrs_[i].p_offset, phdr_segments_[i].get(), phdrs_[i].p_filesz)) {
        fprintf(stderr, "Failed to read phdr data %d\n", i);
        return false;
      }
    }
  }

  return LinuxDumper::Init();
}

bool LinuxLiveCoreDumper::IsPostMortem() const {
  // Since we are working from the core dump, return true.
  return true;
}

bool LinuxLiveCoreDumper::ThreadsSuspend() {
  return true;
}

bool LinuxLiveCoreDumper::ThreadsResume() {
  return true;
}

bool LinuxLiveCoreDumper::GetThreadInfoByIndex(size_t index, ThreadInfo* info) {
  if (index >= thread_infos_.size())
    return false;

  *info = thread_infos_[index];
  return true;
}

bool LinuxLiveCoreDumper::CopyFromProcess(void* dest, pid_t child,
                                          const void* src, size_t length) {
  char path[PATH_MAX+1];
  if (!BuildProcPath(path, child, "mem")) {
    return false;
  }

  int mem_fd = open(path, O_RDONLY);
  if (mem_fd == -1) {
    fprintf(stderr, "Failed to open %s\n", path);
    return false;
  }

  bool success = false;
  if (lseek(mem_fd, (off_t)src, SEEK_SET) == (off_t)src) {
    if (IGNORE_EINTR(read(mem_fd, dest, length)) == static_cast<ssize_t>(length)) {
      success = true;
    }
  }

  close(mem_fd);
  return success;
}

bool LinuxLiveCoreDumper::BuildProcPath(char* path, pid_t pid,
                                        const char* node) const {
  if (!path || !node || pid <= 0)
    return false;

  size_t node_len = my_strlen(node);
  if (node_len == 0)
    return false;

  const unsigned pid_len = my_uint_len(pid);
  const size_t total_length = 6 + pid_len + 1 + node_len;
  if (total_length >= NAME_MAX)
    return false;

  my_memcpy(path, "/proc/", 6);
  my_uitos(path + 6, pid, pid_len);
  path[6 + pid_len] = '/';
  my_memcpy(path + 6 + pid_len + 1, node, node_len);
  path[total_length] = '\0';
  return true;
}

bool LinuxLiveCoreDumper::EnumerateThreads() {
  // Parse the PT_NOTEs.
  for (unsigned i = 0; i < phdrs_.size(); ++i) {
    if (phdrs_[i].p_type != PT_NOTE) {
      continue;
    }

    MemoryRange note_content(phdr_segments_[i].get(), phdrs_[i].p_filesz);
    for (ElfCoreDump::Note note(note_content); note.IsValid(); note = note.GetNextNote()) {
      // Validate the PT_NOTE.
      ElfCoreDump::Word type = note.GetType();
      MemoryRange name = note.GetName();
      MemoryRange description = note.GetDescription();
      if (type == 0 || name.IsEmpty() || description.IsEmpty()) {
        fprintf(stderr, "Invalid note at %d\n", i);
        continue;
      }

      // Parse the PT_NOTE (copied from LinuxCoreDumper::EnumerateThreads()).
      // Based on write_note_info() in linux/kernel/fs/binfmt_elf.c, notes are
      // ordered as follows (NT_PRXFPREG and NT_386_TLS are i386 specific):
      //   Thread           Name          Type
      //   -------------------------------------------------------------------
      //   1st thread       CORE          NT_PRSTATUS
      //   process-wide     CORE          NT_PRPSINFO
      //   process-wide     CORE          NT_SIGINFO
      //   process-wide     CORE          NT_AUXV
      //   1st thread       CORE          NT_FPREGSET
      //   1st thread       LINUX         NT_PRXFPREG
      //   1st thread       LINUX         NT_386_TLS
      //
      //   2nd thread       CORE          NT_PRSTATUS
      //   2nd thread       CORE          NT_FPREGSET
      //   2nd thread       LINUX         NT_PRXFPREG
      //   2nd thread       LINUX         NT_386_TLS
      //
      //   3rd thread       CORE          NT_PRSTATUS
      //   3rd thread       CORE          NT_FPREGSET
      //   3rd thread       LINUX         NT_PRXFPREG
      //   3rd thread       LINUX         NT_386_TLS
      //
      // The following code only works if notes are ordered as expected.
      switch (type) {
        case NT_PRPSINFO: {
          if (description.length() != sizeof(elf_prpsinfo)) {
            fprintf(stderr, "Found NT_PRPSINFO descriptor of unexpected size\n");
            return false;
          }

          const elf_prpsinfo* prpsinfo =
              reinterpret_cast<const elf_prpsinfo*>(description.data());
          if (prpsinfo->pr_pid != pid_) {
            fprintf(stderr, "Coredump is from process %d, but we're "
                    "analyzing process %d\n", prpsinfo->pr_pid, pid_);
            return false;
          }
          break;
        }
        case NT_PRSTATUS: {
          if (description.length() != sizeof(elf_prstatus)) {
            fprintf(stderr, "Found NT_PRSTATUS descriptor of unexpected size\n");
            return false;
          }

          const elf_prstatus* status =
              reinterpret_cast<const elf_prstatus*>(description.data());
          ThreadInfo info;
          memset(&info, 0, sizeof(ThreadInfo));
          info.tgid = status->pr_pgrp;
          info.ppid = status->pr_ppid;
#if defined(__mips__)
#if defined(__ANDROID__)
          for (int i = EF_R0; i <= EF_R31; i++)
            info.mcontext.gregs[i - EF_R0] = status->pr_reg[i];
#else  // __ANDROID__
          for (int i = EF_REG0; i <= EF_REG31; i++)
            info.mcontext.gregs[i - EF_REG0] = status->pr_reg[i];
#endif  // __ANDROID__
          info.mcontext.mdlo = status->pr_reg[EF_LO];
          info.mcontext.mdhi = status->pr_reg[EF_HI];
          info.mcontext.pc = status->pr_reg[EF_CP0_EPC];
#else  // __mips__
          memcpy(&info.regs, status->pr_reg, sizeof(info.regs));
#endif  // __mips__

#if defined(__i386)
          memcpy(&info.stack_pointer, &info.regs.esp, sizeof(info.regs.esp));
#elif defined(__x86_64)
          memcpy(&info.stack_pointer, &info.regs.rsp, sizeof(info.regs.rsp));
#elif defined(__ARM_EABI__)
          memcpy(&info.stack_pointer, &info.regs.ARM_sp, sizeof(info.regs.ARM_sp));
#elif defined(__aarch64__)
          memcpy(&info.stack_pointer, &info.regs.sp, sizeof(info.regs.sp));
#elif defined(__mips__)
          info.stack_pointer =
              reinterpret_cast<uint8_t*>(info.mcontext.gregs[MD_CONTEXT_MIPS_REG_SP]);
#else
#error "This code hasn't been ported to your platform yet."
#endif

          if (threads_.empty()) {
            crash_thread_ = status->pr_pid;
            crash_signal_ = status->pr_info.si_signo;
            crash_signal_code_ = status->pr_info.si_code;
          }
          threads_.push_back(status->pr_pid);
          thread_infos_.push_back(info);
          break;
        }
        case NT_SIGINFO: {
          if (description.length() != sizeof(siginfo_t)) {
            fprintf(stderr, "Found NT_SIGINFO descriptor of unexpected size\n");
            return false;
          }

          const siginfo_t* info =
              reinterpret_cast<const siginfo_t*>(description.data());

          // Set crash_address when si_addr is valid for the signal.
          switch (info->si_signo) {
            case MD_EXCEPTION_CODE_LIN_SIGBUS:
            case MD_EXCEPTION_CODE_LIN_SIGFPE:
            case MD_EXCEPTION_CODE_LIN_SIGILL:
            case MD_EXCEPTION_CODE_LIN_SIGSEGV:
            case MD_EXCEPTION_CODE_LIN_SIGSYS:
            case MD_EXCEPTION_CODE_LIN_SIGTRAP:
              crash_address_ = reinterpret_cast<uintptr_t>(info->si_addr);
              break;
          }

          // Set crash_exception_info for common signals.  Since exception info is
          // unsigned, but some of these fields might be signed, we always cast.
          switch (info->si_signo) {
            case MD_EXCEPTION_CODE_LIN_SIGKILL:
              set_crash_exception_info({
                static_cast<uint64_t>(info->si_pid),
                static_cast<uint64_t>(info->si_uid),
              });
            break;
            case MD_EXCEPTION_CODE_LIN_SIGSYS:
#ifdef si_syscall
              set_crash_exception_info({
                static_cast<uint64_t>(info->si_syscall),
                static_cast<uint64_t>(info->si_arch),
              });
#endif
              break;
          }
          break;
        }
#if defined(__i386) || defined(__x86_64)
        case NT_FPREGSET: {
          if (thread_infos_.empty())
            return false;

          ThreadInfo* info = &thread_infos_.back();
          if (description.length() != sizeof(info->fpregs)) {
            fprintf(stderr, "Found NT_FPREGSET descriptor of unexpected size\n");
            return false;
          }

          memcpy(&info->fpregs, description.data(), sizeof(info->fpregs));
          break;
        }
#endif
#if defined(__i386)
        case NT_PRXFPREG: {
          if (thread_infos_.empty())
            return false;

          ThreadInfo* info = &thread_infos_.back();
          if (description.length() != sizeof(info->fpxregs)) {
            fprintf(stderr, "Found NT_PRXFPREG descriptor of unexpected size\n");
            return false;
          }

          memcpy(&info->fpxregs, description.data(), sizeof(info->fpxregs));
          break;
        }
#endif
      }
    }
  }

  return !threads_.empty();
}

bool LinuxLiveCoreDumper::ReadFromCore(off_t target_offset, void* buffer, size_t bytes_to_read) {
  if (target_offset < core_offset_) {
    // We can only move forward.
    return false;
  }

  // Skip to the target offset.
  while (core_offset_ < target_offset) {
    char buf[BUFSIZ];
    size_t remaining_skip = target_offset - core_offset_;
    size_t buf_read = std::min(remaining_skip, sizeof(buf));
    if (IGNORE_EINTR(read(core_fd_, buf, buf_read)) != static_cast<ssize_t>(buf_read)) {
      return false;
    }
    core_offset_ += buf_read;
  }

  if (IGNORE_EINTR(read(core_fd_, buffer, bytes_to_read)) != static_cast<ssize_t>(bytes_to_read)) {
    return false;
  }
  core_offset_ += bytes_to_read;

  return true;
}


}  // namespace google_breakpad
