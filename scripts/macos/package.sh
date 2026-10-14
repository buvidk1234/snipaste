#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${1:-$repo_root/build/macos-release}"
qt_root="${2:-${QT_ROOT:-}}"
version="${3:-}"
if [[ -z "$version" ]]; then
    version="$(sed -n '/project(snipaste/,/)/s/.*VERSION \([0-9.]*\).*/\1/p' "$repo_root/CMakeLists.txt")"
fi
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$ ]]
name="snipaste-$version-macos-universal"
stage="$repo_root/dist/$name"
archive="$repo_root/dist/$name.zip"
[[ "$stage" == "$repo_root/dist/"* && -d "$build_dir/snipaste.app" ]]
mkdir -p "$repo_root/dist"
rm -rf -- "$stage"
mkdir -p "$stage"
ditto "$build_dir/snipaste.app" "$stage/snipaste.app"
if [[ -n "$qt_root" ]]; then deploy="$qt_root/bin/macdeployqt"; else deploy="$(command -v macdeployqt)"; fi
"$deploy" "$stage/snipaste.app" -always-overwrite -codesign=-
codesign --force --deep --sign - "$stage/snipaste.app"
codesign --verify --deep --strict "$stage/snipaste.app"
ditto -c -k --sequesterRsrc --keepParent "$stage/snipaste.app" "$archive"
(cd "$repo_root/dist" && shasum -a 256 "$name.zip" > "$name.zip.sha256")
echo "Package: $archive"
