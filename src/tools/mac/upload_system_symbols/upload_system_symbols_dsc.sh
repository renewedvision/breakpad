#!/bin/bash
# Copyright 2023 Google LLC
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google LLC nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Finds the dyld_shared_cache on a system, extracts it, and dumps the symbols
# in Breakpad format. Must be run from the same directory as prebuilt
# `dump_syms`, `upload_system_symbols` and `dsc_extractor` binaries.
# Exits with 0 if all supported architectures for this version were found and
# dumped, and 1 otherwise.

set -ex
dir="$(dirname $0)"
dir="$(cd "${dir}"; pwd)"
major_version=$(sw_vers -productVersion | cut -d . -f 1)
if [[ "${major_version}" -lt 13 ]]; then
  dsc_directory="/System/Library/dyld"
else
  dsc_directory="/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld"
fi
version="$(sw_vers -productVersion).$(sw_vers -buildVersion)"

WORKING_DIR=$(mktemp -d)
trap 'rm -rf ${WORKING_DIR}' exit
mkdir "${WORKING_DIR}/dsc.${version}" "${WORKING_DIR}/breakpad.${version}"

architectures=(x86_64h)
missing_architectures=()
if [[ "${major_version}" -lt 13 ]]; then
  architectures+=( x86_64 )
fi
if [[ "${major_version}" -ge 11 ]]; then
  architectures+=( arm64e )
fi
status=0

for arch in "${architectures[@]}"; do
  cache="${dsc_directory}/dyld_shared_cache_${arch}"
  if [[ ! -f "${cache}" ]]; then
    status=1
    missing_architectures+=("${arch}")
    continue
  fi
  "${dir}/dsc_extractor" \
      "${cache}" \
      "${WORKING_DIR}/dsc.${version}/${arch}"
  mkdir -p "${WORKING_DIR}/dsc.${version}/${arch}/System/Library/Components"
  "${dir}/upload_system_symbols" \
      --breakpad-tools="${dir}" \
      --system-root="${WORKING_DIR}/dsc.${version}/${arch}" \
      --dump-to="${WORKING_DIR}/breakpad.${version}"
done
"${dir}/upload_system_symbols" \
    --breakpad-tools="${dir}" \
    --system-root=/ \
    --dump-to="${WORKING_DIR}/breakpad.${version}"
mkdir "${HOME}/Desktop/breakpad.${version}"    
cp -r "${WORKING_DIR}/breakpad.${version}/" "${HOME}/Desktop/breakpad.${version}"

set +x
echo
echo "Dumped!"
if [[ "${#missing_architectures[@]}" -gt 0 ]]; then
  echo "dyld_shared_cache not found for architecture(s):" >& 2
  echo "  " "${missing_architectures[@]}" >& 2
  echo "You'll need to get symbols for them elsewhere." >& 2
fi
echo "To upload, run:"
echo
echo "'${dir}/upload_system_symbols'" \\
echo "    --breakpad-tools='${dir}'" \\
echo "    --api-key=<YOUR API KEY>" \\
echo "    --upload-from='${HOME}/Desktop/breakpad.${version}'"
exit ${status}
