#!/bin/bash
set -e

# infinidream Appcast Generator
# Uses Sparkle's generate_appcast tool to create appcast.xml
#
# Usage: ./generate_appcast.sh -v VERSION -z ZIP_PATH [-o OUTPUT_DIR]
#
# Example: ./generate_appcast.sh -v 0.12.0 -z build/Release/infinidream-0.12.0.zip

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default configuration
VERSION=""
ZIP_PATH=""
OUTPUT_DIR=""
DOWNLOAD_URL_PREFIX="https://github.com/e-dream-ai/client/releases/download"

# Parse command line arguments
while getopts "v:z:o:" opt; do
    case ${opt} in
        v )
            VERSION="$OPTARG"
            ;;
        z )
            ZIP_PATH="$OPTARG"
            ;;
        o )
            OUTPUT_DIR="$OPTARG"
            ;;
        \? )
            echo "Usage: $0 -v VERSION -z ZIP_PATH [-o OUTPUT_DIR]"
            echo "  -v : Version string (e.g., 0.12.0)"
            echo "  -z : Path to the zip file"
            echo "  -o : Output directory for appcast.xml (optional, defaults to zip directory)"
            exit 1
            ;;
    esac
done

# Validate required arguments
if [ -z "$VERSION" ]; then
    echo -e "${RED}Error: Version (-v) is required${NC}"
    exit 1
fi

if [ -z "$ZIP_PATH" ]; then
    echo -e "${RED}Error: Zip path (-z) is required${NC}"
    exit 1
fi

if [ ! -f "$ZIP_PATH" ]; then
    echo -e "${RED}Error: Zip file not found: $ZIP_PATH${NC}"
    exit 1
fi

# Set output directory
if [ -z "$OUTPUT_DIR" ]; then
    OUTPUT_DIR=$(dirname "$ZIP_PATH")
fi

# Get absolute paths
ZIP_PATH=$(cd "$(dirname "$ZIP_PATH")" && pwd)/$(basename "$ZIP_PATH")
OUTPUT_DIR=$(cd "$OUTPUT_DIR" && pwd)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}infinidream Appcast Generator${NC}"
echo -e "${BLUE}========================================${NC}"
echo "Version: $VERSION"
echo "Zip file: $ZIP_PATH"
echo "Output directory: $OUTPUT_DIR"
echo ""

# Find generate_appcast tool
GENERATE_APPCAST=""

# Check common locations
if [ -x "/usr/local/bin/generate_appcast" ]; then
    GENERATE_APPCAST="/usr/local/bin/generate_appcast"
elif [ -x "$HOME/bin/generate_appcast" ]; then
    GENERATE_APPCAST="$HOME/bin/generate_appcast"
elif [ -x "$SCRIPT_DIR/bin/generate_appcast" ]; then
    GENERATE_APPCAST="$SCRIPT_DIR/bin/generate_appcast"
elif command -v generate_appcast &> /dev/null; then
    GENERATE_APPCAST="generate_appcast"
fi

if [ -z "$GENERATE_APPCAST" ]; then
    echo -e "${RED}Error: generate_appcast tool not found${NC}"
    echo ""
    echo "Please download Sparkle tools from:"
    echo "  https://github.com/sparkle-project/Sparkle/releases"
    echo ""
    echo "Extract and place generate_appcast in one of these locations:"
    echo "  - /usr/local/bin/generate_appcast"
    echo "  - ~/bin/generate_appcast"
    echo "  - $SCRIPT_DIR/bin/generate_appcast"
    echo ""
    echo "Or add it to your PATH."
    exit 1
fi

echo "Using generate_appcast: $GENERATE_APPCAST"

# Create a temporary directory for appcast generation
APPCAST_DIR=$(mktemp -d)
trap "rm -rf $APPCAST_DIR" EXIT

# Copy zip to temp directory
cp "$ZIP_PATH" "$APPCAST_DIR/"

# Get zip filename
ZIP_FILENAME=$(basename "$ZIP_PATH")

# Construct download URL
DOWNLOAD_URL="${DOWNLOAD_URL_PREFIX}/${VERSION}/${ZIP_FILENAME}"
echo "Download URL: $DOWNLOAD_URL"

# Generate appcast using Sparkle's tool
echo ""
echo -e "${YELLOW}Generating appcast with Sparkle's generate_appcast...${NC}"

# Run generate_appcast with download URL prefix
"$GENERATE_APPCAST" \
    --download-url-prefix "${DOWNLOAD_URL_PREFIX}/${VERSION}/" \
    "$APPCAST_DIR"

# Check if appcast was generated
GENERATED_APPCAST="$APPCAST_DIR/appcast.xml"
if [ ! -f "$GENERATED_APPCAST" ]; then
    echo -e "${RED}Error: generate_appcast did not create appcast.xml${NC}"
    echo "Contents of temp directory:"
    ls -la "$APPCAST_DIR"
    exit 1
fi

# Copy appcast to output directory
cp "$GENERATED_APPCAST" "$OUTPUT_DIR/appcast.xml"
APPCAST_PATH="$OUTPUT_DIR/appcast.xml"

echo -e "${GREEN}✓ appcast.xml generated: $APPCAST_PATH${NC}"

# Show appcast contents
echo ""
echo -e "${BLUE}Generated appcast.xml:${NC}"
cat "$APPCAST_PATH"

# Summary
echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Appcast Generation Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Files ready for upload:"
echo "  1. Zip file: $ZIP_PATH"
echo "     → Upload to: $DOWNLOAD_URL"
echo ""
echo "  2. Appcast: $APPCAST_PATH"
echo "     → Upload to: https://infinidream.ai/appcast.xml"
echo ""
echo -e "${YELLOW}Upload Instructions:${NC}"
echo "  1. Create GitHub release for version ${VERSION}"
echo "  2. Upload ${ZIP_FILENAME} to the release"
echo "  3. Upload appcast.xml to your web server at https://infinidream.ai/appcast.xml"
