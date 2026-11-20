#!/usr/bin/env python3
"""
Windows packaging: build an NSIS Setup.exe directly from the MSBuild output
directory (client_generic/MSVC/<Configuration>/).

Structure and CLI style align with client_generic/MacBuild/release.py; there
is no Sparkle appcast on Windows.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional


class Colors:
    RED = "\033[0;31m"
    GREEN = "\033[0;32m"
    YELLOW = "\033[1;33m"
    BLUE = "\033[0;34m"
    NC = "\033[0m"


def print_color(color: str, message: str) -> None:
    # Flush so messages stay in order with signtool's and makensis's output when piped.
    print(f"{color}{message}{Colors.NC}", flush=True)


def print_red(message: str) -> None:
    print_color(Colors.RED, message)


def print_green(message: str) -> None:
    print_color(Colors.GREEN, message)


def print_yellow(message: str) -> None:
    print_color(Colors.YELLOW, message)


def print_blue(message: str) -> None:
    print_color(Colors.BLUE, message)


def run_command(
    cmd: list[str],
    *,
    check: bool = True,
    capture_output: bool = False,
    env: Optional[dict[str, str]] = None,
    cwd: Optional[str | Path] = None,
) -> subprocess.CompletedProcess[str]:
    merged: dict[str, str] = dict(os.environ)
    if env:
        merged.update(env)
    try:
        return subprocess.run(
            cmd,
            check=check,
            capture_output=capture_output,
            text=True,
            env=merged,
            cwd=cwd,
        )
    except FileNotFoundError:
        print_red(f"Command not found: {cmd[0]}")
        raise


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parent.parent.parent


def winbuild_dir(repo: Path) -> Path:
    return repo / "client_generic" / "WinBuild"


def msvc_out_dir(repo: Path, configuration: str) -> Path:
    return repo / "client_generic" / "MSVC" / configuration


def installer_dir(repo: Path) -> Path:
    return repo / "client_generic" / "InstallerMSVC"


def normalize_version(value: str) -> str:
    """'v0.16.2' -> '0.16.2', as build.py does."""
    v = value.strip()
    if v.lower().startswith("v") and len(v) > 1 and (v[1].isdigit() or v[1] == "."):
        v = v[1:].lstrip()
    return v


def built_version(repo: Path) -> tuple[Optional[str], Optional[str]]:
    """The version compiled into the binaries and where it came from ("cli" for
    build.py -v, "git-tag" for a dev build), from the header
    generate_build_version.py writes."""
    header = repo / "client_generic" / "MSVC" / "edream_build_version_generated.h"
    if not header.is_file():
        return None, None
    text = header.read_text(encoding="utf-8", errors="replace")

    def define(name: str) -> Optional[str]:
        m = re.search(rf'#define\s+{name}\s+"([^"]*)"', text)
        return m.group(1) if m else None

    return define("EDREAM_BUILD_VERSION_SEMVER"), define("EDREAM_BUILD_VERSION_SOURCE")


def resolve_version(repo: Path, requested: Optional[str]) -> str:
    """The installer's version must match the one the app reports (Settings, About, API),
    and must be a release version given to build.py, not a dev build's."""
    built, source = built_version(repo)
    if not built or source != "cli":
        print_red(
            "These binaries weren't built with a release version"
            + (f" (they say {built}, from the latest git tag)" if built else "")
            + ".\nBuild with: python client_generic/WinBuild/build.py -v X.Y.Z"
        )
        sys.exit(1)
    if requested is None:
        print_blue(f"Using the version the binaries were built with: {built}")
        return built
    version = normalize_version(requested)
    if built and built != version:
        print_red(
            f"The binaries were built as {built}, but the installer would say {version}.\n"
            f"Rebuild with: python client_generic/WinBuild/build.py -v {version}\n"
            f"or package as built by leaving out -v."
        )
        sys.exit(1)
    return version


def ensure_dir(p: Path) -> None:
    p.mkdir(parents=True, exist_ok=True)


