#!/usr/bin/env python3
"""Build sio_teardown_test with AddressSanitizer and run each scenario against a
local wss:// server using a throwaway self-signed CA.

    ./run.py [iterations]                 (default 200)
    ./run.py 500 --scenarios watchdog     to run a subset (or SCENARIOS="watchdog")

macOS: clang++ against the static vcpkg deps in cache/univ-osx.
Linux: g++ (or $CXX) against system Boost.JSON and OpenSSL.
Windows: MSVC (found with vswhere) against vcpkg_installed/x64-windows.
Uses the mock server's venv for aiohttp. Exits nonzero on the first failure.
"""
import argparse
import glob
import os
import platform
import shutil
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_GENERIC = os.path.dirname(os.path.dirname(HERE))
REPO = os.path.dirname(CLIENT_GENERIC)
SIO = os.path.join(CLIENT_GENERIC, "Networking", "sio")
STUB = os.path.join(HERE, "stub")
PORT = 8766
SYSTEM = platform.system()


def run(cmd, **kwargs):
    subprocess.run(cmd, check=True, **kwargs)


def find_openssl():
    found = shutil.which("openssl")
    if not found and SYSTEM == "Windows":
        # Git for Windows ships one.
        for candidate in (r"C:\Program Files\Git\usr\bin\openssl.exe",
                          r"C:\Program Files\Git\mingw64\bin\openssl.exe"):
            if os.path.exists(candidate):
                return candidate
    if not found:
        sys.exit("openssl not found on PATH")
    return found


