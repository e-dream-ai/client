#!/bin/bash
set -e

# Test App Build Script - Archive + Export + Notarization only
# No screensaver embedding - just testing the distribution workflow

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT="infinidream.xcodeproj"
BUILD_CONFIG="Release"
APP_SCHEME="infinidream App Prod"
APP_NAME="infinidream.app"
BUILD_DIR="build"
DERIVED_DATA="${BUILD_DIR}/DerivedData"
KEYCHAIN_PROFILE="infinidream-notarization"

# Parse command line arguments
while getopts "p:" opt; do
    case ${opt} in
        p )
            KEYCHAIN_PROFILE="$OPTARG"
            ;;
        \? )
            echo "Usage: $0 [-p keychain_profile]"
            echo "  -p : Keychain profile name (default: infinidream-notarization)"
            exit 1
            ;;
    esac
done

# Display configuration
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Test App Build - Archive/Export/Notarize${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "Configuration: ${BUILD_CONFIG}"
echo -e "Scheme: ${APP_SCHEME}"
echo -e "Keychain Profile: ${KEYCHAIN_PROFILE}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Clean previous builds
echo -e "${YELLOW}Cleaning previous builds...${NC}"
if [ -d "$BUILD_DIR" ]; then
    /bin/rm -rf "$BUILD_DIR" 2>/dev/null || {
        echo -e "${YELLOW}Standard cleanup failed, trying alternative method...${NC}"
        find "$BUILD_DIR" -name ".DS_Store" -delete 2>/dev/null
        /bin/rm -rf "$BUILD_DIR"
    }
fi

# Create output directory
mkdir -p "${BUILD_DIR}/${BUILD_CONFIG}"

# Archive app
echo -e "${YELLOW}Archiving app (${APP_SCHEME})...${NC}"
APP_ARCHIVE_PATH="${BUILD_DIR}/${BUILD_CONFIG}/${APP_NAME}.xcarchive"

xcodebuild archive \
           -project "$PROJECT" \
           -scheme "$APP_SCHEME" \
           -configuration "$BUILD_CONFIG" \
           -derivedDataPath "$DERIVED_DATA" \
           -archivePath "$APP_ARCHIVE_PATH" \
           | xcpretty --color || {
    # Fallback if xcpretty is not installed
    xcodebuild archive \
               -project "$PROJECT" \
               -scheme "$APP_SCHEME" \
               -configuration "$BUILD_CONFIG" \
               -derivedDataPath "$DERIVED_DATA" \
               -archivePath "$APP_ARCHIVE_PATH"
}

# Verify archive was created
if [ ! -d "${APP_ARCHIVE_PATH}" ]; then
    echo -e "${RED}Failed to create app archive${NC}"
    echo -e "${RED}Expected at: ${APP_ARCHIVE_PATH}${NC}"
    exit 1
fi

echo -e "${GREEN}✓ App archive created successfully${NC}"

# Create export options plist for app distribution
APP_EXPORT_OPTIONS="${BUILD_DIR}/${BUILD_CONFIG}/AppExportOptions.plist"
cat > "$APP_EXPORT_OPTIONS" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>method</key>
    <string>developer-id</string>
    <key>teamID</key>
    <string>3L54M5L5KK</string>
    <key>signingStyle</key>
    <string>automatic</string>
    <key>signingCertificate</key>
    <string>Developer ID Application</string>
</dict>
</plist>
EOF

# Export archive for distribution
echo -e "${YELLOW}Exporting app for distribution...${NC}"
APP_EXPORT_PATH="${BUILD_DIR}/${BUILD_CONFIG}/AppExport"

xcodebuild -exportArchive \
           -archivePath "$APP_ARCHIVE_PATH" \
           -exportPath "$APP_EXPORT_PATH" \
           -exportOptionsPlist "$APP_EXPORT_OPTIONS" \
           | xcpretty --color || {
    # Fallback if xcpretty is not installed
    xcodebuild -exportArchive \
               -archivePath "$APP_ARCHIVE_PATH" \
               -exportPath "$APP_EXPORT_PATH" \
               -exportOptionsPlist "$APP_EXPORT_OPTIONS"
}

# Find the exported app
EXPORTED_APP=$(find "$APP_EXPORT_PATH" -name "*.app" -type d | head -1)
if [ ! -d "$EXPORTED_APP" ]; then
    echo -e "${RED}Failed to export app${NC}"
    echo "Export path contents:"
    ls -la "$APP_EXPORT_PATH" 2>/dev/null || echo "Export path not found"
    exit 1
fi

echo -e "${GREEN}✓ App exported successfully${NC}"
echo "Exported app: $EXPORTED_APP"

# Create zip for notarization
echo -e "${YELLOW}Creating zip for notarization...${NC}"
APP_ZIP="${BUILD_DIR}/${BUILD_CONFIG}/app.zip"
ditto -c -k --keepParent "$EXPORTED_APP" "$APP_ZIP"

if [ ! -f "$APP_ZIP" ]; then
    echo -e "${RED}Failed to create zip file${NC}"
    exit 1
fi

# Notarize app
echo -e "${YELLOW}Notarizing app...${NC}"
echo -e "${YELLOW}This may take several minutes...${NC}"

# Capture notarization output
NOTARY_OUTPUT=$(xcrun notarytool submit "$APP_ZIP" \
                 --keychain-profile "$KEYCHAIN_PROFILE" \
                 --wait 2>&1)

# Display the output
echo "$NOTARY_OUTPUT"

# Extract submission ID
SUBMISSION_ID=$(echo "$NOTARY_OUTPUT" | grep -E "id: [a-f0-9-]+" | head -1 | awk '{print $2}')

# Check for invalid or rejected status
if echo "$NOTARY_OUTPUT" | grep -q "status: Invalid\|status: Rejected"; then
    echo ""
    echo -e "${RED}✗ App notarization failed - Status: Invalid/Rejected${NC}"
    echo ""
    if [ -n "$SUBMISSION_ID" ]; then
        echo "Submission ID: ${SUBMISSION_ID}"
        echo ""
        echo "To see why notarization failed, run:"
        echo "  xcrun notarytool log ${SUBMISSION_ID} --keychain-profile '${KEYCHAIN_PROFILE}'"
        echo ""
    fi
    rm "$APP_ZIP"
    exit 1
fi

# Check if submission failed entirely
if ! echo "$NOTARY_OUTPUT" | grep -q "status: Accepted"; then
    echo ""
    echo -e "${RED}✗ App notarization failed${NC}"
    rm "$APP_ZIP"
    exit 1
fi

echo -e "${GREEN}✓ App notarization successful${NC}"

# Staple the notarization ticket
echo -e "${YELLOW}Stapling notarization ticket to app...${NC}"
if ! xcrun stapler staple "$EXPORTED_APP"; then
    echo -e "${RED}✗ Failed to staple notarization ticket${NC}"
    echo "The app was notarized but stapling failed."
    echo "Users will need internet access to verify the app on first launch."
else
    echo -e "${GREEN}✓ Notarization ticket stapled successfully${NC}"
fi

# Verify notarization
echo -e "${YELLOW}Verifying app notarization...${NC}"
if spctl --assess --type execute -vvv "$EXPORTED_APP" 2>&1 | grep -q "accepted"; then
    echo -e "${GREEN}✓ App notarization verification passed${NC}"
else
    echo -e "${YELLOW}⚠ Could not fully verify notarization (app may still be valid)${NC}"
fi

# Get file sizes
APP_SIZE=$(du -sh "$EXPORTED_APP" | cut -f1)
ZIP_SIZE=$(du -h "$APP_ZIP" | cut -f1)

# Clean up zip
rm "$APP_ZIP"

# Summary
echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Test App Build Complete! 🎉${NC}"
echo -e "${GREEN}========================================${NC}"
echo "Archive: ${APP_ARCHIVE_PATH}"
echo "Exported app: ${EXPORTED_APP} (${APP_SIZE})"
echo "Notarization: ✓ Complete"
echo ""
echo "To test the app, run:"
echo "  open '${EXPORTED_APP}'"
echo ""