def check_msvc_output(repo: Path, configuration: str) -> None:
    out = msvc_out_dir(repo, configuration)
    if not out.is_dir():
        print_red(f"Build output directory not found: {out}")
        print_red("Run: python client_generic/WinBuild/build.py")
        sys.exit(1)
    debug = configuration.lower().startswith("debug")
    exe = out / ("infinidreamd.exe" if debug else "infinidream.exe")
    if not exe.is_file():
        print_red(f"Missing executable: {exe}")
        sys.exit(1)
    scr = out / ("infinidreamd.scr" if debug else "infinidream.scr")
    if not scr.is_file():
        print_yellow(f"Missing screensaver binary: {scr} (PostBuild copy may have been skipped)")


def find_signtool() -> Optional[Path]:
    """The newest Windows SDK signtool.exe, x64 first: Artifact Signing's dlib is x64 and
    only loads into an x64 signtool."""
    env = os.environ.get("SIGNTOOL")
    if env:
        p = Path(env)
        if p.is_file():
            return p
    arches = ("x64", "x86", "arm64")
    kit = os.environ.get("WindowsKitDir") or os.environ.get("WindowsSdkDir")
    if kit:
        found = [c for c in Path(kit).rglob("signtool.exe") if c.parent.name in arches]
        found.sort(key=lambda c: arches.index(c.parent.name))
        if found:
            return found[0]
    prog_files = os.environ.get("ProgramFiles(x86)") or "C:\\Program Files (x86)"
    kits = Path(prog_files) / "Windows Kits" / "10" / "bin"
    if kits.is_dir():
        versions = sorted(
            (p for p in kits.iterdir() if p.is_dir() and p.name[:1].isdigit()),
            key=lambda p: tuple(int(x) for x in p.name.split(".") if x.isdigit()),
            reverse=True,
        )
        for ver in versions:
            for arch in arches:
                cand = ver / arch / "signtool.exe"
                if cand.is_file():
                    return cand
    return None


ARTIFACT_SIGNING_TIMESTAMP = "http://timestamp.acs.microsoft.com"
LEGACY_TIMESTAMP = "http://timestamp.digicert.com"


def artifact_signing_metadata(repo: Path) -> Path:
    """Endpoint, account and certificate profile for Azure Artifact Signing. Nothing in it
    is secret; access comes from the signer's Azure login."""
    env = os.environ.get("SIGN_METADATA")
    return Path(env) if env else winbuild_dir(repo) / "artifact-signing.json"


def find_artifact_signing_dlib() -> Optional[Path]:
    env = os.environ.get("SIGN_DLIB")
    if env:
        return Path(env)
    local = os.environ.get("LOCALAPPDATA")
    if not local:
        return None
    for folder in ("MicrosoftArtifactSigningClientTools", "MicrosoftTrustedSigningClientTools"):
        cand = Path(local) / "Microsoft" / folder / "Azure.CodeSigning.Dlib.dll"
        if cand.is_file():
            return cand
    return None


def ensure_az_on_path() -> None:
    """The dlib signs in through the Azure CLI (`az login`). A shell opened before the
    CLI was installed doesn't have it on PATH yet; add its default location."""
    if shutil.which("az"):
        return
    for base in (os.environ.get("ProgramFiles"), os.environ.get("ProgramFiles(x86)")):
        if not base:
            continue
        wbin = Path(base) / "Microsoft SDKs" / "Azure" / "CLI2" / "wbin"
        if (wbin / "az.cmd").is_file():
            os.environ["PATH"] = f"{wbin}{os.pathsep}{os.environ.get('PATH', '')}"
            return


