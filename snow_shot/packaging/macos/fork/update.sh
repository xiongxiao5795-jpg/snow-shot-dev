#!/bin/bash
set -euo pipefail
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
source "$(dirname "$0")/install-snow-shot-macos.sh"
work="$2"
case "$1" in
    check) fork_check "$work" ;;
    download) fork_download "$work" ;;
    *) exit 2 ;;
esac
