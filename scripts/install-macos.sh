#!/bin/bash
set -euo pipefail
manifest="$1"
task_root="$(cd "$(dirname "$manifest")" && pwd)"
read_value() { /usr/bin/plutil -extract "$1" raw -o - "$manifest"; }
destination="$(read_value destination)"
archive="$(read_value archive)"
expected_hash="$(read_value sha256)"
parent_pid="$(read_value processId)"
config="$(read_value config)"
backup="${destination}.previous-${parent_pid}"
next="${destination}.next-${parent_pid}"
status="$task_root/status.json"
installed=0
moved=0

finish() {
    result=$?
    if [[ $installed -eq 0 ]]; then
        if [[ $moved -eq 1 && -d "$backup" ]]; then
            [[ ! -e "$destination" ]] || /bin/mv "$destination" "$next.failed"
            /bin/mv "$backup" "$destination"
        fi
        printf '{"state":"failed","message":"macOS update failed. The previous application was restored."}\n' > "$status"
    fi
    /usr/bin/open -a "$destination" --args --snipaste-restart --config "$config" || true
    exit "$result"
}
trap finish EXIT

case "$task_root" in */snipaste-update-*) ;; *) exit 1 ;; esac
case "$destination" in /*/snipaste.app) ;; *) exit 1 ;; esac
case "$archive" in "$task_root"/*) ;; *) exit 1 ;; esac
[[ -x "$destination/Contents/MacOS/snipaste" && ! -e "$backup" && ! -e "$next" ]]
[[ "$(/usr/bin/shasum -a 256 "$archive" | /usr/bin/awk '{print $1}')" == "$expected_hash" ]]
/bin/mkdir -p "$task_root/expanded"
/usr/bin/ditto -x -k "$archive" "$task_root/expanded"
source="$task_root/expanded/snipaste.app"
[[ -x "$source/Contents/MacOS/snipaste" ]]
[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$source/Contents/Info.plist")" == "com.snipaste.local" ]]
/usr/bin/codesign --verify --deep --strict "$source"
/usr/bin/ditto "$source" "$next"
printf '{"state":"ready"}\n' > "$status"
deadline=$((SECONDS + 120))
while /bin/kill -0 "$parent_pid" 2>/dev/null; do
    [[ $SECONDS -lt $deadline ]]
    /bin/sleep 0.25
done
/bin/mv "$destination" "$backup"
moved=1
/bin/mv "$next" "$destination"
installed=1
printf '{"state":"installed"}\n' > "$status"
# The previous bundle is retained next to the new app for recovery.