def signtool_sign_args(repo: Path) -> list[str]:
    """`signtool sign` and its options, up to the file names.

    Signs with Azure Artifact Signing (artifact-signing.json, after `az login`) unless
    SIGN_THUMBPRINT or SIGN_PFX names a local certificate."""
    signtool = find_signtool()
    if not signtool:
        print_red("signtool.exe not found. Set SIGNTOOL or install the Windows SDK.")
        sys.exit(1)
    thumb = os.environ.get("SIGN_THUMBPRINT")
    pfx = os.environ.get("SIGN_PFX")
    cmd: list[str] = [str(signtool), "sign", "/fd", "SHA256", "/td", "SHA256"]
    if thumb or pfx:
        cmd.extend(["/tr", os.environ.get("SIGN_TIMESTAMP_URL", LEGACY_TIMESTAMP)])
        if thumb:
            cmd.extend(["/sha1", thumb])
        else:
            cmd.extend(["/f", pfx])
            pfx_pw = os.environ.get("SIGN_PFX_PASSWORD", "")
            if pfx_pw:
                cmd.extend(["/p", pfx_pw])
        return cmd

    metadata = artifact_signing_metadata(repo)
    if not metadata.is_file():
        print_red(f"Artifact Signing metadata not found: {metadata}")
        sys.exit(1)
    dlib = find_artifact_signing_dlib()
    if not dlib or not dlib.is_file():
        print_red(
            "Azure.CodeSigning.Dlib.dll not found. Install the signing client tools "
            "(`winget install -e --id Microsoft.Azure.ArtifactSigningClientTools`) "
            "or set SIGN_DLIB to the dll's full path."
        )
        sys.exit(1)
    ensure_az_on_path()
    if not shutil.which("az"):
        print_red(
            "Azure CLI not found. Install it (`winget install -e --id Microsoft.AzureCLI`) "
            "and run `az login`."
        )
        sys.exit(1)
    cmd.extend(
        [
            "/tr", os.environ.get("SIGN_TIMESTAMP_URL", ARTIFACT_SIGNING_TIMESTAMP),
            "/dlib", str(dlib),
            "/dmdf", str(metadata),
        ]
    )
    return cmd


def sign_files(repo: Path, paths: list[Path]) -> None:
    """Authenticode-sign the files in place (replacing any earlier signature), then check
    that Windows accepts each signature."""
    cmd = signtool_sign_args(repo) + [str(p) for p in paths]
    for p in paths:
        print_blue(f"Signing: {p}")
    result = run_command(cmd, check=False)
    if result.returncode != 0:
        print_red("Signing failed.")
        if "/dlib" in cmd:
            print_red(
                "If the error is about credentials or access, run `az login` and make sure "
                "your account has the Artifact Signing Certificate Profile Signer role."
            )
        sys.exit(1)
    verify_signatures(paths)
    print_green("Signing finished.")


def verify_signatures(paths: list[Path]) -> None:
    signtool = find_signtool()
    if not signtool:
        print_red("signtool.exe not found. Set SIGNTOOL or install the Windows SDK.")
        sys.exit(1)
    result = run_command(
        [str(signtool), "verify", "/pa", "/q", *[str(p) for p in paths]], check=False
    )
    if result.returncode != 0:
        print_red("Signature verification failed: " + ", ".join(p.name for p in paths))
        sys.exit(1)


def app_binaries(repo: Path, configuration: str) -> list[Path]:
    """The exe and scr the installer packs, under the names check_msvc_output uses."""
    out = msvc_out_dir(repo, configuration)
    debug = configuration.lower().startswith("debug")
    names = ("infinidreamd.exe", "infinidreamd.scr") if debug else ("infinidream.exe", "infinidream.scr")
    return [out / n for n in names if (out / n).is_file()]


def find_makensis() -> Optional[Path]:
    env = os.environ.get("MAKENSIS")
    if env:
        p = Path(env)
        if p.is_file():
            return p
    on_path = shutil.which("makensis")
    if on_path:
        return Path(on_path)
    candidates = [
        os.environ.get("ProgramFiles(x86)"),
        os.environ.get("ProgramFiles"),
        r"C:\Program Files (x86)",
        r"C:\Program Files",
    ]
    for base in candidates:
        if not base:
            continue
        cand = Path(base) / "NSIS" / "makensis.exe"
        if cand.is_file():
            return cand
    return None


