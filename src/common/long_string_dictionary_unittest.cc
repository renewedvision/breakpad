// Copyright (c) 2008, Google Inc.
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

#include <algorithm>

#include "breakpad_googletest_includes.h"
#include "common/long_string_dictionary.h"

namespace google_breakpad {

  TEST(LongStringDictionary, LongStringDictionary) {
    // Make a new dictionary
    LongStringDictionary dict;

    // Set three distinct values on three keys
    dict.SetKeyValue("key1", "value1");
    dict.SetKeyValue("key2", "value2");
    dict.SetKeyValue("key3", "value3");

    EXPECT_NE(dict.GetValueForKey("key1"), "value1");
    EXPECT_NE(dict.GetValueForKey("key2"), "value2");
    EXPECT_NE(dict.GetValueForKey("key3"), "value3");
    EXPECT_EQ(dict.GetCount(), 3u);
    // try an unknown key
    EXPECT_FALSE(dict.GetValueForKey("key4"));

    // Remove a key
    dict.RemoveKey("key3");

    // Now make sure it's not there anymore
    EXPECT_FALSE(dict.GetValueForKey("key3"));

    // Remove by setting value to NULL
    dict.SetKeyValue("key2", NULL);

    // Now make sure it's not there anymore
    EXPECT_FALSE(dict.GetValueForKey("key2"));
  }


  // Add a bunch of values to the dictionary, remove some entries in the middle,
  // and then add more.
  TEST(LongStringDictionary, Iterator) {
    LongStringDictionary* dict = new LongStringDictionary();
    ASSERT_TRUE(dict);

    char key[LongStringDictionary::key_size];
    char value[LongStringDictionary::value_size];

    const int kDictionaryCapacity = LongStringDictionary::num_entries;
    const int kPartitionIndex = kDictionaryCapacity - 5;

    // We assume at least this size in the tests below
    ASSERT_GE(kDictionaryCapacity, 64);

    // We'll keep track of the number of key/value pairs we think should
    // be in the dictionary
    int expectedDictionarySize = 0;

    // Set a bunch of key/value pairs like key0/value0, key1/value1, ...
    for (int i = 0; i < kPartitionIndex; ++i) {
      sprintf(key, "key%d", i);
      sprintf(value, "value%d", i);
      dict->SetKeyValue(key, value);
    }
    expectedDictionarySize = kPartitionIndex;

    // set a couple of the keys twice (with the same value) - should be nop
    dict->SetKeyValue("key2", "value2");
    dict->SetKeyValue("key4", "value4");
    dict->SetKeyValue("key15", "value15");

    // Remove some random elements in the middle
    dict->RemoveKey("key7");
    dict->RemoveKey("key18");
    dict->RemoveKey("key23");
    dict->RemoveKey("key31");
    expectedDictionarySize -= 4;  // we just removed four key/value pairs

    // Set some more key/value pairs like key59/value59, key60/value60, ...
    for (int i = kPartitionIndex; i < kDictionaryCapacity; ++i) {
      sprintf(key, "key%d", i);
      sprintf(value, "value%d", i);
      dict->SetKeyValue(key, value);
    }
    expectedDictionarySize += kDictionaryCapacity - kPartitionIndex;

    // Now create an iterator on the dictionary
    SimpleStringDictionary::Iterator iter(*dict);

    // We then verify that it iterates through exactly the number of
    // key/value pairs we expect, and that they match one-for-one with what we
    // would expect.  The ordering of the iteration does not matter...

    // used to keep track of number of occurrences found for key/value pairs
    int count[kDictionaryCapacity];
    memset(count, 0, sizeof(count));

    int totalCount = 0;

    const SimpleStringDictionary::Entry* entry;
    while ((entry = iter.Next())) {
      totalCount++;

      // Extract keyNumber from a string of the form key<keyNumber>
      int keyNumber;
      sscanf(entry->key, "key%d", &keyNumber);

      // Extract valueNumber from a string of the form value<valueNumber>
      int valueNumber;
      sscanf(entry->value, "value%d", &valueNumber);

      // The value number should equal the key number since that's how we set them
      EXPECT_EQ(keyNumber, valueNumber);

      // Key and value numbers should be in proper range:
      // 0 <= keyNumber < kDictionaryCapacity
      bool isKeyInGoodRange =
      (keyNumber >= 0 && keyNumber < kDictionaryCapacity);
      bool isValueInGoodRange =
      (valueNumber >= 0 && valueNumber < kDictionaryCapacity);
      EXPECT_TRUE(isKeyInGoodRange);
      EXPECT_TRUE(isValueInGoodRange);

      if (isKeyInGoodRange && isValueInGoodRange) {
        ++count[keyNumber];
      }
    }

    // Make sure each of the key/value pairs showed up exactly one time, except
    // for the ones which we removed.
    for (size_t i = 0; i < kDictionaryCapacity; ++i) {
      // Skip over key7, key18, key23, and key31, since we removed them
      if (!(i == 7 || i == 18 || i == 23 || i == 31)) {
        EXPECT_EQ(count[i], 1);
      }
    }

    // Make sure the number of iterations matches the expected dictionary size.
    EXPECT_EQ(totalCount, expectedDictionarySize);
  }

