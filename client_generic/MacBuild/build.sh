#!/bin/bash
set -e

# infinidream Unified Build Script
# Builds screensaver and app, with optional notarization
# Combines the functionality of build_screensaver.sh, notarize_screensaver.sh, and build_app_with_screensaver.sh
#
# Auto-discovers code signing credentials from keychain, with fallback to defaults
# Override via environment variables: DEVELOPER_ID_CERT, TEAM_ID, KEYCHAIN_PROFILE

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default configuration
BUILD_RELEASE=false
BUILD_STAGE=false
NOTARIZE=false
GENERATE_APPCAST=false
BUILD_CONFIG="Debug"
SCREENSAVER_SCHEME="ScreenSaver Prod"
APP_SCHEME="infinidream App Prod"
SCREENSAVER_NAME="infinidream.saver"
APP_NAME="infinidream.app"
VERSION=""

# Signing configuration
# These can be overridden by environment variables: DEVELOPER_ID_CERT, TEAM_ID, KEYCHAIN_PROFILE

# Auto-discover Developer ID certificate if not set
if [ -z "$DEVELOPER_ID_CERT" ]; then
    # Try to find Developer ID Application certificate in keychain
    DISCOVERED_CERT=$(security find-identity -v -p codesigning | grep "Developer ID Application" | head -1 | sed -E 's/^[^"]*"([^"]+)".*$/\1/')
    if [ -n "$DISCOVERED_CERT" ]; then
        DEVELOPER_ID_CERT="$DISCOVERED_CERT"
        CERT_AUTO_DISCOVERED=true
    else
        # Fallback to default
        DEVELOPER_ID_CERT="Developer ID Application: Guillaume Louel (3L54M5L5KK)"
        CERT_AUTO_DISCOVERED=false
    fi
else
    CERT_AUTO_DISCOVERED=false
fi

# Auto-discover Team ID from certificate if not set
if [ -z "$TEAM_ID" ]; then
    # Extract Team ID from certificate (text in parentheses)
    EXTRACTED_TEAM_ID=$(echo "$DEVELOPER_ID_CERT" | sed -E 's/.*\(([^)]+)\).*/\1/')
    if [ -n "$EXTRACTED_TEAM_ID" ] && [ "$EXTRACTED_TEAM_ID" != "$DEVELOPER_ID_CERT" ]; then
        TEAM_ID="$EXTRACTED_TEAM_ID"
        TEAM_AUTO_DISCOVERED=true
    else
        # Fallback to default
        TEAM_ID="3L54M5L5KK"
        TEAM_AUTO_DISCOVERED=false
    fi
else
    TEAM_AUTO_DISCOVERED=false
fi

# Keychain profile for notarization (can be overridden by environment variable)
if [ -z "$KEYCHAIN_PROFILE" ]; then
    KEYCHAIN_PROFILE="infinidream-notarization"
fi

# Parse command line arguments
while getopts "rsnav:" opt; do
    case ${opt} in
        r )
            BUILD_RELEASE=true
            BUILD_CONFIG="Release"
            ;;
        s )
            BUILD_STAGE=true
            SCREENSAVER_SCHEME="ScreenSaver Stage"
            APP_SCHEME="infinidream App Stage"
            SCREENSAVER_NAME="infinidream-stage.saver"
            APP_NAME="infinidream stage.app"
            ;;
        n )
            NOTARIZE=true
            ;;
        a )
            GENERATE_APPCAST=true
            ;;
        v )
            VERSION="$OPTARG"
            ;;
        \? )
            echo "Usage: $0 [-r] [-s] [-n] [-a] [-v VERSION]"
            echo "  -r : Build in Release mode (default: Debug)"
            echo "  -s : Build stage version (default: production)"
            echo "  -n : Enable notarization (requires -r)"
            echo "  -a : Generate appcast.xml for Sparkle updates"
            echo "  -v : Version string (e.g., 0.12.0) - used for zip naming and appcast"
            echo ""
            echo "Environment variables (optional overrides):"
            echo "  DEVELOPER_ID_CERT : Code signing certificate name"
            echo "  TEAM_ID           : Apple Developer Team ID"
            echo "  KEYCHAIN_PROFILE  : Notarization keychain profile name"
            exit 1
            ;;
    esac
