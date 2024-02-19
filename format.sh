#!/bin/sh

find . -name '*.cpp' -o -name '*.h' -o -name '*.m' -o -name '*.mm' -name '*.metal' \
| grep -v "./client_generic/boost/*" \
| grep -v "./client_generic/tinyXml/xmltest.cp*" \
| grep -v "./client_generic/ffmpeg/*" \
| grep -v "./client_generic/curl/*" \
| grep -v "./client_generic/curlTest/*" \
| grep -v "./client_generic/openssl-1.0.2k/*" \
| grep -v "./client_generic/zlib/*" \
| grep -v "./client_generic/openssl*" \
| grep -v "./client_generic/tinyXml/*" \
| grep -v "./client_generic/libxml/*" \
| grep -v "./client_generic/libpng/*" \
| grep -v "./client_generic/lua5.1/*" \
| grep -v "./client_generic/MacBuild/Frameworks/*" \
| grep -v "./client_generic/MacBuild/build" \
| grep -v "./client_generic/wxWidgets/*" \
| xargs -n1 clang-format -i 
