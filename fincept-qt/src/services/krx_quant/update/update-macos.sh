#!/bin/bash
# The personal build's self-update on macOS (codeyoma/krx-quant#92, #130, custom/README.md
# "앱 업데이트").
#
# The app downloads a new build's DMG, checks its sha256, starts this script and keeps running.
# This waits for the app to exit (the user pressed 지금 재시작, or quit the app some other time),
# swaps the installed .app bundle for the one in the DMG (the old one is kept aside until the copy
# succeeds, and put back if it fails), and opens the new one only when the relaunch file exists by
# then (the app makes it for 지금 재시작). If the swap fails after a restart was asked for, it
# opens the old app and the DMG, so the user can drag the new app across by hand.
#
#   update-macos.sh <app pid> <dmg> <installed .app> [<relaunch file> | --no-launch]
#
# --no-launch (CI), or no relaunch file: open nothing; the exit code tells.
# The DMG is removed after a successful swap. A log goes to $TMPDIR/fincept-update.log.

set -u
pid=$1
dmg=$2
app=$3
relaunch=${4:-}
log="${TMPDIR:-/tmp}/fincept-update.log"
exec >>"$log" 2>&1
echo "$(date '+%F %T') update ${app} from ${dmg}"

mnt=""
# True when the user asked for a restart: the app made the relaunch file before it quit.
restart_asked() {
    [ -n "$relaunch" ] && [ "$relaunch" != "--no-launch" ] && [ -e "$relaunch" ]
}

fail() {
    echo "failed: $1"
    if [ -n "$mnt" ]; then
        hdiutil detach "$mnt" -quiet 2>/dev/null
        rmdir "$mnt" 2>/dev/null
    fi
    if restart_asked; then
        rm -f "$relaunch"
        [ -d "$app" ] && open "$app"
        open "$dmg"
    fi
    exit 1
}

# The app keeps running until the user restarts or quits it: wait as long as that takes.
while kill -0 "$pid" 2>/dev/null; do
    sleep 1
done

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
if restart_asked; then
    rm -f "$relaunch"
    open "$app"
fi
exit 0
