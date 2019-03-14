#include "common/windows/pe_source_line_writer.h"

#include "common/windows/pe_util.h"

namespace google_breakpad {
PESourceLineWriter::PESourceLineWriter(const wstring & pe_file) :
  pe_file_(pe_file) {
}

PESourceLineWriter::~PESourceLineWriter() {
}

bool PESourceLineWriter::WriteMap(FILE * map_file) {
  ModuleInfo module_info;
  if (!GetModuleInfo(&module_info)) {
    // TODO(nbilling): error
    return false;
  }
  // Hard-code "windows" for the OS because that's the only thing that makes
  // sense for PDB files.  (This might not be strictly correct for Windows CE
  // support, but we don't care about that at the moment.)
  fprintf(map_file, "MODULE windows %ws %ws %ws\n",
    module_info.cpu.c_str(), module_info.debug_identifier.c_str(),
    module_info.debug_file.c_str());

  PEModuleInfo pe_info;
  if (!GetPEInfo(&pe_info)) {
    // TODO(nbilling): error
    return false;
  }
  fprintf(map_file, "INFO CODE_ID %ws %ws\n",
    pe_info.code_identifier.c_str(),
    pe_info.code_file.c_str());

  if (!PrintPEFrameData(pe_file_, map_file)) {
    // TODO(nbilling): error
    return false;
  }

  return true;
}

bool PESourceLineWriter::GetModuleInfo(ModuleInfo * info) {
  return ReadModuleInfo(pe_file_, info);
}

bool PESourceLineWriter::GetPEInfo(PEModuleInfo * info) {
  return ReadPEInfo(pe_file_, info);
}

bool PESourceLineWriter::UsesGUID(bool * uses_guid) {
  return true;
}

}  // namespace google_breakpad
