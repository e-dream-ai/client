#!/bin/sh


# Tag-relative revision with a -dirty suffix for uncommitted changes (#662),
# e.g. 0.16.1-11-gedfd089a-dirty. Shown in the log banner, OSD and settings.
GIT_REV=`git describe --tags --always --dirty`
BUILD_DATE=`date -u`

json_content='{
  "REVISION": "'"$GIT_REV"'",
  "BUILD_DATE": "'"$BUILD_DATE"'"
}'

file_path='BuildData.json'

# Write JSON content to the file
echo "$json_content" > "$file_path"