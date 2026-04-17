#!/bin/sh

find . -name '*.cpp' -o -name '*.h' -o -name '*.m' -o -name '*.mm' -o -name '*.metal' \
| grep -v "./client_generic/socket.io-client-cpp/*" \
| grep -v "./client_generic/MacBuild/Frameworks/*" \
| grep -v "./client_generic/MacBuild/build" \
| xargs -n1 clang-format -i