def make_certs(work):
    """CA + localhost leaf cert. The client's OpenSSL reads SSL_CERT_FILE through
    set_default_verify_paths(), so trusting the CA needs no code change."""
    openssl = find_openssl()
    quiet = {"stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL}
    p = lambda name: os.path.join(work, name)
    run([openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
         "-subj", "/CN=sio-teardown CA", "-keyout", p("ca.key"), "-out", p("ca.pem")], **quiet)
    run([openssl, "req", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=localhost",
         "-keyout", p("leaf.key"), "-out", p("leaf.csr")], **quiet)
    with open(p("ext.cnf"), "w") as f:
        f.write("subjectAltName=DNS:localhost,IP:127.0.0.1\n")
    run([openssl, "x509", "-req", "-in", p("leaf.csr"), "-CA", p("ca.pem"), "-CAkey", p("ca.key"),
         "-CAcreateserial", "-days", "1", "-extfile", p("ext.cnf"), "-out", p("leaf.pem")], **quiet)


def msvc_env():
    """The environment vcvars64.bat sets up (cl, link, and the ASan runtime DLL on PATH)."""
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.exists(vswhere):
        sys.exit("vswhere.exe not found; install Visual Studio 2022 with the C++ workload")
    install = subprocess.check_output(
        [vswhere, "-latest", "-products", "*", "-requires",
         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
        text=True).strip()
    vcvars = os.path.join(install, "VC", "Auxiliary", "Build", "vcvars64.bat")
    out = subprocess.check_output(f'"{vcvars}" >nul 2>&1 && set', shell=True, text=True)
    # Upper-case the names: cmd reports PATH as "Path".
    return {k.upper(): v for k, v in (line.split("=", 1) for line in out.splitlines() if "=" in line)}


def build(work):
    """Returns (path to the test binary, environment to run it in)."""
    env = dict(os.environ)
    sources = [os.path.join(SIO, "sio_beast.cpp"), os.path.join(HERE, "sio_teardown_test.cpp")]
    if SYSTEM == "Windows":
        env = msvc_env()
        deps = os.path.join(REPO, "vcpkg_installed", "x64-windows")
        boost_json = glob.glob(os.path.join(deps, "lib", "boost_json-*.lib"))
        if not boost_json:
            sys.exit(f"Boost.JSON not found in {deps}; run vcpkg install --triplet x64-windows")
        exe = os.path.join(work, "sio_teardown_test.exe")
        cl = shutil.which("cl", path=env["PATH"])  # subprocess searches the caller's PATH
        if not cl:
            sys.exit("cl.exe not found after vcvars64.bat")
        run([cl, "/nologo", "/std:c++17", "/EHsc", "/MD", "/Zi", "/O1", "/fsanitize=address",
             "/bigobj", "/D_WIN32_WINNT=0x0A00", "/DWIN32_LEAN_AND_MEAN",
             "/I" + STUB, "/I" + SIO, "/I" + os.path.join(deps, "include"),
             *sources, os.path.join(STUB, "winstore_shim.cpp"), "/Fe:" + exe,
             "/link", "/LIBPATH:" + os.path.join(deps, "lib"),
             os.path.basename(boost_json[0]), "libssl.lib", "libcrypto.lib",
             "ws2_32.lib", "mswsock.lib", "crypt32.lib"],
            cwd=work, env=env, stdout=subprocess.DEVNULL)
        env["PATH"] = os.path.join(deps, "bin") + os.pathsep + env["PATH"]
        return exe, env

    if SYSTEM == "Darwin":
        deps = os.path.join(REPO, "cache", "univ-osx")
        cxx = os.environ.get("CXX", "clang++")
        incs = ["-I" + os.path.join(deps, "include")]
        libs = [os.path.join(deps, "lib", name)
                for name in ("libboost_json.a", "libssl.a", "libcrypto.a")]
    else:
        cxx = os.environ.get("CXX", "g++")
        incs = []
        libs = ["-lboost_json", "-lssl", "-lcrypto", "-pthread"]
    exe = os.path.join(work, "sio_teardown_test")
    run([cxx, "-std=c++17", "-g", "-O1", "-fsanitize=address", "-fno-omit-frame-pointer",
         "-I" + STUB, "-I" + SIO, *incs, *sources, *libs, "-o", exe])
    return exe, env


def server_python():
    venv = os.path.join(CLIENT_GENERIC, "tests", "mock-server", "venv")
    for candidate in (os.path.join(venv, "bin", "python"),
                      os.path.join(venv, "Scripts", "python.exe")):
        if os.path.exists(candidate):
            return candidate
    sys.exit(f"mock server venv not found at {venv}; see tests/mock-server/README.md")


def wait_for_port(server, timeout=10):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if server.poll() is not None:
            return False
        try:
            socket.create_connection(("127.0.0.1", PORT), timeout=0.5).close()
            return True
        except OSError:
            time.sleep(0.1)
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("iterations", nargs="?", type=int, default=200)
    parser.add_argument("--scenarios", nargs="+",
                        default=os.environ.get("SCENARIOS", "close reconnect watchdog").split())
    args = parser.parse_args()

    work = tempfile.mkdtemp(prefix="sio-teardown.")
    server = None
    try:
        make_certs(work)
        exe, env = build(work)

        # Output goes to a file: an inherited pipe would keep a caller's `| tail` open.
        server_log = open(os.path.join(work, "server.log"), "w")
        server = subprocess.Popen(
            [server_python(), os.path.join(HERE, "server.py"), "--port", str(PORT),
             "--cert", os.path.join(work, "leaf.pem"), "--key", os.path.join(work, "leaf.key")],
            stdout=server_log, stderr=subprocess.STDOUT)
        if not wait_for_port(server):
            server_log.flush()
            with open(server_log.name) as f:
                sys.stderr.write(f.read())
            sys.exit("server.py did not start")

        env["SSL_CERT_FILE"] = os.path.join(work, "ca.pem")
        env["ASAN_OPTIONS"] = "halt_on_error=1:detect_leaks=0"
        for scenario in args.scenarios:
            for flood in ([], ["flood"]):
                cmd = [exe, str(PORT), scenario, str(args.iterations), *flood]
                if subprocess.run(cmd, env=env).returncode != 0:
                    sys.exit(f"FAIL: {scenario} {' '.join(flood)}".rstrip())
    finally:
        if server:
            server.terminate()
            server.wait()
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
