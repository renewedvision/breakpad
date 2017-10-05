// Copyright (c) 2017, Google Inc.
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

#ifndef COMMON_LONG_STRING_DICTIONARY_H_
#define COMMON_LONG_STRING_DICTIONARY_H_

#include <math.h>
#include <string.h>

#include <algorithm>

#include "common/simple_string_dictionary.h"

#define arraysize(f) (sizeof(f) / sizeof(*f))

namespace {
// Suffixes for segment keys.
static const char *kSuffixes[] = {"__1", "__2", "__3", "__4", "__5",
                                  "__6", "__7", "__8", "__9", "__10"};
} // namespace

namespace google_breakpad {
// LongStringDictionary is a subclass of SimpleStringDictionary which supports
// longger values to be stored in the dictionary. The maximum size supported is
// (value_size - 1) * 10.
//
// Clients must avoid using the above suffixes as their key's suffix when
// LongStringDictionary is used.
class LongStringDictionary : public SimpleStringDictionary {
public:
  // Stores |value| into |key| or segment values into segment keys, replacing
  // the existing value if |key| is already present and replacing the existing
  // segment values if segment keys are already present.
  //
  // |key| must not be NULL. No mater wheter the |value| will be divided into
  // segments, the lengh of |key| must smaller than (value_size -
  // kMaxSuffixLength - 1).
  //
  // If |value| is NULL, the key and its corresponding segment keys are removed
  // from the map. If there is no more space in the map, then the operation
  // silently fails.
  void SetKeyValue(const char *key, const char *value) {
    assert(key);
    if (!key)
      return;

    RemoveKey(key);

    if (!value) {
      return;
    }

    // Key must not be an empty string.
    assert(key[0] != '\0');
    if (key[0] == '\0')
      return;

    // Either the key or the value is not valid for segmentation, forwards the
    // key and the value to SetKeyValue of SimpleStringDictionary and returns.
    if (!ValidValueForSegment(value)) {
      SimpleStringDictionary::SetKeyValue(key, value);
      return;
    }

    char segment_key[key_size];
    char segment_value[value_size];

    strcpy(segment_key, key);

    const char *remain_value = value;
    size_t remain_value_length = strlen(value);

    size_t key_length = strlen(key);
    assert(key_length + kMaxSuffixLength <= (key_size - 1));

    for (int i = 0; i < arraysize(kSuffixes); i++) {
      if (remain_value_length <= 0) {
        return;
      }

      strcpy(segment_key + key_length, kSuffixes[i]);

      size_t segment_value_length =
          std::min(remain_value_length, value_size - 1);

      strncpy(segment_value, remain_value, segment_value_length);
      segment_value[segment_value_length] = '\0';

      remain_value += segment_value_length;
      remain_value_length -= segment_value_length;

      SimpleStringDictionary::SetKeyValue(segment_key, segment_value);
    }
  }

  // Given |key|, removes any associated value or associated segment values.
  // |key| must not be NULL. If the key is not found, searchs its segment keys
  // and removes corresponding segment values if found.
  bool RemoveKey(const char *key) {
    assert(key);
    if (!key)
      return false;

    if (SimpleStringDictionary::RemoveKey(key)) {
      return true;
    };

    size_t key_length = strlen(key);
    assert(key_length + kMaxSuffixLength <= (key_size - 1));

    static char segment_key[key_size];
    strcpy(segment_key, key);

    for (int i = 0; i < arraysize(kSuffixes); i++) {
      segment_key[key_length] = '\0';
      strcat(segment_key, kSuffixes[i]);

      if (!SimpleStringDictionary::RemoveKey(segment_key)) {
        return i != 0;
      }
    }
    return true;
  }

  // Given |key|, returns its corresponding |value|. |key| must not be NULL. If
  // the key is found, its corresponding |value| is returned.
  //
  // If no corresponding |value| is found, segment keys of the given |key| will
  // be used to search for corresponding segment values. If segment values
  // exist, assembled value from them is returned. If no segment value exists,
  // NULL is returned.
  const char *GetValueForKey(const char *key) const {
    assert(key);
    if (!key)
      return NULL;

    // Key must not be an empty string.
    assert(key[0] != '\0');
    if (key[0] == '\0')
      return NULL;

    const char *value = SimpleStringDictionary::GetValueForKey(key);
    if (value)
      return value;

    size_t key_length = strlen(key);
    assert(key_length + kMaxSuffixLength <= (key_size - 1));

    bool found_segment = false;
    char segment_key[key_size];
    static char return_value[(value_size - 1) * arraysize(kSuffixes) + 1];

    return_value[0] = '\0';

    strcpy(segment_key, key);
    for (int i = 0; i < arraysize(kSuffixes); i++) {
      segment_key[key_length] = '\0';
      strcat(segment_key, kSuffixes[i]);

      const char *segment_value =
          SimpleStringDictionary::GetValueForKey(segment_key);

      if (segment_value != NULL) {
        found_segment = true;
        strcat(return_value, segment_value);
      } else {
        break;
      }
    }

    if (found_segment) {
      return return_value;
    }
    return NULL;
  }

private:
  // The maximum suffix string length.
  static const size_t kMaxSuffixLength = 4;

  // If the value is smaller than (value_size - 1) or larger than
  // (value_size - 1) * arraysize(kSuffixes), it doesn't need to (or can't) be
  // stored in several segments.
  bool ValidValueForSegment(const char *value) const {
    size_t value_length = strlen(value);
    if (value_length <= (value_size - 1) ||
        value_length > (value_size - 1) * arraysize(kSuffixes)) {
      return false;
    }
    return true;
  }
};
} // namespace google_breakpad

#endif // COMMON_LONG_STRING_DICTIONARY_H_
