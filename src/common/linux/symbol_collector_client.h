#ifndef THIRD_PARTY_BREAKPAD_SRC_COMMON_LINUX_SYMBOL_COLLECTOR_CLIENT_H_
#define THIRD_PARTY_BREAKPAD_SRC_COMMON_LINUX_SYMBOL_COLLECTOR_CLIENT_H_

#include <string>

#include "common/using_std_string.h"
#include "third_party/curl/curl.h"

namespace google_breakpad {
namespace sym_upload {

struct UploadUrlResponse {
    string upload_url;
    string upload_key;
};

enum SymbolStatus {
    Found,
    Missing,
    Unknown
};

enum CompleteUploadResult {
    Ok,
    DuplicateData,
    Error
};

class SymbolCollectorClient {
 public:
  static bool CreateUploadUrl(
      string &api_url,
      string &api_key,
      UploadUrlResponse *uploadUrlResponse);

  static CompleteUploadResult CompleteUpload(
      string &api_url,
      string &api_key,
      const string& upload_key,
      const string& debug_file,
      const string& debug_id);

  static SymbolStatus CheckSymbolStatus(
      string &api_url,
      string &api_key,
      const string& debug_file,
      const string& debug_id);
};

}  // namespace sym_upload
}  // namespace google_breakpad

#endif  // THIRD_PARTY_BREAKPAD_SRC_COMMON_LINUX_SYMBOL_COLLECTOR_CLIENT_H_
