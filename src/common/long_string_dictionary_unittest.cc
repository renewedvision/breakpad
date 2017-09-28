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

#include <algorithm>

#include <string.h>
#include <math.h>

#include "common/simple_string_dictionary.h"

namespace google_breakpad {
  static const char* kSuffixes[] =
  {"__1", "__2", "__3", "__4", "__5", "__6", "__7", "__8", "__9", "__10"};
  static const size_t kSuffixLengthes[10] = {3, 3, 3, 3, 3, 3, 3, 3, 3, 4};

  class LongStringDictionary : public SimpleStringDictionary {
  private:
    static const size_t max_segment_count = 10;
    static const size_t max_suffix_length = 4;

  public:
    void SetKeyValue(const char* key, const char* value) {
      if (!value) {
        RemoveKey(key);
        return;
      }

      assert(key);
      if (!key)
        return;

      // Key must not be an empty string.
      assert(key[0] != '\0');
      if (key[0] == '\0')
        return;

      size_t key_length = strlen(key);
      size_t value_length = strlen(value);

      // Either the key or the value is not valid for segmentation, the key and the value will be
      // forwarded to SetKeyValue of SimpleStringDictionary.
      if (!ValidKey(key) || !ValidValue(value)) {
        SimpleStringDictionary::SetKeyValue(key, value);
        return;
      }

      static char temporary_key[key_size];
      static char temporary_value[value_size];

      strcpy(temporary_key, key);

      size_t remain_value_index = 0;
      size_t remain_value_length = value_length;

      for (int i = 0; i < max_segment_count; i++) {
        temporary_key[key_length] = '\0';
        strcat(temporary_key, kSuffixes[i]);

        if (remain_value_length > value_size - 1) {
          strncpy(temporary_value, value + remain_value_index, value_size - 1);
          temporary_value[value_size - 1] = '\0';

          remain_value_index += (value_size - 1);
          remain_value_length -= (value_size - 1);
        } else if (remain_value_length > 0) {
          strncpy(temporary_value, value + remain_value_index, remain_value_length);
          temporary_value[remain_value_length] = '\0';

          remain_value_index = value_length;
          remain_value_length = 0;
        } else {
          return;
        }

        SimpleStringDictionary::SetKeyValue(temporary_key, temporary_value);
      }
    }

    void RemoveKey(const char* key) {
      SimpleStringDictionary::RemoveKey(key);

      size_t key_length = strlen(key);
      // If the key is too long, there is no need to check the ones with suffixes.
      if (key_length + max_suffix_length > (key_size - 1)) {
        return;
      }

      static char temporary_key[key_size];
      strcpy(temporary_key, key);

      for (int i = 0; i < max_segment_count; i++) {
        temporary_key[key_length] = '\0';
        strcat(temporary_key, kSuffixes[i]);

        if (GetConstEntryForKey(temporary_key) != NULL) {
          SimpleStringDictionary::RemoveKey(temporary_key);
        } else {
          break;
        }
      }
    }

    const char* GetValueForKey(const char* key) const {
      assert(key);
      if (!key)
        return NULL;

      // Key must not be an empty string.
      assert(key[0] != '\0');
      if (key[0] == '\0')
        return NULL;

      if (ValidKey(key)) {
        size_t key_length = strlen(key);

        bool found_segment = false;
        static char temporary_key[key_size];
        static char temporary_value[(value_size - 1) * max_segment_count + 1];

        temporary_value[0] = '\0';

        strcpy(temporary_key, key);
        for (int i = 0; i < max_segment_count; i++) {
          temporary_key[key_length] = '\0';
          strcat(temporary_key, kSuffixes[i]);

          const Entry* entry = GetConstEntryForKey(temporary_key);

          if (entry != NULL) {
            found_segment = true;
            strcat(temporary_value, entry->value);
          } else {
            break;
          }
        }

        if (found_segment) {
          return temporary_value;
        }
      }

      const Entry* entry = GetConstEntryForKey(key);
      if (!entry)
        return NULL;

      return entry->value;
    }

  private:
    // If the key is too long, it's not valid for segmentation.
    bool ValidKey(const char * key) const {
      size_t key_length = strlen(key);
      if (key_length + max_suffix_length > (key_size - 1)) {
        return false;
      }
      return true;
    }

    // If the value is smaller than (value_size - 1) or larger than
    // (value_size - 1) * max_segment_count, value don't need to (or can't) be stored in several
    // segments.
    bool ValidValue(const char * value) const {
      size_t value_length = strlen(value);
      if (value_length <= (value_size - 1) || value_length > (value_size - 1) * max_segment_count) {
        return false;
      }
      return true;
    }
    
  };
}  // namespace google_breakpad

#endif  // COMMON_LONG_STRING_DICTIONARY_H_
