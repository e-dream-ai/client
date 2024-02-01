#!/bin/bash

VERSION=$1
DEST=$2
CONFIGURATION=$3

WD=build/${CONFIGURATION}


# Function to print the error message and exit
handle_error() {
    local exit_code="$?"
    echo "Error: Command '$BASH_COMMAND' failed with exit code $exit_code"
    exit "$exit_code"
}

# Set the trap to call the error-handling function
trap 'handle_error' ERR

DEST_TMP=$DEST/tmp

SAVER_TMP=$DEST_TMP/Saver

APP_TMP=$DEST_TMP/App

BASE_DIR=`dirname "$0"`

cd "$BASE_DIR"

mkdir -p "$DEST_TMP" "$SAVER_TMP" "$APP_TMP"

cp -PR "$WD/e-dream.app" "$APP_TMP"

cp -PR "$WD/e-dream.saver" "$SAVER_TMP"

APP_CERT="Developer ID Application: Spotworks LLC (FCG855WTXV)"

codesign --deep --force --verbose --sign "$APP_CERT" "$APP_TMP/e-dream.app"

pkgbuild --root "$APP_TMP" \
    --component-plist "Package/e-dreamAppComponents.plist" \
    --scripts "Package/Scripts" \
    --identifier "com.spotworks.e-dream.app.pkg" \
    --version "$VERSION" \
    --install-location "/Applications" \
    "$DEST_TMP/e-dreamApp.pkg"

pkgbuild --root "$SAVER_TMP" \
    --component-plist "Package/e-dreamSaverComponents.plist" \
    --scripts "Package/Scripts" \
    --identifier "com.spotworks.e-dream.pkg" \
    --version "$VERSION" \
    --install-location "/Library/Screen Savers" \
    "$DEST_TMP/e-dreamSaver.pkg"
    
cp Package/Distribution-template.xml Package/Distribution.xml

#replace the version placeholder in Distribution.xml
sed -i '' -e "s/##VER##/$VERSION/g" Package/Distribution.xml

productbuild --distribution "Package/Distribution.xml"  \
    --package-path "$DEST_TMP" \
    --resources "../Runtime" \
    --sign "Developer ID Installer: Spotworks LLC (FCG855WTXV)" \
    "$DEST/e-dream.pkg"
    
rm -f Package/Distribution.xml

rm -rf "$DEST_TMP"

hdiutil create -fs HFS+ -srcfolder "$DEST" build/"$DEST"

rm -rf "$DEST"

cd -

