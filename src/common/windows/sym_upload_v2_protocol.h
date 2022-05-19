#ifndef COMMON_WINDOWS_SYM_UPLOAD_V2_PROTOCOL_H_
#define COMMON_WINDOWS_SYM_UPLOAD_V2_PROTOCOL_H_

#include <string>

namespace google_breakpad {

// Send file at |symbol_filename| using the sym-upload-v2 protocol to |api_url|
// using key |api_key|, and using identifiers |debug_file| and |debug_id|.
// |timeout_ms| is the number of miliseconds we will wait before terminating
// the upload attempt. If |force| is set then we will overwrite an existing
// file with the same |debug_file| and |debug_id| in the store.
bool SymUploadV2ProtocolSend(const wchar_t* api_url,
                             const wchar_t* api_key,
                             int* timeout_ms,
                             const std::wstring& debug_file,
                             const std::wstring& debug_id,
                             const std::wstring& symbol_filename,
                             bool force);

}  // namespace google_breakpad

#endif  // COMMON_WINDOWS_SYM_UPLOAD_V2_PROTOCOL_H_