def run_makensis(
    repo: Path,
    *,
    version: str,
    configuration: str,
    output_dir: Path,
    sign: bool = False,
) -> Path:
    check_msvc_output(repo, configuration)

    nsi_dir = installer_dir(repo)
    nsi = nsi_dir / "nsis_installer.nsi"
    if not nsi.is_file():
        print_red(f"NSIS script not found: {nsi}")
        sys.exit(1)
    makensis_path = find_makensis()
    if not makensis_path:
        print_red(
            "makensis not found. Install NSIS 3.x (`winget install NSIS.NSIS`) "
            "or set MAKENSIS to the full path of makensis.exe."
        )
        sys.exit(1)
    makensis = str(makensis_path)

    ensure_dir(output_dir)
    out_file = output_dir / f"infinidream-{version}.exe"
    source_dir = msvc_out_dir(repo, configuration)

    cmd = [
        makensis,
        f"/DPRODUCT_VERSION={version}",
        f"/DSOURCE_DIR={source_dir}",
        f"/DOUT_FILE={out_file}",
    ]
    if sign:
        # The uninstaller is generated inside makensis, so it's signed from there:
        # !uninstfinalize runs this script with --sign-file on it before it's packed.
        cmd += [f"/DSIGN_PYTHON={sys.executable}", f"/DSIGN_SCRIPT={Path(__file__).resolve()}"]
    cmd.append("nsis_installer.nsi")
    print_blue(f"Running NSIS: {' '.join(cmd)} (cwd={nsi_dir})")
    run_command(cmd, cwd=nsi_dir)
    if not out_file.is_file():
        print_red(f"Expected output missing: {out_file}")
        sys.exit(1)
    print_green(f"Installer created: {out_file}")
    return out_file


def github_upload(asset: Path, tag: str) -> None:
    gh = shutil.which("gh")
    if not gh:
        print_red("gh CLI not found on PATH.")
        sys.exit(1)
    if not os.environ.get("GITHUB_TOKEN"):
        print_yellow("GITHUB_TOKEN is not set; gh may still use existing auth.")
    # winget pins the installer's sha256 (#575), so once a release is final its exe
    # must never change. Prereleases can be re-uploaded freely.
    view = subprocess.run(
        [gh, "release", "view", tag, "--json", "isPrerelease,isDraft,assets"],
        capture_output=True,
        text=True,
    )
    if view.returncode == 0:
        info = json.loads(view.stdout)
        final = not info["isPrerelease"] and not info["isDraft"]
        if final and any(a["name"] == asset.name for a in info["assets"]):
            print_red(
                f"Release {tag} is final and already has {asset.name}; refusing to replace it, "
                "since winget and users have its sha256. Ship a new version instead."
            )
            sys.exit(1)
    print_blue(f"Uploading {asset.name} to GitHub release {tag}")
    run_command(
        [gh, "release", "upload", tag, str(asset), "--clobber"],
    )
    print_green("Upload finished.")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Windows release helper: build NSIS Setup.exe, upload to GitHub.",
    )
    parser.add_argument(
        "--configuration",
        default="Release",
        help="MSBuild configuration to package (default: Release).",
    )
    parser.add_argument(
        "-v",
        "--version",
        dest="version",
        default=None,
        help=(
            "Version for filenames and the installer. Must match the version build.py "
            "compiled in (build.py -v); defaults to that version."
        ),
    )
    parser.add_argument(
        "--output-dir",
        default=None,
        help="Directory for artifacts (default: client_generic/WinBuild/dist).",
    )
    parser.add_argument(
        "--sign",
        action="store_true",
        help=(
            "Authenticode-sign the exe, scr, uninstaller and setup exe with Azure Artifact "
            "Signing (needs `az login`), or with SIGN_THUMBPRINT / SIGN_PFX if set."
        ),
    )
    parser.add_argument(
        "--sign-file",
        nargs="+",
        type=Path,
        metavar="FILE",
        help="Only sign these files and exit (makensis uses this for the uninstaller).",
    )
    parser.add_argument(
        "-g",
        "--github-release",
        action="store_true",
        help="Upload produced artifacts to the GitHub release whose tag matches --version.",
    )
    args = parser.parse_args()

    repo = repo_root_from_script()
    if args.sign_file:
        sign_files(repo, args.sign_file)
        return

    out_dir = Path(args.output_dir) if args.output_dir else winbuild_dir(repo) / "dist"
    version = resolve_version(repo, args.version)

    if args.sign:
        check_msvc_output(repo, args.configuration)
        sign_files(repo, app_binaries(repo, args.configuration))

    setup = run_makensis(
        repo,
        version=version,
        configuration=args.configuration,
        output_dir=out_dir,
        sign=args.sign,
    )

    if args.sign:
        sign_files(repo, [setup])

    if args.github_release:
        github_upload(setup, version)


if __name__ == "__main__":
    main()
