#!/bin/bash
# Build sio_teardown_test with AddressSanitizer and run each scenario against
# a local wss:// server using a throwaway self-signed CA.
#   ./run.sh [iterations]        (default 200)
#   SCENARIOS="watchdog" ./run.sh   to run a subset
# Uses the mock server's venv for aiohttp. Exits nonzero on the first failure.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
client_generic="$(cd "$here/../.." && pwd)"
python="$client_generic/tests/mock-server/venv/bin/python"
iterations="${1:-200}"
port=8766
work="$(mktemp -d "${TMPDIR:-/tmp}/sio-teardown.XXXXXX")"
trap 'kill $server_pid 2>/dev/null || true; rm -rf "$work"' EXIT

# CA + localhost leaf cert. OpenSSL in the client reads SSL_CERT_FILE through
# set_default_verify_paths(), so trusting the CA needs no code change.
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj "/CN=sio-teardown CA" \
    -keyout "$work/ca.key" -out "$work/ca.pem" 2>/dev/null
openssl req -newkey rsa:2048 -nodes -subj "/CN=localhost" \
    -keyout "$work/leaf.key" -out "$work/leaf.csr" 2>/dev/null
printf 'subjectAltName=DNS:localhost,IP:127.0.0.1\n' > "$work/ext.cnf"
openssl x509 -req -in "$work/leaf.csr" -CA "$work/ca.pem" -CAkey "$work/ca.key" \
    -CAcreateserial -days 1 -extfile "$work/ext.cnf" -out "$work/leaf.pem" 2>/dev/null

# macOS: clang++ against the static vcpkg deps in cache/univ-osx.
# Linux: g++ (or $CXX) against system Boost.JSON and OpenSSL.
if [ "$(uname)" = Darwin ]; then
    deps="$(cd "$client_generic/.." && pwd)/cache/univ-osx"
    cxx="${CXX:-clang++}"
    incs=(-I"$deps/include")
    libs=("$deps/lib/libboost_json.a" "$deps/lib/libssl.a" "$deps/lib/libcrypto.a")
else
    cxx="${CXX:-g++}"
    incs=()
    libs=(-lboost_json -lssl -lcrypto -pthread)
fi
"$cxx" -std=c++17 -g -O1 -fsanitize=address -fno-omit-frame-pointer \
    -I"$here/stub" -I"$client_generic/Networking/sio" ${incs[@]+"${incs[@]}"} \
    "$client_generic/Networking/sio/sio_beast.cpp" "$here/sio_teardown_test.cpp" \
    "${libs[@]}" -o "$work/sio_teardown_test"

"$python" "$here/server.py" --port $port --cert "$work/leaf.pem" --key "$work/leaf.key" &
server_pid=$!
for _ in $(seq 50); do nc -z 127.0.0.1 $port 2>/dev/null && break; sleep 0.1; done

export SSL_CERT_FILE="$work/ca.pem"
export ASAN_OPTIONS="halt_on_error=1:detect_leaks=0"
scenarios="${SCENARIOS:-close reconnect watchdog}"
for scenario in $scenarios; do
    "$work/sio_teardown_test" $port $scenario "$iterations"
    "$work/sio_teardown_test" $port $scenario "$iterations" flood
done
