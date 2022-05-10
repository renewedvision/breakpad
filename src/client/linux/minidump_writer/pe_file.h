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
#include "common/linux/memory_mapped_file.h"

namespace google_breakpad {

typedef enum {
	notPeCoff = 0,
	peWithoutBuildId = 1,
  peWithBuildId = 2
} PEFileFormat;

class PEFile {
 public:
  /**
   * Attempts to parse RSDS_DEBUG_FORMAT record from a PE (Portable
   * Executable) file. To do this we check whether the loaded file is a PE
   * file, and if it is - try to find IMAGE_DEBUG_DIRECTORY structure with
   * its type set to IMAGE_DEBUG_TYPE_CODEVIEW.
   *
   * @param filename Filename for the module to parse.
   * @param debug_info RSDS_DEBUG_FORMAT struct to be populated with PE debug
   * info (GUID and age).
   * @return
   *   notPeCoff: not PE/COFF file;
   *   peWithoutBuildId: a PE/COFF file but build-id is not set;
   *   peWithBuildId: a PE/COFF file and build-id is set.
   */

  static PEFileFormat TryGetDebugInfo(const char* filename,
                                      PRSDS_DEBUG_FORMAT debug_info) {
    PEFileFormat result = PEFileFormat::notPeCoff;
    MemoryMappedFile mapped_file(filename, 0);
    if (!mapped_file.data())
      return result;
    const void* base = mapped_file.data();
    const size_t file_size = mapped_file.size();

    const IMAGE_DOS_HEADER* header =
        TryReadStruct<IMAGE_DOS_HEADER>(base, 0, file_size);
    if (!header || (header->e_magic != IMAGE_DOS_SIGNATURE)) {
      return result;
    }

    // NTHeader is at position 'e_lfanew'.
    DWORD nt_header_offset = header->e_lfanew;
    // First, read a common IMAGE_NT_HEADERS structure. It should contain a
    // special flag marking whether PE module is 64x (OptionalHeader.Magic)
    // and so-called NT_SIGNATURE in Signature field.
    const IMAGE_NT_HEADERS* nt_header =
        TryReadStruct<IMAGE_NT_HEADERS>(base, nt_header_offset, file_size);
    if (!nt_header || (nt_header->Signature != IMAGE_NT_SIGNATURE)
       || ((nt_header->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
       &&  (nt_header->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)))
      return result;

    result = PEFileFormat::peWithoutBuildId;
    bool x64 = nt_header->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    WORD sections_number = nt_header->FileHeader.NumberOfSections;
    DWORD debug_offset;
    DWORD debug_size;
    DWORD section_offset;
    if (x64) {
      const IMAGE_NT_HEADERS64* header_64 =
          TryReadStruct<IMAGE_NT_HEADERS64>(base, nt_header_offset, file_size);
      if (!header_64)
        return result;
      debug_offset =
          header_64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG]
              .VirtualAddress;
      debug_size =
          header_64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG]
              .Size;
      section_offset = nt_header_offset + sizeof(IMAGE_NT_HEADERS64);
    } else {
      const IMAGE_NT_HEADERS32* header_32 =
          TryReadStruct<IMAGE_NT_HEADERS32>(base, nt_header_offset, file_size);
      if (!header_32)
        return result;
      debug_offset =
          header_32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG]
              .VirtualAddress;
      debug_size =
          header_32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG]
              .Size;
      section_offset = nt_header_offset + sizeof(IMAGE_NT_HEADERS32);
    }

    DWORD debug_end_pos = debug_offset + debug_size;
    while (debug_offset < debug_end_pos) {
      for (WORD i = 0; i < sections_number; ++i) {
        // Section headers are placed sequentially after the NT_HEADER (32/64).
        const IMAGE_SECTION_HEADER* section =
            TryReadStruct<IMAGE_SECTION_HEADER>(base, section_offset, file_size);
        section_offset += sizeof(IMAGE_SECTION_HEADER);
        if (!section)
          return result;

        // Current `debug_offset` should be inside a section, stop if we find
        // a suitable one (we don't consider any malformed sections here).
        if ((section->VirtualAddress <= debug_offset) &&
            (debug_offset < section->VirtualAddress + section->SizeOfRawData)) {
          DWORD offset =
              section->PointerToRawData + debug_offset - section->VirtualAddress;
          // Go to the position of current ImageDebugDirectory (offset).
          const IMAGE_DEBUG_DIRECTORY* debug_directory =
              TryReadStruct<IMAGE_DEBUG_DIRECTORY>(base, offset, file_size);
          if (!debug_directory)
            return result;
          // Process ImageDebugDirectory with CodeViewRecord type and skip
          // all others.
          if (debug_directory->Type == IMAGE_DEBUG_TYPE_CODEVIEW) {
            DWORD debug_directory_size = debug_directory->SizeOfData;
            if (debug_directory_size < sizeof(RSDS_DEBUG_FORMAT))
              // RSDS section is malformed.
              return result;
            // Go to the position of current ImageDebugDirectory Raw Data
            // (debug_directory->PointerToRawData) and read the RSDS section.
            const RSDS_DEBUG_FORMAT* rsds =
                TryReadStruct<RSDS_DEBUG_FORMAT>(
                  base, debug_directory->PointerToRawData, file_size);

            if (!rsds)
              return result;

            memcpy(debug_info->guid, rsds->guid, sizeof(rsds->guid));
            memcpy(debug_info->age, rsds->age, sizeof(rsds->age));
            result = PEFileFormat::peWithBuildId;
            return result;
          }

          break;
        }
      }

      debug_offset += sizeof(IMAGE_DEBUG_DIRECTORY);
    }

    return result;
  }

 private:
  template <class TStruct>
  static const TStruct* TryReadStruct(const void* base,
                                      const DWORD position,
                                      const size_t file_size) {
    if (position + sizeof(TStruct) >= file_size){
      return nullptr;
    }

    const void* ptr = static_cast<const char*>(base) + position;
    return const_cast<TStruct*>(reinterpret_cast<const TStruct*>(ptr));
  }
};
}  // namespace google_breakpad
#endif  // CLIENT_LINUX_MINIDUMP_WRITER_PE_FILE_H_