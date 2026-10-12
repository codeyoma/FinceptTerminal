#!/bin/bash
# The personal build's in-place update on macOS (codeyoma/krx-quant#92, custom/README.md).
#
# The app downloads a new build's DMG, checks its sha256, starts this script and quits. This
# waits for the app to exit, swaps the installed .app bundle for the one in the DMG (the old
# one is kept aside until the copy succeeds, and put back if it fails) and opens the new one.
# If the swap cannot be done, it opens the DMG so the user can drag the app across by hand.
#
#   update-macos.sh <app pid> <dmg> <installed .app> [--no-launch]
#
# --no-launch (CI): open nothing, neither the new app nor the DMG; the exit code tells.
# The DMG is removed after a successful swap. A log goes to $TMPDIR/fincept-update.log.

set -u
pid=$1
dmg=$2
app=$3
launch=${4:-}
log="${TMPDIR:-/tmp}/fincept-update.log"
exec >>"$log" 2>&1
echo "$(date '+%F %T') update ${app} from ${dmg}"

mnt=""
fail() {
    echo "failed: $1"
    if [ -n "$mnt" ]; then
        hdiutil detach "$mnt" -quiet 2>/dev/null
        rmdir "$mnt" 2>/dev/null
    fi
    [ "$launch" = "--no-launch" ] || open "$dmg"
    exit 1
}

# The app quits right after starting this; give it up to two minutes.
for _ in $(seq 1 600); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.2
done
kill -0 "$pid" 2>/dev/null && fail "the app is still running"

case "$app" in
    *.app) ;;
    *) fail "not an app bundle: ${app}" ;;
esac

mnt=$(mktemp -d "${TMPDIR:-/tmp}/fincept-dmg.XXXXXX") || fail "no mount point"
hdiutil attach "$dmg" -nobrowse -readonly -noautoopen -mountpoint "$mnt" -quiet || fail "could not mount the DMG"
new=$(find "$mnt" -maxdepth 1 -name '*.app' -type d | head -n 1)
[ -n "$new" ] || fail "no app in the DMG"

old="${app}.previous"
rm -rf "$old"
mv "$app" "$old" || fail "could not move the installed app aside"
if ! ditto "$new" "$app" || ! codesign --verify --deep --strict "$app"; then
    rm -rf "$app"
    if mv "$old" "$app"; then
        fail "could not copy the new app; the installed one is back"
    fi
    fail "could not copy the new app, nor put the installed one back: it is at ${old}"
fi
rm -rf "$old"
hdiutil detach "$mnt" -quiet || hdiutil detach "$mnt" -force -quiet || echo "warning: ${mnt} is still mounted"
rmdir "$mnt" 2>/dev/null
mnt=""
xattr -dr com.apple.quarantine "$app" 2>/dev/null
rm -f "$dmg"
echo "updated"
[ "$launch" = "--no-launch" ] || open "$app"
exit 0