done

# Validate configuration
if [ "$NOTARIZE" = true ] && [ "$BUILD_RELEASE" = false ]; then
    echo -e "${RED}Error: Notarization requires Release mode. Use -r flag with -n${NC}"
    exit 1
fi

if [ "$GENERATE_APPCAST" = true ] && [ -z "$VERSION" ]; then
    echo -e "${RED}Error: Appcast generation requires version. Use -v flag with -a${NC}"
    exit 1
fi

# If version not provided, try to extract from Info.plist
if [ -z "$VERSION" ]; then
    VERSION=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "infinidream-App-Info.plist" 2>/dev/null || echo "")
fi

# Update Info.plist with provided version (if -v was specified)
if [ -n "$VERSION" ]; then
    # Determine which Info.plist to update based on stage/prod
    if [ "$BUILD_STAGE" = true ]; then
        APP_INFO_PLIST="infinidream App copy-Info.plist"
    else
        APP_INFO_PLIST="infinidream-App-Info.plist"
    fi
    
    # Get current version from plist
    CURRENT_VERSION=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$APP_INFO_PLIST" 2>/dev/null || echo "")
    
    if [ "$CURRENT_VERSION" != "$VERSION" ]; then
        echo -e "${YELLOW}Updating $APP_INFO_PLIST version from $CURRENT_VERSION to $VERSION...${NC}"
        /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" "$APP_INFO_PLIST"
        /usr/libexec/PlistBuddy -c "Set :CFBundleVersion $VERSION" "$APP_INFO_PLIST"
        echo -e "${GREEN}✓ Version updated in Info.plist${NC}"
    fi
fi

# Configuration
PROJECT="infinidream.xcodeproj"
BUILD_DIR="build"
DERIVED_DATA="${BUILD_DIR}/DerivedData"
PRODUCTS_DIR="${DERIVED_DATA}/Build/Products/${BUILD_CONFIG}"
PROJECT_SCREENSAVER="Resources/${SCREENSAVER_NAME}"

# Display build configuration
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}infinidream Unified Build${NC}"
echo -e "${BLUE}========================================${NC}"
if [ "$BUILD_STAGE" = true ]; then
    echo -e "Environment: ${YELLOW}STAGE${NC}"
else
    echo -e "Environment: ${GREEN}PRODUCTION${NC}"
fi
echo -e "Configuration: ${BUILD_CONFIG}"
echo -e "Screensaver Scheme: ${SCREENSAVER_SCHEME}"
echo -e "App Scheme: ${APP_SCHEME}"
echo -e "Notarization: $([ "$NOTARIZE" = true ] && echo "Enabled" || echo "Disabled")"
echo -e "Generate Appcast: $([ "$GENERATE_APPCAST" = true ] && echo "Enabled" || echo "Disabled")"
if [ -n "$VERSION" ]; then
    echo -e "Version: ${VERSION}"
fi

# Show signing configuration if Release build
if [ "$BUILD_RELEASE" = true ]; then
    echo -e "Code Signing:"
    # Show certificate with discovery status
    if [ "$CERT_AUTO_DISCOVERED" = true ]; then
        echo -e "  Certificate: ${DEVELOPER_ID_CERT} ${GREEN}(auto-discovered)${NC}"
    else
        echo -e "  Certificate: ${DEVELOPER_ID_CERT} ${YELLOW}(default)${NC}"
    fi
    # Show Team ID with discovery status
    if [ "$TEAM_AUTO_DISCOVERED" = true ]; then
        echo -e "  Team ID: ${TEAM_ID} ${GREEN}(auto-discovered)${NC}"
    else
        echo -e "  Team ID: ${TEAM_ID} ${YELLOW}(default)${NC}"
    fi
else
    echo -e "Code Signing: Disabled"
fi

# Show notarization profile if notarization is enabled
if [ "$NOTARIZE" = true ]; then
    echo -e "Notarization Profile: ${KEYCHAIN_PROFILE}"
fi

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

# ============================================================================
# STEP 1: Build Screensaver
# ============================================================================
echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}STEP 1: Building Screensaver${NC}"
echo -e "${BLUE}========================================${NC}"

