// Copyright (c) 2022, Google Inc.
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

#ifndef CLIENT_LINUX_MINIDUMP_WRITER_PE_FILE_H_
#define CLIENT_LINUX_MINIDUMP_WRITER_PE_FILE_H_

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include "client/linux/minidump_writer/pe_structs.h"

namespace google_breakpad {

class PEFile {
 public:
  /**
   * Attempts to parse RSDS_DEBUG_FORMAT record from the PE (Portable
   * Executable) file. To do this we check whether the loaded file is a PE file
   * (and populates `pe` argument), and if it is - try to find
   * IMAGE_DEBUG_DIRECTORY structure with its type set to
   * IMAGE_DEBUG_TYPE_CODEVIEW.
   *
   * @param file Filestream with its current position set to 0.
   * @param pe Flag to be set if the `file` is a PE file (dll or exe).
   * @param debug_info RSDS_DEBUG_FORMAT struct to be populated with PE debug
   * info.
   * @return if the debug_info was found and successfully populated.
   */
  bool TryGetDebugInfo(std::ifstream& file,
                       bool* pe,
                       RSDS_DEBUG_FORMAT* debug_info) {
    *pe = false;
    IMAGE_DOS_HEADER dos_header;
    if (!TryReadStructFromFile<IMAGE_DOS_HEADER>(file, sizeof(IMAGE_DOS_HEADER),
                                                 &dos_header))
      return false;

    // PE file should start with `MZ` magic.
    if (dos_header.e_magic != IMAGE_DOS_SIGNATURE)
      return false;

    // NTHeader is at position 'e_lfanew'.
    DWORD ntHeaderOffset = dos_header.e_lfanew;

    // Go to NTHeader position.
    file.seekg(ntHeaderOffset);
    IMAGE_NT_HEADERS nt_header;
    if (!TryReadStructFromFile<IMAGE_NT_HEADERS>(file, sizeof(IMAGE_NT_HEADERS),
                                                 &nt_header))
      return false;

    // NTHeader should start with PE signature.
    if (nt_header.Signature != IMAGE_NT_SIGNATURE)
      return false;

    bool x64 = nt_header.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;

    *pe = (nt_header.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) ||
          (nt_header.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC);

    if (!pe)
      return false;

    WORD sections_number = nt_header.FileHeader.NumberOfSections;
    DWORD debug_offset;
    DWORD debug_size;
    file.seekg(ntHeaderOffset);
    if (x64) {
      IMAGE_NT_HEADERS64 header64;
      if (!TryReadStructFromFile<IMAGE_NT_HEADERS64>(
              file, sizeof(IMAGE_NT_HEADERS64), &header64))
        return false;

      debug_offset =
          header64.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG]
              .VirtualAddress;
      debug_size =
          header64.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG]
              .Size;
    } else {
      IMAGE_NT_HEADERS32 header32;
      if (!TryReadStructFromFile<IMAGE_NT_HEADERS32>(
              file, sizeof(IMAGE_NT_HEADERS32), &header32))
        return false;

      debug_offset =
          header32.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG]
              .VirtualAddress;
      debug_size =
          header32.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG]
              .Size;
    }

    // Get all ImageSectionHeaders.
    std::vector<IMAGE_SECTION_HEADER> sections =
        ReadSectionHeaders(file, sections_number);

    DWORD debug_end_pos = debug_offset + debug_size;
    while (debug_offset < debug_end_pos) {
      for (IMAGE_SECTION_HEADER& section : sections) {
        // Find in which ImageSectionHeader the ImageDebugDirectory is stored.
        if ((section.VirtualAddress <= debug_offset) &&
            (debug_offset < section.VirtualAddress + section.SizeOfRawData)) {
          DWORD offset =
              section.PointerToRawData + debug_offset - section.VirtualAddress;

          // Go to position of current ImageDebugDirectory.
          file.seekg(offset);
          IMAGE_DEBUG_DIRECTORY image_debug_directory;
          if (!TryReadStructFromFile<IMAGE_DEBUG_DIRECTORY>(
                  file, sizeof(IMAGE_DEBUG_DIRECTORY), &image_debug_directory))
            return false;

          // Process ImageDebugDirectory with CodeViewRecord type and skip
          // others.
          if (image_debug_directory.Type == IMAGE_DEBUG_TYPE_CODEVIEW) {
            DWORD debug_directory_size = image_debug_directory.SizeOfData;

            if (debug_directory_size < sizeof(RSDS_DEBUG_FORMAT))
              return false;
            // Go to position of current ImageDebugDirectory Raw Data.
            file.seekg(image_debug_directory.PointerToRawData);

            RSDS_DEBUG_FORMAT rsds;
            if (!TryReadStructFromFile<RSDS_DEBUG_FORMAT>(
                    file, debug_directory_size, &rsds)) {
              return false;
            }
            memcpy(debug_info->guid, rsds.guid, sizeof(rsds.guid));
            memcpy(debug_info->age, rsds.age, sizeof(rsds.age));
            return true;
          }

          break;
        }
      }

      debug_offset += sizeof(IMAGE_DEBUG_DIRECTORY);
    }

    return false;
  }

 private:
  template <class TStruct>
  bool TryReadStructFromFile(std::ifstream& file,
                             size_t size,
                             TStruct* result) {
    if (size < sizeof(TStruct))
      return false;

    file.read(reinterpret_cast<char*>(result), size);
    if (!file)
      return false;

    return true;
  }

  std::vector<IMAGE_SECTION_HEADER> ReadSectionHeaders(std::ifstream& file,
                                                       WORD sections_number) {
    std::vector<IMAGE_SECTION_HEADER> sections(sections_number);
    for (WORD i = 0; i < sections_number; ++i) {
      if (!TryReadStructFromFile<IMAGE_SECTION_HEADER>(
              file, sizeof(IMAGE_SECTION_HEADER), &sections[i]))
        return sections;
    }

    return sections;
  }
};
}  // namespace google_breakpad
#endif  // CLIENT_LINUX_MINIDUMP_WRITER_PE_FILE_H_