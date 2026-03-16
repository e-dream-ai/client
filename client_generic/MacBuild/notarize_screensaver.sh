#!/bin/bash
set -e

# infinidream Screensaver Notarization Script (Step 2)
# Takes the screensaver.zip from Step 1 and notarizes it
# Outputs a notarized and stapled screensaver

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
BUILD_CONFIG="Release"
BUILD_DIR="build"
DERIVED_DATA="${BUILD_DIR}/DerivedData"
PRODUCTS_DIR="${DERIVED_DATA}/Build/Products/${BUILD_CONFIG}"
INPUT_ZIP="${BUILD_DIR}/${BUILD_CONFIG}/screensaver.zip"
SCREENSAVER_NAME="infinidream.saver"
PROJECT_SCREENSAVER="Resources/${SCREENSAVER_NAME}"
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
echo -e "${BLUE}infinidream Screensaver Notarization (Step 2)${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "Input ZIP: ${INPUT_ZIP}"
echo -e "Project Screensaver: ${PROJECT_SCREENSAVER}"
echo -e "Keychain Profile: ${KEYCHAIN_PROFILE}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check if input zip exists
if [ ! -f "${INPUT_ZIP}" ]; then
    echo -e "${RED}Error: Screensaver zip not found at ${INPUT_ZIP}${NC}"
    echo -e "${RED}Please run ./build_screensaver.sh first${NC}"
    exit 1
fi

# Check if screensaver exists in project Resources
if [ ! -d "${PROJECT_SCREENSAVER}" ]; then
    echo -e "${RED}Error: Screensaver not found at ${PROJECT_SCREENSAVER}${NC}"
    echo -e "${RED}Please run ./build_screensaver.sh first${NC}"
    exit 1
fi

# Submit for notarization
echo -e "${YELLOW}Submitting screensaver to Apple for notarization...${NC}"
echo -e "${YELLOW}This may take several minutes...${NC}"

# Capture notarization output
NOTARY_OUTPUT=$(xcrun notarytool submit "${INPUT_ZIP}" \
                 --keychain-profile "${KEYCHAIN_PROFILE}" \
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
        echo "  xcrun notarytool log ${SUBMISSION_ID} --keychain-profile '${KEYCHAIN_PROFILE}'"
        echo ""
    fi
    echo "Common issues:"
    echo "- Missing code signature"
    echo "- Invalid or expired certificate"
    echo "- Unsigned binaries in the bundle"
    echo "- Missing entitlements"
    echo ""
    echo "Troubleshooting tips:"
    echo "1. Check the notarization log for details"
    echo "2. Ensure all binaries are signed with a Developer ID certificate"
    echo "3. Check that the Team ID in the notarization profile matches your certificate"
    exit 1
fi

# Check if submission failed entirely
if ! echo "$NOTARY_OUTPUT" | grep -q "status: Accepted"; then
    echo ""
    echo -e "${RED}✗ Screensaver notarization failed${NC}"
    echo ""
    echo "Troubleshooting tips:"
    echo "1. Ensure keychain profile '${KEYCHAIN_PROFILE}' is configured:"
    echo "   xcrun notarytool store-credentials '${KEYCHAIN_PROFILE}' \\"
    echo "   --apple-id YOUR_APPLE_ID --team-id YOUR_TEAM_ID"
    echo ""
    echo "2. Check notarization history:"
    echo "   xcrun notarytool history --keychain-profile '${KEYCHAIN_PROFILE}'"
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

# Summary
echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Screensaver Notarization Complete! 🎉${NC}"
echo -e "${GREEN}========================================${NC}"
echo "Notarized screensaver: ${PROJECT_SCREENSAVER}"
echo ""
echo "Next steps:"
echo "Run ./build_app_with_screensaver.sh to build the app with the notarized screensaver"
echo ""