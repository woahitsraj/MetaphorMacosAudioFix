#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="${1:-dev}"
package_dir="${root_dir}/build/windows/package"
dist_dir="${root_dir}/dist"
release_name="MetaphorAudioFix-${version}-win64"
staging_dir="${dist_dir}/${release_name}"
zip_path="${dist_dir}/${release_name}.zip"
sha_path="${zip_path}.sha256"

if [[ ! -d "${package_dir}" ]]; then
  echo "Package directory not found: ${package_dir}" >&2
  echo "Run ./build-windows.sh first." >&2
  exit 1
fi

rm -rf "${staging_dir}" "${zip_path}" "${sha_path}"
mkdir -p "${staging_dir}"

cp "${package_dir}/MetaphorAudioFix.asi" "${staging_dir}/"
cp "${package_dir}/MetaphorAudioFix.ini" "${staging_dir}/"
cp "${package_dir}/libwinpthread-1.dll" "${staging_dir}/"
cp "${root_dir}/README.md" "${staging_dir}/"
cp "${root_dir}/LICENSE" "${staging_dir}/"

(
  cd "${dist_dir}"
  zip -r "${release_name}.zip" "${release_name}" >/dev/null
)

if command -v sha256sum >/dev/null 2>&1; then
  sha256sum "${zip_path}" > "${sha_path}"
elif command -v shasum >/dev/null 2>&1; then
  shasum -a 256 "${zip_path}" > "${sha_path}"
else
  echo "No sha256 tool found; skipping checksum generation." >&2
fi

echo "Created ${zip_path}"
if [[ -f "${sha_path}" ]]; then
  echo "Created ${sha_path}"
fi

