#!/bin/sh
set -ex
dir="$(dirname "${0}")"
dir="$(cd "${dir}"; pwd)"
major_version=$(sw_vers -productVersion | cut -d . -f 1)
if [[ "${major_version}" -lt 13 ]]; then
    dsc_directory="/System/Library/dyld"
else
    dsc_directory="/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld"
fi
version="$(sw_vers -productVersion).$(sw_vers -buildVersion)"
rm -rf "/tmp/dsc.${version}" "/tmp/breakpad.${version}"
mkdir "/tmp/dsc.${version}" "/tmp/breakpad.${version}"

architectures=(x86_64h)
missing_architectures=()
if [[ "${major_version}" -lt 13 ]]; then
  architectures+=( x86_64 )
fi
if [[ "${major_version}" -ge 11 ]]; then
  architectures+=( arm64e )
fi

for arch in ${architectures[@]}; do
  cache="${dsc_directory}/dyld_shared_cache_${arch}"
  if [[ ! -f "${cache}" ]]; then
    missing_architectures+=("${arch}")
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
echo "Dumped!"
if [[ "${#missing_architectures[@]}" -gt 0 ]]; then
  echo "dyld_shared_cache not found for architecture(s): ${missing_architectures[@]}"
  echo "You'll need to get symbols for them elsewhere."
fi
echo "To upload, run:"
echo
echo "'${dir}/upload_system_symbols'" \\
echo "    --breakpad-tools='${dir}'" \\
echo "    --api-key=<YOUR API KEY>" \\
echo "    --upload-from='/tmp/breakpad.${version}'"
