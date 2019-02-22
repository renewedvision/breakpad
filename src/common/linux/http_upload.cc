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

#include "common/linux/http_upload.h"

#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include "third_party/curl/curl.h"

#include <memory>

namespace {

using std::map;
using std::unique_ptr;

static const char kUserAgent[] = "Breakpad/1.0 (Linux)";

// Callback to get the response data from server.
static size_t WriteCallback(void *ptr, size_t size,
                            size_t nmemb, void *userp) {
  if (!userp)
    return 0;

  string *response = reinterpret_cast<string *>(userp);
  size_t real_size = size * nmemb;
  response->append(reinterpret_cast<char *>(ptr), real_size);
  return real_size;
}

// Checks the curl_lib parameter points to a valid curl lib.
static bool CheckCurlLib(void* curl_lib) {
  return curl_lib &&
      dlsym(curl_lib, "curl_easy_init") &&
      dlsym(curl_lib, "curl_easy_setopt");
}

// Checks that the given list of parameters has only printable
// ASCII characters in the parameter name, and does not contain
// any quote (") characters.  Returns true if so.
static bool CheckParameters(const map<string, string> &parameters) {
  for (map<string, string>::const_iterator pos = parameters.begin();
       pos != parameters.end(); ++pos) {
    const string &str = pos->first;
    if (str.size() == 0)
      return false;  // disallow empty parameter names
    for (unsigned int i = 0; i < str.size(); ++i) {
      int c = str[i];
      if (c < 32 || c == '"' || c > 127) {
        return false;
      }
    }
  }
  return true;
}

class FormPost {
public:
  FormPost(
    void* curl_lib,
    const map<string, string> &parameters,
    const map<string, string> &files) :
    curl_lib_(curl_lib),
    formpost_(NULL) {
    struct curl_httppost *lastptr = NULL;
    // Add form data.
    CURLFORMcode (*curl_formadd)(
        struct curl_httppost **,
        struct curl_httppost **,
        ...);
    *(void**) (&curl_formadd) = dlsym(curl_lib, "curl_formadd");
    map<string, string>::const_iterator iter = parameters.begin();
    for (; iter != parameters.end(); ++iter)
      (*curl_formadd)(&formpost_, &lastptr,
                    CURLFORM_COPYNAME, iter->first.c_str(),
                    CURLFORM_COPYCONTENTS, iter->second.c_str(),
                    CURLFORM_END);

    // Add form files.
    for (iter = files.begin(); iter != files.end(); ++iter) {
      (*curl_formadd)(&formpost_, &lastptr,
                    CURLFORM_COPYNAME, iter->first.c_str(),
                    CURLFORM_FILE, iter->second.c_str(),
                    CURLFORM_END);
    }
  }

  ~FormPost() {
    if (formpost_ != NULL && curl_lib_ != NULL) {
      void (*curl_formfree)(struct curl_httppost *);
      *(void**) (&curl_formfree) = dlsym(curl_lib_, "curl_formfree");
      (*curl_formfree)(formpost_);
    }
  }

  struct curl_httppost* get() {
    return formpost_;
  }

private:
  void* curl_lib_;
  struct curl_httppost* formpost_;
};

class AutoFileCloser {
 public:
  explicit AutoFileCloser(FILE* file) : file_(file) {}
  ~AutoFileCloser() {
    if (file_)
      fclose(file_);
  }

  FILE* get() {
    return file_;
  }