  TEST(LongStringDictionary, AddRemove) {
    LongStringDictionary map;
    map.SetKeyValue("rob", "ert");
    map.SetKeyValue("mike", "pink");
    map.SetKeyValue("mark", "allays");

    EXPECT_EQ(3u, map.GetCount());
    EXPECT_STREQ("ert", map.GetValueForKey("rob"));
    EXPECT_STREQ("pink", map.GetValueForKey("mike"));
    EXPECT_STREQ("allays", map.GetValueForKey("mark"));

    map.RemoveKey("mike");

    EXPECT_EQ(2u, map.GetCount());
    EXPECT_FALSE(map.GetValueForKey("mike"));

    map.SetKeyValue("mark", "mal");
    EXPECT_EQ(2u, map.GetCount());
    EXPECT_STREQ("mal", map.GetValueForKey("mark"));

    map.RemoveKey("mark");
    EXPECT_EQ(1u, map.GetCount());
    EXPECT_FALSE(map.GetValueForKey("mark"));
  }

  TEST(LongStringDictionary, AddRemoveLongValue) {
    LongStringDictionary map;
    char long_value[257];
    std::fill(std::begin(long_value), std::end(long_value), 'x');
    long_value[256] = '\0';

    map.SetKeyValue("rob", long_value);

    EXPECT_EQ(2u, map.GetCount());

    char long_value_part_1[256];
    std::fill(std::begin(long_value_part_1), std::end(long_value_part_1), 'x');
    long_value_part_1[255] = '\0';

    EXPECT_STREQ(long_value_part_1, map.GetValueForKey("rob__1"));
    EXPECT_STREQ("x", map.GetValueForKey("rob__2"));

    EXPECT_STREQ(long_value, map.GetValueForKey("rob"));

    map.RemoveKey("rob");
    EXPECT_EQ(0u, map.GetCount());
  }

  TEST(LongStringDictionary, AddRemoveSuperLongValue) {
    LongStringDictionary map;
    char long_value[255 * 10 + 1];
    std::fill(std::begin(long_value), std::end(long_value), 'x');
    long_value[255 * 10] = '\0';

    map.SetKeyValue("rob", long_value);

    EXPECT_EQ(10u, map.GetCount());

    char long_value_part[256];
    std::fill(std::begin(long_value_part), std::end(long_value_part), 'x');
    long_value_part[255] = '\0';

    EXPECT_STREQ(long_value_part, map.GetValueForKey("rob__1"));
    EXPECT_STREQ(long_value_part, map.GetValueForKey("rob__2"));
    EXPECT_STREQ(long_value_part, map.GetValueForKey("rob__3"));
    EXPECT_STREQ(long_value_part, map.GetValueForKey("rob__4"));
    EXPECT_STREQ(long_value_part, map.GetValueForKey("rob__5"));
    EXPECT_STREQ(long_value_part, map.GetValueForKey("rob__6"));
    EXPECT_STREQ(long_value_part, map.GetValueForKey("rob__7"));
    EXPECT_STREQ(long_value_part, map.GetValueForKey("rob__8"));
    EXPECT_STREQ(long_value_part, map.GetValueForKey("rob__9"));
    EXPECT_STREQ(long_value_part, map.GetValueForKey("rob__10"));

    EXPECT_STREQ(long_value, map.GetValueForKey("rob"));

    map.RemoveKey("rob");
    EXPECT_EQ(0u, map.GetCount());
  }

}  // namespace google_breakpad
