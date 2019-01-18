#include "common/linux/symbol_collector_client.h"

#include "common/linux/http_upload.h"
#include <regex>

namespace google_breakpad {
namespace sym_upload {

// static
bool SymbolCollectorClient::CreateUploadUrl(
    string &api_url,
    string &api_key,
    UploadUrlResponse *uploadUrlResponse) {
  string response, error;
  long response_code;

  string url = api_url +
               "/v1/uploads:create"
               "?key=" + api_key;

  if (!HTTPUpload::SendSimplePostRequest(url,
                                   "",
                                   "",
                                   "",
                                   "",
                                   "",
                                   &response,
                                   &response_code,
                                   &error)) {
    printf("Failed to create upload url, error message: %s\n", error.c_str());
    printf("Response code: %ld\n", response_code);
    printf("Response:\n");
    printf("%s\n", response.c_str());
    return false;
  }

  // Note that these are camel-case in REST api!!!
  // TODO(nbilling): consider using a json or proto parser instead of regex
  std::regex upload_url_regex("\"uploadUrl\": \"([^\"]+)\"");
  std::regex upload_key_regex("\"uploadKey\": \"([^\"]+)\"");

  std::smatch upload_url_match;
  if (!std::regex_search(response, upload_url_match, upload_url_regex) ||
      upload_url_match.size() != 2) {
      printf("Failed to parse create url response.");
      printf("Response:\n");
      printf("%s\n", response.c_str());
      return false;
  }
  string upload_url = upload_url_match[1].str();

  std::smatch upload_key_match;
  if (!std::regex_search(response, upload_key_match, upload_key_regex) ||
      upload_key_match.size() != 2) {
      printf("Failed to parse create url response.");
      printf("Response:\n");
      printf("%s\n", response.c_str());
      return false;
  }
  string upload_key = upload_key_match[1].str();

  uploadUrlResponse->upload_url = upload_url;
  uploadUrlResponse->upload_key = upload_key;
  return true;
}

// static
CompleteUploadResult SymbolCollectorClient::CompleteUpload(
    string &api_url,
    string &api_key,
    const string& upload_key,
    const string& debug_file,
    const string& debug_id) {
  string response, error;
  long response_code;

  string url = api_url +
               "/v1/uploads/" + upload_key + ":complete"
               "?key=" + api_key;
  // TODO(nbilling): consider JSON serializer lib?
  string body =
    "{ "
    "debug_file: \"" + debug_file + "\", "
    "debug_id: \"" + debug_id + "\" "
    "}";

  if (!HTTPUpload::SendSimplePostRequest(
      url,
      body,
      "",
      "",
      "",
      "application/json",
      &response,
      &response_code,
      &error)) {
    printf("Failed to complete upload, error message: %s\n", error.c_str());
    printf("Response code: %ld\n", response_code);
    printf("Response:\n");
    printf("%s\n", response.c_str());
    return CompleteUploadResult::Error;
  }

// TODO(nbilling): consider using a json or proto parser instead of regex
  std::regex result_regex("\"result\": \"([^\"]+)\"");
  std::smatch result_match;
  if (!std::regex_search(response, result_match, result_regex) ||
      result_match.size() != 2) {
      printf("Failed to parse complete upload response.");
      printf("Response:\n");
      printf("%s\n", response.c_str());
      return CompleteUploadResult::Error;
  }
  string result = result_match[1].str();

  if (result.compare("DUPLICATE_DATA") == 0) {
      return CompleteUploadResult::DuplicateData;
  }

  return CompleteUploadResult::Ok;
}

// static
SymbolStatus SymbolCollectorClient::CheckSymbolStatus(
    string &api_url,
    string &api_key,
    const string& debug_file,
    const string& debug_id) {
  string response, error;
  long response_code;
  string url = api_url +
               "/v1/symbols/" + debug_file + "/" + debug_id + ":check_status"
               "?key=" + api_key;
  
  if (!HTTPUpload::SendGetRequest(
      url,
      "",
      "",
      "",
      &response,
      &response_code,
      &error)) {
    printf("Failed to check symbol status, error message: %s\n", error.c_str());
    printf("Response code: %ld\n", response_code);
    printf("Response:\n");
    printf("%s\n", response.c_str());
    return SymbolStatus::Unknown;
  }

  // TODO(nbilling): consider using a json or proto parser instead of regex
  std::regex status_regex("\"status\": \"([^\"]+)\"");
  std::smatch status_match;
  if (!std::regex_search(response, status_match, status_regex) ||
      status_match.size() != 2) {
      printf("Failed to parse check symbol status response.");
      printf("Response:\n");
      printf("%s\n", response.c_str());
      return SymbolStatus::Unknown;
  }
  string status = status_match[1].str();

  return (status.compare("FOUND") == 0) ?
    SymbolStatus::Found :
    SymbolStatus::Missing;
}

}  // namespace sym_upload
}  // namespace google_breakpad
