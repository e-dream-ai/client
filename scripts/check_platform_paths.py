#!/usr/bin/env python
"""Fail if cross-platform sources hardcode a platform-specific path.

Shared code must *ask* for platform facts rather than *state* them:

    g_Settings()->ConfigPath()   -- where this platform's settings.json lives
    PlatformUtils::GetWorkingDir(), GetAppPath(), ...

A hardcoded "~/.config/infinidream/settings.json" compiles fine everywhere and
is wrong on two platforms out of three, with no build-time signal. That is the
failure this check exists to catch -- see issue #675 and the "Platform-specific
code" section of AGENTS.md.

Run from anywhere:  python scripts/check_platform_paths.py
"""

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Directories compiled into every platform's build.
SHARED_DIRS = [
    "client_generic/Client",
    "client_generic/Common",
    "client_generic/Networking",
    "client_generic/ContentDecoder",
    "client_generic/ContentDownloader",
    "client_generic/DisplayOutput",
    "client_generic/TupleStorage",
]

SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".mm", ".inl"}

# Vendored trees that live under the shared dirs; not ours to police.
VENDOR_PARTS = {"imgui", "websocketpp", "socket.io-client-cpp", "tinyXml", "rapidjson"}

# Files that are *allowed* to name a platform path, with the reason why.
ALLOWLIST = {
    # The per-platform shims: defining these paths is precisely their job.
    "client_generic/Client/client_win32.h": "defines m_AppData for Windows",
    "client_generic/Client/client_mac.h": "defines m_AppData for macOS",
    "client_generic/Client/client_linux.h": "defines m_AppData for Linux",
    # Dead code: the DEBUG_LOG macro sits inside an `#if 0` block.
    "client_generic/Common/Log.h": "DEBUG_LOG macro is disabled behind #if 0",
}

PATTERNS = [
    (re.compile(r"~/\.config/"), "Linux XDG config path"),
    (re.compile(r"\.config/infinidream"), "Linux config directory"),
    (re.compile(r"/Users/Shared/"), "macOS shared data path"),
    (re.compile(r"%LOCALAPPDATA%", re.IGNORECASE), "Windows LocalAppData path"),
    (re.compile(r"%APPDATA%", re.IGNORECASE), "Windows AppData path"),
    (re.compile(r"%ProgramData%", re.IGNORECASE), "Windows ProgramData path"),
    (re.compile(r"AppData[\\/]{1,2}Local"), "Windows AppData path"),
]

COMMENT_START = ("//", "*", "/*")


def is_comment(line):
    return line.lstrip().startswith(COMMENT_START)


def iter_shared_sources():
    for shared_dir in SHARED_DIRS:
        root = REPO_ROOT / shared_dir
        if not root.is_dir():
            continue

        for path in sorted(root.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES:
                continue
            if VENDOR_PARTS.intersection(path.relative_to(REPO_ROOT).parts):
                continue

            yield path


def main():
    findings = []

    for path in iter_shared_sources():
        rel = path.relative_to(REPO_ROOT).as_posix()
        if rel in ALLOWLIST:
            continue

        text = path.read_text(encoding="utf-8", errors="replace")
        for lineno, line in enumerate(text.splitlines(), start=1):
            # A path in prose is documentation, not behaviour.
            if is_comment(line):
                continue

            for pattern, description in PATTERNS:
                if pattern.search(line):
                    findings.append((rel, lineno, description, line.strip()))
                    break

    if not findings:
        return 0

    print("Hardcoded platform-specific paths in shared code:\n")
    for rel, lineno, description, snippet in findings:
        print(f"  {rel}:{lineno}  ({description})")
        print(f"    {snippet}\n")

    print(
        "Shared code must not name a platform's paths. Use "
        "g_Settings()->ConfigPath() for the settings file, or add a "
        "PlatformUtils method and implement it for all three platforms.\n"
        "If a hit is genuinely correct, add the file to ALLOWLIST in "
        "scripts/check_platform_paths.py with a reason."
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
