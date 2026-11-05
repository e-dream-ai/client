#!/usr/bin/env bash
# build_appimage.sh — Thin wrapper; the build is implemented in build_appimage.py.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
exec python build_appimage.py "$@"
