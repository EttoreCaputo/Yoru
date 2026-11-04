#!/usr/bin/env bash
# Flash this build onto the Yoru board, and only onto a board that already
# runs Yoru: DualEye shares the Mac and the USB IDs, so /dev/cu.usbmodem* may
# well be the other board (on 2026-11-03 DualEye's firmware went onto Yoru's).
#
#   tools/flash.sh              flash the one board whose app is "yoru"
#   tools/flash.sh --list       say what each board runs, flash nothing
#   tools/flash.sh --port P     flash P whatever it runs (a blank board)
#
# Each board looked at is reset once to read its app. Yoru Hub holding a port
# makes it unreadable: quit the app first. Needs the IDF environment.

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
port=""
list=0
case "${1:-}" in
    --list) list=1 ;;
    --port) port="${2:?--port needs a port}" ;;
    "") ;;
    *) echo "usage: $0 [--list | --port P]" >&2; exit 2 ;;
esac

# The project name in the app's esp_app_desc_t: 32 bytes, 48 bytes into the
# first segment, which starts 0x20 into the factory partition at 0x10000.
project_on() {
    local tmp
    tmp="$(mktemp)"
    if ! python -m esptool --port "$1" read-flash 0x10050 32 "$tmp" >/dev/null 2>&1; then
        rm -f "$tmp"
        echo "?"
        return
    fi
    local name
    name="$(tr -d '\000' < "$tmp" | tr -cd '[:print:]')"
    rm -f "$tmp"
    echo "${name:-blank}"
}

if [[ -z "$port" ]]; then
    found=()
    for p in /dev/cu.usbmodem*; do
        [[ -e "$p" ]] || continue
        if lsof "$p" >/dev/null 2>&1; then
            echo "$p: busy ($(lsof "$p" | awk 'NR==2{print $1}')), not read" >&2
            continue
        fi
        name="$(project_on "$p")"
        echo "$p: $name" >&2
        [[ "$name" == "yoru" ]] && found+=("$p")
    done
    [[ $list -eq 1 ]] && exit 0
    if [[ ${#found[@]} -ne 1 ]]; then
        echo "yoru: ${#found[@]} boards run Yoru; flash one with --port" >&2
        exit 1
    fi
    port="${found[0]}"
fi

cd "$root"
# idf.py is an alias of the activated shell, which a script doesn't inherit.
python "${IDF_PATH:?activate ESP-IDF first}/tools/idf.py" -p "$port" flash