if [ "$BUILD_RELEASE" = true ]; then
    # Release build with manual code signing
    BUILD_VERSION="$VERSION" xcodebuild -project "$PROJECT" \
               -scheme "$SCREENSAVER_SCHEME" \
               -configuration "$BUILD_CONFIG" \
               -derivedDataPath "$DERIVED_DATA" \
               CODE_SIGN_STYLE="Manual" \
               CODE_SIGN_IDENTITY="$DEVELOPER_ID_CERT" \
               DEVELOPMENT_TEAM="$TEAM_ID" \
               OTHER_CODE_SIGN_FLAGS="--timestamp" \
               | xcpretty --color || {
        # Fallback if xcpretty is not installed
        BUILD_VERSION="$VERSION" xcodebuild -project "$PROJECT" \
                   -scheme "$SCREENSAVER_SCHEME" \
                   -configuration "$BUILD_CONFIG" \
                   -derivedDataPath "$DERIVED_DATA" \
                   CODE_SIGN_STYLE="Manual" \
                   CODE_SIGN_IDENTITY="$DEVELOPER_ID_CERT" \
                   DEVELOPMENT_TEAM="$TEAM_ID" \
                   OTHER_CODE_SIGN_FLAGS="--timestamp"
    }
else
    # Debug build without code signing
    BUILD_VERSION="$VERSION" xcodebuild -project "$PROJECT" \
               -scheme "$SCREENSAVER_SCHEME" \
               -configuration "$BUILD_CONFIG" \
               -derivedDataPath "$DERIVED_DATA" \
               CODE_SIGN_IDENTITY="" \
               CODE_SIGNING_REQUIRED=NO \
               CODE_SIGNING_ALLOWED=NO
fi

# Verify screensaver build
if [ ! -d "${PRODUCTS_DIR}/${SCREENSAVER_NAME}" ]; then
    echo -e "${RED}Failed to build ${SCREENSAVER_NAME}${NC}"
    echo -e "${RED}Expected at: ${PRODUCTS_DIR}/${SCREENSAVER_NAME}${NC}"
    exit 1
fi

echo -e "${GREEN}✓ ${SCREENSAVER_NAME} built successfully${NC}"

# Verify code signing for Release builds
if [ "$BUILD_RELEASE" = true ]; then
    echo -e "${YELLOW}Verifying code signature...${NC}"
    if codesign --verify --deep --strict "${PRODUCTS_DIR}/${SCREENSAVER_NAME}" 2>&1; then
        echo -e "${GREEN}✓ Code signature verified${NC}"

        # Check with spctl for more detailed info (non-fatal)
        echo -e "${YELLOW}Checking signature details with spctl...${NC}"
        SPCTL_OUTPUT=$(spctl -vvv --assess --type exec "${PRODUCTS_DIR}/${SCREENSAVER_NAME}" 2>&1 || true)

        if [ -n "$SPCTL_OUTPUT" ]; then
            echo "Signature status: $SPCTL_OUTPUT"

            # Check if signed with Developer ID Application
            if echo "$SPCTL_OUTPUT" | grep -q "Developer ID Application"; then
                echo -e "${GREEN}✓ Signed with Developer ID Application certificate${NC}"
            else
                echo -e "${YELLOW}⚠ Warning: Not signed with Developer ID Application certificate${NC}"
            fi
        else
            echo -e "${YELLOW}⚠ Could not get detailed signature information${NC}"
        fi
    else
        echo -e "${RED}✗ Code signature verification failed${NC}"
        echo "This may cause notarization to fail."
    fi
fi

# Copy screensaver to output directory for backwards compatibility
echo -e "${YELLOW}Copying screensaver to output directory...${NC}"
OUTPUT_SAVER="${BUILD_DIR}/${BUILD_CONFIG}/${SCREENSAVER_NAME}"
cp -R "${PRODUCTS_DIR}/${SCREENSAVER_NAME}" "${OUTPUT_SAVER}"

if [ ! -d "${OUTPUT_SAVER}" ]; then
    echo -e "${RED}Failed to copy screensaver to output directory${NC}"
    exit 1
fi

# Copy to project Resources directory and verify with MD5
echo -e "${YELLOW}Copying screensaver to project Resources directory...${NC}"