 private:
  FILE* file_;
};

enum HttpMethod {
  Simple_POST,
  Multipart_POST,
  PUT,
  GET
};

struct PutOptions {
  const string& path;
};

struct MultipartPostOptions {
  const map<string, string>& parameters;
  const map<string, string>& files;
};

struct SimplePostOptions {
  const string& body;
};

struct RequestOptions {
  HttpMethod http_method;
  const string& url;
  const string& proxy;
  const string& proxy_user_pwd;
  const string& ca_certificate_file;
  union {
    PutOptions* put_options;
    SimplePostOptions* simple_post_options;
    MultipartPostOptions* multipart_post_options;
    // This will be nullptr if GET request.
  };
  const string& content_type;
};

// static
static bool SendRequestInner(const RequestOptions& request_options,
                                  string *response_body,
                                  long *response_code,
                                  string *error_description) {
  if (response_code != NULL)
    *response_code = 0;

  // We may have been linked statically; if curl_easy_init is in the
  // current binary, no need to search for a dynamic version.
  void* curl_lib = dlopen(NULL, RTLD_NOW);
  if (!CheckCurlLib(curl_lib)) {
    dlerror();  // Clear dlerror before attempting to open libraries.
    dlclose(curl_lib);
    curl_lib = NULL;
  }
  if (!curl_lib) {
    curl_lib = dlopen("libcurl.so", RTLD_NOW);
  }
  if (!curl_lib) {
    if (error_description != NULL)
      *error_description = dlerror();
    curl_lib = dlopen("libcurl.so.4", RTLD_NOW);
  }
  if (!curl_lib) {
    // Debian gives libcurl a different name when it is built against GnuTLS
    // instead of OpenSSL.
    curl_lib = dlopen("libcurl-gnutls.so.4", RTLD_NOW);
  }
  if (!curl_lib) {
    curl_lib = dlopen("libcurl.so.3", RTLD_NOW);
  }
  if (!curl_lib) {
    return false;
  }

  CURLcode err_code = CURLE_OK;
  {  // Block for lifecycle of resources that depend on curl_lib.
    CURL* (*curl_easy_init)(void);
    *(void**) (&curl_easy_init) = dlsym(curl_lib, "curl_easy_init");
    CURL *curl = (*curl_easy_init)();
    if (error_description != NULL)
      *error_description = "No Error";

    if (!curl) {
      dlclose(curl_lib);
      return false;
    }

    CURLcode (*curl_easy_setopt)(CURL *, CURLoption, ...);
    *(void**) (&curl_easy_setopt) = dlsym(curl_lib, "curl_easy_setopt");
    (*curl_easy_setopt)(curl, CURLOPT_URL, request_options.url.c_str());
    (*curl_easy_setopt)(curl, CURLOPT_USERAGENT, kUserAgent);
    // Support multithread by disabling timeout handling, would get SIGSEGV
    // with Curl_resolv_timeout in stack trace otherwise.
    // See https://curl.haxx.se/libcurl/c/threadsafe.html
    (*curl_easy_setopt)(curl, CURLOPT_NOSIGNAL, 1);
    // Set proxy information if necessary.
    if (!request_options.proxy.empty())
      (*curl_easy_setopt)(
          curl,
          CURLOPT_PROXY,
          request_options.proxy.c_str());
    if (!request_options.proxy_user_pwd.empty())
      (*curl_easy_setopt)(
          curl,
          CURLOPT_PROXYUSERPWD,
          request_options.proxy_user_pwd.c_str());

    if (!request_options.ca_certificate_file.empty())
      (*curl_easy_setopt)(
          curl,
          CURLOPT_CAINFO,
          request_options.ca_certificate_file.c_str());

    unique_ptr<FormPost> form_post(nullptr);
    unique_ptr<AutoFileCloser> put_file_closer(nullptr);

    if (request_options.http_method == HttpMethod::Multipart_POST) {
      if (!CheckParameters(
        request_options.multipart_post_options->parameters)) {
        return false;
      }

      form_post.reset(new FormPost(
        curl_lib,
        request_options.multipart_post_options->parameters,
        request_options.multipart_post_options->files));
      (*curl_easy_setopt)(curl, CURLOPT_HTTPPOST, form_post->get());
    } else if (request_options.http_method == HttpMethod::Simple_POST) {
      const string& body = request_options.simple_post_options->body;
      (*curl_easy_setopt)(curl, CURLOPT_POSTFIELDSIZE, body.size());
      (*curl_easy_setopt)(curl, CURLOPT_COPYPOSTFIELDS, body.c_str());

    } else if (request_options.http_method == HttpMethod::PUT) {
      put_file_closer.reset(new AutoFileCloser(
        fopen(
          request_options.put_options->path.c_str(),
          "rb")));
      (*curl_easy_setopt)(curl, CURLOPT_UPLOAD, 1L);
      (*curl_easy_setopt)(curl, CURLOPT_PUT, 1L);
      (*curl_easy_setopt)(curl, CURLOPT_READDATA, put_file_closer->get());
    }
    else if (request_options.http_method == HttpMethod::GET) {
      (*curl_easy_setopt)(curl, CURLOPT_HTTPGET, 1L);
    }

    struct curl_slist *headerlist = NULL;
    struct curl_slist* (*curl_slist_append)(struct curl_slist *, const char *);
    *(void**) (&curl_slist_append) = dlsym(curl_lib, "curl_slist_append");

    // Disable 100-continue header.
    char expect_header[] = "Expect:";
    headerlist = (*curl_slist_append)(headerlist, expect_header);

    if (!request_options.content_type.empty()) {
      string content_type_header = "Content-Type: " +
        request_options.content_type;
      headerlist = (*curl_slist_append)(
          headerlist,
          content_type_header.c_str());
    }

    // Append headers
    (*curl_easy_setopt)(curl, CURLOPT_HTTPHEADER, headerlist);

    if (response_body != NULL) {
      (*curl_easy_setopt)(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
      (*curl_easy_setopt)(curl, CURLOPT_WRITEDATA,
                      reinterpret_cast<void *>(response_body));
    }

    // Note: this makes debugging difficult because the response body won't be
    // copied into the response buffer. For debugging you can safely disable
    // this in test builds.
    // Fail if 400+ is returned from the web server.
    (*curl_easy_setopt)(curl, CURLOPT_FAILONERROR, 1);

    CURLcode (*curl_easy_perform)(CURL *);
    *(void**) (&curl_easy_perform) = dlsym(curl_lib, "curl_easy_perform");
    err_code = (*curl_easy_perform)(curl);
    if (response_code != NULL) {
      CURLcode (*curl_easy_getinfo)(CURL *, CURLINFO, ...);
      *(void**) (&curl_easy_getinfo) = dlsym(curl_lib, "curl_easy_getinfo");
      (*curl_easy_getinfo)(curl, CURLINFO_RESPONSE_CODE, response_code);
    }
    const char* (*curl_easy_strerror)(CURLcode);
    *(void**) (&curl_easy_strerror) = dlsym(curl_lib, "curl_easy_strerror");
  #ifndef NDEBUG
    if (err_code != CURLE_OK)
      fprintf(stderr, "Failed to send http request to %s, error: %s\n",
              request_options.url.c_str(),
              (*curl_easy_strerror)(err_code));
  #endif
    if (error_description != NULL)
      *error_description = (*curl_easy_strerror)(err_code);

    void (*curl_easy_cleanup)(CURL *);
    *(void**) (&curl_easy_cleanup) = dlsym(curl_lib, "curl_easy_cleanup");
    (*curl_easy_cleanup)(curl);

    if (headerlist != NULL) {
      void (*curl_slist_free_all)(struct curl_slist *);
      *(void**) (&curl_slist_free_all) = dlsym(
          curl_lib,
          "curl_slist_free_all");
      (*curl_slist_free_all)(headerlist);
    }
  }  // End of block for curl_lib.

  dlclose(curl_lib);
  return err_code == CURLE_OK;
}

}  // namespace

namespace google_breakpad {

// static
bool HTTPUpload::SendPutRequest(const string &url,
                                const string &path,
                                const string &proxy,
                                const string &proxy_user_pwd,
                                const string &ca_certificate_file,
                                string *response_body,
                                long *response_code,
                                string *error_description) {
  PutOptions put_options = {path};
  RequestOptions request_options = {
    HttpMethod::PUT,
    url,
    proxy,
    proxy_user_pwd,
    ca_certificate_file,
    { nullptr },
    ""
  };
  request_options.put_options = &put_options;

  return SendRequestInner(
      request_options,
      response_body,
      response_code,
      error_description);
}

// static
bool HTTPUpload::SendGetRequest(const string &url,
                                const string &proxy,
                                const string &proxy_user_pwd,
                                const string &ca_certificate_file,
                                string *response_body,
                                long *response_code,
                                string *error_description) {
  RequestOptions request_options = {
    HttpMethod::GET,
    url,
    proxy,
    proxy_user_pwd,
    ca_certificate_file,
    { nullptr },
    ""
  };

  return SendRequestInner(
      request_options,
      response_body,
      response_code,
      error_description);
}

// static
bool HTTPUpload::SendMultipartPostRequest(
    const string &url,
    const map<string, string> &parameters,
    const map<string, string> &files,
    const string &proxy,
    const string &proxy_user_pwd,
    const string &ca_certificate_file,
    const string &content_type,
    string *response_body,
    long *response_code,
    string *error_description) {
  MultipartPostOptions multipart_post_options = { parameters, files };
  RequestOptions request_options = {
    HttpMethod::Multipart_POST,
    url,
    proxy,
    proxy_user_pwd,
    ca_certificate_file,
    { nullptr },
    content_type
  };

  request_options.multipart_post_options = &multipart_post_options;

  return SendRequestInner(
      request_options,
      response_body,
      response_code,
      error_description);
}

// static
bool HTTPUpload::SendSimplePostRequest(
    const string &url,
    const string &body,
    const string &proxy,
    const string &proxy_user_pwd,
    const string &ca_certificate_file,
    const string &content_type,
    string *response_body,
    long *response_code,
    string *error_description) {
    SimplePostOptions simple_post_options = { body };
  RequestOptions request_options = {
    HttpMethod::Simple_POST,
    url,
    proxy,
    proxy_user_pwd,
    ca_certificate_file,
    { nullptr },
    content_type
  };
  request_options.simple_post_options = &simple_post_options;

  return SendRequestInner(
      request_options,
      response_body,
      response_code,
      error_description);
}

}  // namespace google_breakpad
