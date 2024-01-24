#WD=build/Release
WD=build/Debug

VERSION=$1
DEST=$2

DEST_TMP=$DEST/tmp

SAVER_TMP=$DEST_TMP/Saver

APP_TMP=$DEST_TMP/App

BASE_DIR=`dirname "$0"`

cd "$BASE_DIR"

mkdir -p "$DEST_TMP" "$SAVER_TMP" "$APP_TMP"

cp -PR "$WD/e-dream.app" "$APP_TMP"

cp -PR "$WD/e-dream.saver" "$SAVER_TMP"

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
    --sign "Developer ID Installer: Scott Draves (D7639HSC8D)" \
    "$DEST/e-dream.pkg"
    
rm -f Package/Distribution.xml

rm -rf "$DEST_TMP"

hdiutil create -fs HFS+ -srcfolder "$DEST" "$DEST"

cd -

