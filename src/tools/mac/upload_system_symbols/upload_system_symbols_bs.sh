#!/bin/sh
set -ex
dir="$(dirname "${0}")"
dir="$(cd "${dir}"; pwd)"
version="$(sw_vers -productVersion).$(sw_vers -buildVersion)"
rm -rf "/tmp/dsc.${version}" "/tmp/breakpad.${version}"
mkdir "/tmp/dsc.${version}" "/tmp/breakpad.${version}"
for arch in arm64e x86_64 x86_64h; do
  "${dir}/dsc_extractor" \
      "/System/Library/dyld/dyld_shared_cache_${arch}" \
      "/tmp/dsc.${version}/${arch}"
  mkdir -p "/tmp/dsc.${version}/${arch}/System/Library/Components"
  "${dir}/upload_system_symbols" \
      --breakpad-tools="${dir}" \
      --system-root="/tmp/dsc.${version}/${arch}" \
      --dump-to="/tmp/breakpad.${version}"
done
"${dir}/upload_system_symbols" \
    --breakpad-tools="${dir}" \
    --system-root=/ \
    --dump-to="/tmp/breakpad.${version}"
cd /tmp
GZIP=-9 tar -zcf ~/"Desktop/breakpad.${version}.tar.gz" "breakpad.${version}"

set +x
echo
echo "Dumped! To upload, run:"
echo
echo "'${dir}/upload_system_symbols'" \\
echo "    --breakpad-tools='${dir}'" \\
echo "    --upload-from='/tmp/breakpad.${version}'"