# Remove existing screensaver if present to ensure clean copy
if [ -d "${PROJECT_SCREENSAVER}" ]; then
    echo -e "${YELLOW}Removing existing screensaver in Resources...${NC}"
    rm -rf "${PROJECT_SCREENSAVER}"
fi

# Copy fresh build to Resources
cp -R "${PRODUCTS_DIR}/${SCREENSAVER_NAME}" "${PROJECT_SCREENSAVER}"

if [ ! -d "${PROJECT_SCREENSAVER}" ]; then
    echo -e "${RED}Failed to copy screensaver to project Resources${NC}"
    exit 1
fi

# Verify the copy with MD5 checksums
echo -e "${YELLOW}Verifying copy integrity with MD5...${NC}"
SOURCE_MD5=$(find "${PRODUCTS_DIR}/${SCREENSAVER_NAME}" -type f -exec md5 -q {} \; | sort | md5 -q)
DEST_MD5=$(find "${PROJECT_SCREENSAVER}" -type f -exec md5 -q {} \; | sort | md5 -q)

if [ "$SOURCE_MD5" != "$DEST_MD5" ]; then
    echo -e "${RED}✗ MD5 mismatch! Copy verification failed${NC}"
    echo "Source MD5: $SOURCE_MD5"
    echo "Dest MD5: $DEST_MD5"
    exit 1
fi

echo -e "${GREEN}✓ MD5 verification passed - screensaver copied correctly${NC}"

# Create zip for notarization from the fresh build in DerivedData
SCREENSAVER_ZIP="${BUILD_DIR}/${BUILD_CONFIG}/screensaver.zip"
echo -e "${YELLOW}Creating zip of screensaver for notarization...${NC}"
ditto -c -k --keepParent "${PRODUCTS_DIR}/${SCREENSAVER_NAME}" "${SCREENSAVER_ZIP}"

if [ ! -f "${SCREENSAVER_ZIP}" ]; then
    echo -e "${RED}Failed to create zip file${NC}"
    exit 1
fi

# ============================================================================
# STEP 2: Notarize Screensaver (if requested)
# ============================================================================
if [ "$NOTARIZE" = true ]; then
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}STEP 2: Notarizing Screensaver${NC}"
    echo -e "${BLUE}========================================${NC}"

    echo -e "${YELLOW}Submitting screensaver to Apple for notarization...${NC}"
    echo -e "${YELLOW}This may take several minutes...${NC}"

    # Capture notarization output
    NOTARY_OUTPUT=$(xcrun notarytool submit "${SCREENSAVER_ZIP}" \
                     --keychain-profile "$KEYCHAIN_PROFILE" \
                     --wait 2>&1)

    # Display the output
    echo "$NOTARY_OUTPUT"

    # Extract submission ID
    SUBMISSION_ID=$(echo "$NOTARY_OUTPUT" | grep -E "id: [a-f0-9-]+" | head -1 | awk '{print $2}')

    # Check for invalid or rejected status
    if echo "$NOTARY_OUTPUT" | grep -q "status: Invalid\|status: Rejected"; then
        echo ""
        echo -e "${RED}✗ Screensaver notarization failed - Status: Invalid/Rejected${NC}"
        echo ""
        if [ -n "$SUBMISSION_ID" ]; then
            echo "Submission ID: ${SUBMISSION_ID}"
            echo ""
            echo "To see why notarization failed, run:"
            echo "  xcrun notarytool log ${SUBMISSION_ID} --keychain-profile '$KEYCHAIN_PROFILE'"
            echo ""
        fi
        echo "Common issues:"
        echo "- Missing code signature"
        echo "- Invalid or expired certificate"
        echo "- Unsigned binaries in the bundle"
        echo "- Missing entitlements"
        exit 1
    fi

    # Check if submission failed entirely
    if ! echo "$NOTARY_OUTPUT" | grep -q "status: Accepted"; then
        echo ""
        echo -e "${RED}✗ Screensaver notarization failed${NC}"
        echo ""
        echo "Troubleshooting tips:"
        echo "1. Ensure keychain profile '$KEYCHAIN_PROFILE' is configured:"
        echo "   xcrun notarytool store-credentials '$KEYCHAIN_PROFILE' \\"
        echo "   --apple-id YOUR_APPLE_ID --team-id YOUR_TEAM_ID"
        echo ""
        echo "2. Check notarization history:"
        echo "   xcrun notarytool history --keychain-profile '$KEYCHAIN_PROFILE'"
        exit 1
    fi

    echo -e "${GREEN}✓ Screensaver notarization successful${NC}"

    # Staple the notarization ticket to project screensaver
    echo -e "${YELLOW}Stapling notarization ticket to screensaver...${NC}"
    if ! xcrun stapler staple "${PROJECT_SCREENSAVER}"; then
        echo -e "${RED}✗ Failed to staple notarization ticket${NC}"
        echo "The screensaver was notarized but stapling failed."
        echo "You may continue, but the screensaver will need internet access to verify."
        exit 1
    fi

    echo -e "${GREEN}✓ Notarization ticket stapled successfully${NC}"

    # Verify stapling
    echo -e "${YELLOW}Verifying notarization...${NC}"
    if spctl --assess --type install -vvv "${PROJECT_SCREENSAVER}" 2>&1 | grep -q "accepted"; then
        echo -e "${GREEN}✓ Notarization verification passed${NC}"
    else
        echo -e "${YELLOW}⚠ Could not verify notarization (this may be normal for screensavers)${NC}"
    fi
