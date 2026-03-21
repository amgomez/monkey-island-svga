#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

./configure --disable-all-engines --enable-engine=scumm "$@"
make -j8 scummvm
