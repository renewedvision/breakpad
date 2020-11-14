// pid2md.cc: An utility to generate a minidump from a living process

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "client/linux/minidump_writer/minidump_writer.h"

int main(int argc, char *argv[]) {
  if (argc < 1) {
    return EXIT_FAILURE;
  } else if (argc != 3) {
    fprintf(stderr, "Usage: %s <process id> <minidump>\n", argv[0]);
    return EXIT_FAILURE;
  }

  pid_t process_id= atoi(argv[1]);
  const char* minidump_file = argv[2];

  if (!google_breakpad::WriteMinidump(minidump_file, process_id, process_id)) {
    fprintf(stderr, "Unable to generate minidump.\n");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