fi

# ============================================================================
# STEP 3: Archive App
# ============================================================================
echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}STEP 3: Archiving App${NC}"
echo -e "${BLUE}========================================${NC}"

echo -e "${GREEN}✓ Found notarized screensaver in project Resources${NC}"

# Archive app (like Xcode Archive)
echo -e "${YELLOW}Archiving app (${APP_SCHEME})...${NC}"
APP_ARCHIVE_PATH="${BUILD_DIR}/${BUILD_CONFIG}/${APP_NAME}.xcarchive"

if [ "$BUILD_RELEASE" = true ]; then
    # Release build with manual code signing
    BUILD_VERSION="$VERSION" xcodebuild archive \
               -project "$PROJECT" \
               -scheme "$APP_SCHEME" \
               -configuration "$BUILD_CONFIG" \
               -derivedDataPath "$DERIVED_DATA" \
               -archivePath "$APP_ARCHIVE_PATH" \
               CODE_SIGN_STYLE="Manual" \
               CODE_SIGN_IDENTITY="$DEVELOPER_ID_CERT" \
               DEVELOPMENT_TEAM="$TEAM_ID" \
               OTHER_CODE_SIGN_FLAGS="--timestamp" \
               | xcpretty --color || {
        # Fallback if xcpretty is not installed
        BUILD_VERSION="$VERSION" xcodebuild archive \
                   -project "$PROJECT" \
                   -scheme "$APP_SCHEME" \
                   -configuration "$BUILD_CONFIG" \
                   -derivedDataPath "$DERIVED_DATA" \
                   -archivePath "$APP_ARCHIVE_PATH" \
                   CODE_SIGN_STYLE="Manual" \
                   CODE_SIGN_IDENTITY="$DEVELOPER_ID_CERT" \
                   DEVELOPMENT_TEAM="$TEAM_ID" \
                   OTHER_CODE_SIGN_FLAGS="--timestamp"
    }
else
    # Debug build without code signing
    BUILD_VERSION="$VERSION" xcodebuild archive \
               -project "$PROJECT" \
               -scheme "$APP_SCHEME" \
               -configuration "$BUILD_CONFIG" \
               -derivedDataPath "$DERIVED_DATA" \
               -archivePath "$APP_ARCHIVE_PATH" \
               CODE_SIGN_IDENTITY="" \
               CODE_SIGNING_REQUIRED=NO \
               CODE_SIGNING_ALLOWED=NO
fi

# Verify archive was created
if [ ! -d "${APP_ARCHIVE_PATH}" ]; then
    echo -e "${RED}Failed to create app archive${NC}"
    echo -e "${RED}Expected at: ${APP_ARCHIVE_PATH}${NC}"
    exit 1
fi

echo -e "${GREEN}✓ App archive created successfully${NC}"

# ============================================================================
# STEP 4: Export App
# ============================================================================
echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}STEP 4: Exporting App${NC}"
echo -e "${BLUE}========================================${NC}"

# Create export options plist for app distribution
APP_EXPORT_OPTIONS="${BUILD_DIR}/${BUILD_CONFIG}/AppExportOptions.plist"

