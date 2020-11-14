// core_handler.cc: An utility to handle coredumps on Linux

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "client/linux/minidump_writer/minidump_writer.h"
#include "client/linux/minidump_writer/linux_core_dumper.h"

using google_breakpad::AppMemoryList;
using google_breakpad::MappingList;
using google_breakpad::LinuxCoreDumper;

void ShowUsage(const char* argv0) {
  fprintf(stderr, "Usage: %s <pid> <minidump output>\n", argv0);
}

bool WriteMinidumpFromCore(const char* filename,
    const char* core_path,
    const char* procfs_override) {
  MappingList mappings;
  AppMemoryList memory_list;
  LinuxCoreDumper dumper(0, core_path, procfs_override);
  return google_breakpad::WriteMinidump(filename, mappings, memory_list,
      &dumper);
}

bool HandleCrash(pid_t pid, const char* procfs_dir, const char* md_filename) {

  /* Only reads the first mega byte from stdin and save it in a file in order
   * to force the dumper to fallback to /proc/<pid>/mem */
  const int buf_size = 1024*1024;
  char *buf = (char*)malloc(buf_size);
  if (buf == NULL) {
      fprintf(stderr, "failed to allocate coredump buffer\n");
      return false;
  }
  int r = 0;
  while (r != buf_size) {
    int ret = read(STDIN_FILENO, buf + r, buf_size - r);
    if (ret == 0) {
      break;
    } else if (ret == -1) {
      fprintf(stderr, "failed to read core (%d/%d, %d)\n", r, buf_size, errno);
      return false;
    }
    r += ret;
  }

  int fd = memfd_create("core_file", 0);
  if (fd == -1) {
    fprintf(stderr, "failed to create core file (%d)\n", errno);
    return false;
  }
  ssize_t core_file_len = snprintf(NULL, 0, "/proc/self/fd/%d", fd) + 1;
  char* core_file = (char*)malloc(core_file_len);
  if (core_file == NULL) {
    fprintf(stderr, "failed to allocate core filename\n");
    return false;
  }
  snprintf(core_file, core_file_len, "/proc/self/fd/%d", fd);

  int w = write(fd, buf, r);
  if (w != r) {
    fprintf(stderr, "failed to write core dump (%d/%d, %d)\n", w, r, errno);
    close(fd);
    return false;
  }
  free(buf); buf = NULL;

  if (!WriteMinidumpFromCore(md_filename, core_file, procfs_dir)) {
    fprintf(stderr, "Unable to generate minidump.\n");
    close(fd);
    return false;
  }
  close(fd);

  return true;
}


int main(int argc, char *argv[]) {
  int ret = EXIT_FAILURE;

  if (argc < 1) {
    return ret;
  } else if (argc != 3) {
    ShowUsage(argv[0]);
    return ret;
  }

  const char* pid_str = argv[1];
  const char* md_filename = argv[2];
  pid_t pid = atoi(pid_str);

  FILE *fl = popen("/usr/bin/logger -t core_handler", "w");
  if(fl == NULL) {
    fprintf(stderr, "failed to open logger\n");
    return ret;
  }

  /* redirect stdout and stderr to the logger */
  int nf = fileno(fl);
  dup2(nf, STDOUT_FILENO);
  dup2(nf, STDERR_FILENO);

  int procfs_dir_len = snprintf(NULL, 0, "/proc/%s", pid_str) + 1;
  char* procfs_dir = (char*)malloc(procfs_dir_len);
  if (procfs_dir == NULL) {
    fprintf(stderr, "failed to allocate /proc/<pid> string\n");
    return ret;
  }
  snprintf(procfs_dir, procfs_dir_len, "/proc/%s", pid_str);

  if (HandleCrash(pid, procfs_dir, md_filename)) {
    fprintf(stderr, "minidump generated at %s\n", md_filename);
    ret = EXIT_SUCCESS;
  } else {
    fprintf(stderr, "cannot generate minidump %s.\n", md_filename);
  }

  free(procfs_dir);

  fflush(stdout);
  fflush(stderr);
  fclose(stdout);
  fclose(stderr);
  pclose(fl);

  return ret;
}
