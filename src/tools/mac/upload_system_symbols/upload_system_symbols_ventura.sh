#!/bin/sh
set -ex
dir="$(dirname "${0}")"
dir="$(cd "${dir}"; pwd)"
major_version=$(sw_vers -productVersion | cut -f 1 -d .)
if [[ $major_version < "13" ]]; then 
    dsc_directory="/System/Library/dyld";
else
    dsc_directory="/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld"; 
fi
version="$(sw_vers -productVersion).$(sw_vers -buildVersion)"
rm -rf "/tmp/dsc.${version}" "/tmp/breakpad.${version}"
mkdir "/tmp/dsc.${version}" "/tmp/breakpad.${version}"
for arch in arm64e x86_64 x86_64h; do
  cache="${dsc_directory}/dyld_shared_cache_${arch}"
  if [[ ! -f $cache ]]; then
    continue
  fi
  "${dir}/dsc_extractor" \
      "${cache}" \
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
echo "    -api-key <YOUR API KEY>" \\
echo "    --upload-from='/tmp/breakpad.${version}'"