if [ "$BUILD_RELEASE" = true ]; then
    # Release build - export for distribution
    cat > "$APP_EXPORT_OPTIONS" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>method</key>
    <string>developer-id</string>
    <key>teamID</key>
    <string>${TEAM_ID}</string>
    <key>signingStyle</key>
    <string>automatic</string>
    <key>signingCertificate</key>
    <string>Developer ID Application</string>
</dict>
</plist>
EOF
else
    # Debug build - export for development
    cat > "$APP_EXPORT_OPTIONS" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>method</key>
    <string>development</string>
    <key>teamID</key>
    <string>${TEAM_ID}</string>
    <key>signingStyle</key>
    <string>automatic</string>
</dict>
</plist>
EOF
fi

# Export archive for distribution (like Xcode "Distribute App")
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

# ============================================================================
# STEP 5: Notarize App (if requested)
# ============================================================================
if [ "$NOTARIZE" = true ]; then
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}STEP 5: Notarizing App${NC}"
    echo -e "${BLUE}========================================${NC}"

    # Create a zip for app notarization
    echo -e "${YELLOW}Creating zip of app for notarization...${NC}"
    APP_ZIP="${BUILD_DIR}/${BUILD_CONFIG}/app.zip"
    ditto -c -k --keepParent "$EXPORTED_APP" "$APP_ZIP"

    # Submit app for notarization
    echo -e "${YELLOW}Submitting app to Apple for notarization...${NC}"
    echo -e "${YELLOW}This may take several minutes...${NC}"

    # Capture notarization output
    NOTARY_OUTPUT=$(xcrun notarytool submit "${APP_ZIP}" \
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
            echo "  xcrun notarytool log ${SUBMISSION_ID} --keychain-profile '$KEYCHAIN_PROFILE'"
            echo ""
        fi
        echo "Common issues:"
        echo "- Missing code signature"
        echo "- Invalid or expired certificate"
        echo "- Unsigned binaries in the bundle"
        echo "- Missing entitlements"
        echo "- Embedded screensaver not properly signed"
        echo ""
        echo "The app has been built and exported."
        echo "To retry notarization after fixing issues:"
        echo "1. ditto -c -k --keepParent '${EXPORTED_APP}' '${APP_ZIP}'"
        echo "2. xcrun notarytool submit '${APP_ZIP}' --keychain-profile '$KEYCHAIN_PROFILE' --wait"
        echo "3. xcrun stapler staple '${EXPORTED_APP}'"
        rm "${APP_ZIP}"
        exit 1
    fi

    # Check if submission failed entirely
    if ! echo "$NOTARY_OUTPUT" | grep -q "status: Accepted"; then
        echo ""
        echo -e "${RED}✗ App notarization failed${NC}"
        echo ""
        echo "The app has been built but notarization failed."
        echo "You can try notarizing manually or check your credentials."
        echo ""
        echo "To notarize manually:"
        echo "1. xcrun notarytool submit '${APP_ZIP}' --keychain-profile '$KEYCHAIN_PROFILE' --wait"
        echo "2. xcrun stapler staple '${EXPORTED_APP}'"
        rm "${APP_ZIP}"
        exit 1
    fi

    echo -e "${GREEN}✓ App notarization successful${NC}"

    # Staple the notarization ticket to app
    echo -e "${YELLOW}Stapling notarization ticket to app...${NC}"
    if ! xcrun stapler staple "$EXPORTED_APP"; then
        echo -e "${RED}✗ Failed to staple notarization ticket to app${NC}"
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

    # Clean up app zip
    rm "${APP_ZIP}"
fi

# ============================================================================
# STEP 6: Create Final Distribution Package
# ============================================================================
echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}STEP 6: Creating Distribution Package${NC}"
echo -e "${BLUE}========================================${NC}"

# Create distribution ZIP if everything succeeded
# Use version if provided, otherwise use date
if [ -n "$VERSION" ]; then
    FINAL_ZIP="${BUILD_DIR}/${BUILD_CONFIG}/infinidream-${VERSION}.zip"
else
    FINAL_ZIP="${BUILD_DIR}/${BUILD_CONFIG}/infinidream-$(date +%Y%m%d).zip"
