#!/bin/bash
#set -x

CONFIGURATION=Debug
CERT_ID=$1

WD=build/${CONFIGURATION}

# Function to print the error message and exit
handle_error() {
    local exit_code="$?"
    echo "Error: Command '$BASH_COMMAND' failed with exit code $exit_code"
    exit "$exit_code"
}

# Set the trap to call the error-handling function
trap 'handle_error' ERR

BASE_DIR=`dirname "$0"`

cd "$BASE_DIR"


xcodebuild -scheme All -configuration "$CONFIGURATION" -project e-dream.xcodeproj clean

xcodebuild -scheme All -configuration "$CONFIGURATION" -project e-dream.xcodeproj build

#VERSION=$(defaults read "`pwd`/$WD/e-dream.saver/Contents/Info" CFBundleVersion)
VERSION=$(git describe --tags --abbrev=0)

./build_installer.sh "$VERSION" "e-dream-$VERSION" "$CONFIGURATION" "$CERT_ID"

cd -
