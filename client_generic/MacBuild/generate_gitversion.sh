#!/bin/bash

# Get the Git hash
GIT_HASH=$(git rev-parse HEAD)
DATE=$(date -u)

# Create the gitversion.h file
echo "#define GIT_VERSION \"$GIT_HASH\"" > gitversion.h
echo "#define BUILD_DATE \"$DATE\"" >> gitversion.h