fi
echo -e "${YELLOW}Creating final distribution package...${NC}"
ditto -c -k --keepParent "$EXPORTED_APP" "$FINAL_ZIP"

# Get file sizes
SAVER_SIZE=$(du -sh "${OUTPUT_SAVER}" | cut -f1)
APP_SIZE=$(du -sh "$EXPORTED_APP" | cut -f1)
ZIP_SIZE=$(du -h "$FINAL_ZIP" | cut -f1)

# ============================================================================
# STEP 7: Generate Appcast (if requested)
# ============================================================================
APPCAST_PATH=""
if [ "$GENERATE_APPCAST" = true ]; then
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}STEP 7: Generating Appcast${NC}"
    echo -e "${BLUE}========================================${NC}"

    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    APPCAST_SCRIPT="${SCRIPT_DIR}/generate_appcast.sh"

    if [ ! -x "$APPCAST_SCRIPT" ]; then
        echo -e "${RED}Error: generate_appcast.sh not found or not executable${NC}"
        echo "Expected at: $APPCAST_SCRIPT"
    else
        echo -e "${YELLOW}Generating appcast.xml...${NC}"
        if "$APPCAST_SCRIPT" -v "$VERSION" -z "$FINAL_ZIP" -o "${BUILD_DIR}/${BUILD_CONFIG}"; then
            APPCAST_PATH="${BUILD_DIR}/${BUILD_CONFIG}/appcast.xml"
            echo -e "${GREEN}✓ Appcast generated successfully${NC}"
        else
            echo -e "${YELLOW}⚠ Appcast generation failed (continuing without appcast)${NC}"
        fi
    fi
fi

# ============================================================================
# Summary
# ============================================================================
echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Build Complete! 🎉${NC}"
echo -e "${GREEN}========================================${NC}"
if [ "$BUILD_STAGE" = true ]; then
    echo "Environment: STAGE"
else
    echo "Environment: PRODUCTION"
fi
echo "Configuration: ${BUILD_CONFIG}"
if [ -n "$VERSION" ]; then
    echo "Version: ${VERSION}"
fi
echo ""
echo "Build outputs:"
echo "  Screensaver (DerivedData): ${PRODUCTS_DIR}/${SCREENSAVER_NAME} (${SAVER_SIZE})"
echo "  Screensaver (output): ${OUTPUT_SAVER}"
echo "  Screensaver (Resources): ${PROJECT_SCREENSAVER}"
echo "  Screensaver zip: ${SCREENSAVER_ZIP}"
echo "  App archive: ${APP_ARCHIVE_PATH}"
echo "  App (exported): ${EXPORTED_APP} (${APP_SIZE})"
echo ""
if [ "$NOTARIZE" = true ]; then
    echo "Notarization: ✓ Complete (screensaver and app)"
else
    echo "Notarization: Skipped"
fi
echo ""
echo "Distribution package: ${FINAL_ZIP} (${ZIP_SIZE})"
if [ -n "$APPCAST_PATH" ] && [ -f "$APPCAST_PATH" ]; then
    echo "Appcast: ${APPCAST_PATH}"
fi
echo ""
echo "To test the app, run:"
echo "  open '${EXPORTED_APP}'"
echo ""

# Show upload instructions if appcast was generated
if [ -n "$APPCAST_PATH" ] && [ -f "$APPCAST_PATH" ]; then
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}Upload Instructions for Sparkle Updates${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""
    echo "1. Create GitHub release for version ${VERSION}:"
    echo "   https://github.com/e-dream-ai/client/releases/new?tag=${VERSION}"
    echo ""
    echo "2. Upload the zip file to the release:"
    echo "   ${FINAL_ZIP}"
    echo ""
    echo "3. Upload appcast.xml to your web server:"
    echo "   ${APPCAST_PATH} → https://infinidream.ai/alpha/appcast.xml"
    echo ""
fi

if [ "$BUILD_RELEASE" = true ] && [ "$NOTARIZE" = false ]; then
    echo -e "${YELLOW}Note: This is a Release build without notarization.${NC}"
    echo -e "${YELLOW}To notarize, run: ./build.sh -r -n$([ "$BUILD_STAGE" = true ] && echo " -s")${NC}"
    echo ""
fi
