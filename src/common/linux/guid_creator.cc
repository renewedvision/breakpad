// Copyright (c) 2006, Google Inc.
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

#include "common/linux/guid_creator.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#if !defined(HAVE_ARC4RANDOM)
#include <sys/stat.h>
#include <fcntl.h>
#endif

//
// GUIDGenerator
//
// This class is used to generate random GUID.
// Currently use random number to generate a GUID since Linux has
// no native GUID generator. This should be OK since we don't expect
// crash to happen very offen.
//
class GUIDGenerator {
 public:
  static uint32_t BytesToUInt32(const uint8_t bytes[]) {
    return ((uint32_t) bytes[0]
            | ((uint32_t) bytes[1] << 8)
            | ((uint32_t) bytes[2] << 16)
            | ((uint32_t) bytes[3] << 24));
  }

  static void UInt32ToBytes(uint8_t bytes[], uint32_t n) {
    bytes[0] = n & 0xff;
    bytes[1] = (n >> 8) & 0xff;
    bytes[2] = (n >> 16) & 0xff;
    bytes[3] = (n >> 24) & 0xff;
  }

  static bool CreateGUID(GUID *guid) {
#if defined(HAVE_ARC4RANDOM) // Android, BSD, ...
    CreateGuidFromArc4Random(guid);
#else // Linux
    if (!CreateGUIDFromDevUrandom(guid)) {
      CreateGUIDFromRand(guid);
    }
#endif

    /* Put in the version */
    guid->data3 &= 0x0fff;
    guid->data3 |= 0x4000;

    /* Put in the variant */
    guid->data4[0] &= 0x3f;
    guid->data4[0] |= 0x80;

    return true;
  }

 private:
#ifdef HAVE_ARC4RANDOM
  static void CreateGuidFromArc4Random(GUID *guid) {
    char *buff = (char *)guid;

    for (size_t i = 0; i < sizeof(GUID); i += sizeof(uint32_t)) {
      uint32_t random_data = arc4random();

      memcpy(buff + i, &random_data, sizeof(uint32_t));
    }
  }
#else
  static void InitOnce() {
    pthread_once(&once_control, &InitOnceImpl);
  }

  static void InitOnceImpl() {
    // time(NULL) is a very poor seed, so lacking anything better mix an
    // address into it.
    srand(time(NULL) | (((uintptr_t)&once_control) >> 4));
  }

  static pthread_once_t once_control;

  // Populate the GUID using random bytes read from /dev/urandom, returns false
  // if the GUID wasn't fully populated with random data
  static bool CreateGUIDFromDevUrandom(GUID *guid) {
    size_t read_bytes = 0;
    char *buff = (char *)guid;
    int fd = open("/dev/urandom", O_RDONLY);

    if (fd != -1) {
      do {
        int res = read(fd, buff + read_bytes, sizeof(GUID) - read_bytes);

        if (res < 0) {
          break;
        }

        read_bytes += res;
      } while (read_bytes < sizeof(GUID));

      close(fd);
    }

    return (read_bytes == sizeof(GUID));
  }

  // Populate the GUID using a stream of random bytes obtained from rand()
  static void CreateGUIDFromRand(GUID *guid) {
    char *buff = (char *)guid;

    InitOnce();

    for (size_t i = 0; i < sizeof(GUID); i++) {
      buff[i] = rand();
    }
  }
#endif
};

#ifndef HAVE_ARC4RANDOM
pthread_once_t GUIDGenerator::once_control = PTHREAD_ONCE_INIT;
#endif

bool CreateGUID(GUID *guid) {
  return GUIDGenerator::CreateGUID(guid);
}

// Parse guid to string.
bool GUIDToString(const GUID *guid, char *buf, int buf_len) {
  // Should allow more space the the max length of GUID.
  assert(buf_len > kGUIDStringLength);
  int num = snprintf(buf, buf_len, kGUIDFormatString,
                     guid->data1, guid->data2, guid->data3,
                     GUIDGenerator::BytesToUInt32(&(guid->data4[0])),
                     GUIDGenerator::BytesToUInt32(&(guid->data4[4])));
  if (num != kGUIDStringLength)
    return false;

  buf[num] = '\0';
  return true;
}
