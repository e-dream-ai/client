#!/bin/bash
# Build ScreenSaver Prod/Stage from the main infinidream.xcodeproj and embed into PlugIns.
# Invoked from App Prod / App Stage Run Script phases and from build.py.
set -euo pipefail

# Xcode Run Script phases often lack Homebrew on PATH.
export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:${PATH:-}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT="$SCRIPT_DIR/infinidream.xcodeproj"
CONFIG="${CONFIGURATION:-Debug}"
STAGE_FLAG="${1:-}"

# Are we embedding into a concrete .app? (Xcode env or CLI dest)
EMBEDDING=0
if [[ -n "${TARGET_BUILD_DIR:-}" && -n "${WRAPPER_NAME:-}" ]]; then
  EMBEDDING=1
elif [[ -n "${2:-}" ]]; then
  EMBEDDING=1
fi

if [[ "$STAGE_FLAG" == "stage" ]] || [[ "${PRODUCT_BUNDLE_IDENTIFIER:-}" == *stage* ]]; then
  SCHEME="ScreenSaver Stage"
  APPEX_NAME="infinidream-stage.appex"
else
  SCHEME="ScreenSaver Prod"
  APPEX_NAME="infinidream.appex"
fi

if [[ ! -d "$PROJECT" ]]; then
  echo "embed_screensaver: ERROR missing $PROJECT" >&2
  exit 1
fi

DERIVED="$SCRIPT_DIR/DerivedData-Screensaver"
LOG="/tmp/embed_screensaver_xcodebuild.log"

# Nested xcodebuild inherits the host app's env which breaks the screensaver
# target. Use a clean environment.
SS_TEAM="${SCREENSAVER_DEVELOPMENT_TEAM:-J4KXDFT4C5}"
set +e
env -i \
  HOME="$HOME" \
  USER="${USER:-}" \
  LOGNAME="${LOGNAME:-${USER:-}}" \
  PATH="$PATH" \
  LANG="${LANG:-en_US.UTF-8}" \
  TERM="${TERM:-dumb}" \
  TMPDIR="${TMPDIR:-/tmp}" \
  DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}" \
  SSH_AUTH_SOCK="${SSH_AUTH_SOCK:-}" \
  Apple_PubSub_Socket_Render="${Apple_PubSub_Socket_Render:-}" \
  XPC_FLAGS="${XPC_FLAGS:-}" \
  __CF_USER_TEXT_ENCODING="${__CF_USER_TEXT_ENCODING:-}" \
  /usr/bin/xcodebuild \
    -project "$PROJECT" \
    -scheme "$SCHEME" \
    -configuration "$CONFIG" \
    -derivedDataPath "$DERIVED" \
    -allowProvisioningUpdates \
    build \
    CODE_SIGN_STYLE=Automatic \
    DEVELOPMENT_TEAM="$SS_TEAM" \
    ENABLE_DEBUG_DYLIB=NO \
    >"$LOG" 2>&1
XCODE_STATUS=$?
set -e

/usr/bin/grep -E 'error:|warning:|BUILD SUCCEEDED|BUILD FAILED|Provisioning' "$LOG" || true

if [[ "$XCODE_STATUS" -ne 0 ]]; then
  echo "embed_screensaver: ERROR xcodebuild failed (status $XCODE_STATUS)" >&2
  tail -80 "$LOG" >&2 || true
  exit "$XCODE_STATUS"
fi

SRC="$DERIVED/Build/Products/$CONFIG/$APPEX_NAME"
if [[ ! -d "$SRC" ]]; then
  FOUND="$(/usr/bin/find "$DERIVED/Build/Products" -type d -name "$APPEX_NAME" 2>/dev/null | head -1 || true)"
  if [[ -n "$FOUND" && -d "$FOUND" ]]; then
    SRC="$FOUND"
  else
    echo "embed_screensaver: ERROR built appex missing at $SRC" >&2
    exit 1
  fi
fi

if [[ -n "${TARGET_BUILD_DIR:-}" && -n "${WRAPPER_NAME:-}" ]]; then
  DEST_DIR="$TARGET_BUILD_DIR/$WRAPPER_NAME/Contents/PlugIns"
else
  APP_PATH="${2:-}"
  if [[ -z "$APP_PATH" || ! -d "$APP_PATH" ]]; then
    if [[ "$EMBEDDING" -eq 1 ]]; then
      echo "embed_screensaver: ERROR embedding requested but app path missing" >&2
      exit 1
    fi
    echo "embed_screensaver: built $SRC (no app destination)"
    exit 0
  fi
  DEST_DIR="$APP_PATH/Contents/PlugIns"
fi

mkdir -p "$DEST_DIR"
rm -rf "$DEST_DIR/$APPEX_NAME"
ditto "$SRC" "$DEST_DIR/$APPEX_NAME"

# Re-sign with the same identity the host is using when available.
IDENTITY="${EXPANDED_CODE_SIGN_IDENTITY_NAME:-${CODE_SIGN_IDENTITY:-}}"
if [[ -n "$IDENTITY" && "$IDENTITY" != "-" ]]; then
  codesign --force --sign "$IDENTITY" --entitlements "$SCRIPT_DIR/Screensaver.entitlements" \
    --generate-entitlement-der "$DEST_DIR/$APPEX_NAME" || true
fi

echo "embed_screensaver: embedded $APPEX_NAME -> $DEST_DIR"
