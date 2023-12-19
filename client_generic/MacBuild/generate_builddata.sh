#!/bin/sh


GIT_REV=`git rev-parse --short HEAD`
BUILD_DATE=`date -u`

json_content='{
  "REVISION": "'"$GIT_REV"'",
  "BUILD_DATE": "'"$BUILD_DATE"'"
}'

file_path='BuildData.json'

# Write JSON content to the file
echo "$json_content" > "$file_path"