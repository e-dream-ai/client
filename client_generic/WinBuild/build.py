#!/usr/bin/env python3
"""
Windows MSBuild driver for the infinidream / e-dream MSVC solution.

Mirrors the ergonomics of client_generic/MacBuild/build.py (CLI, colored logs).
"""

from __future__ import annotations

import argparse
import os
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
    print(f"{color}{message}{Colors.NC}")


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
    except subprocess.CalledProcessError as e:
        if capture_output:
            print_red(f"Command failed: {' '.join(cmd)}")
            if e.stdout:
                print(e.stdout)
            if e.stderr:
                print(e.stderr)
        raise


def run_command_with_output(
    cmd: list[str],
    *,
    env: Optional[dict[str, str]] = None,
    cwd: Optional[str | Path] = None,
) -> str:
    r = run_command(cmd, check=False, capture_output=True, env=env, cwd=cwd)
    if r.stdout:
        return r.stdout.strip()
    return ""


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parent.parent.parent


def find_vswhere() -> Optional[Path]:
    prog_x86 = os.environ.get("ProgramFiles(x86)") or os.environ.get("PROGRAMFILES(X86)")
    if not prog_x86:
        return None
    candidate = Path(prog_x86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if candidate.is_file():
        return candidate
    return None


def find_msbuild() -> Path:
    env_path = os.environ.get("MSBUILD")
    if env_path:
        p = Path(env_path)
        if p.is_file():
            return p
        print_yellow(f"MSBUILD is set but not a file: {env_path}")

    vswhere = find_vswhere()
    if vswhere:
        out = run_command_with_output(
            [
                str(vswhere),
                "-latest",
                "-requires",
                "Microsoft.Component.MSBuild",
                "-find",
                r"MSBuild\**\Bin\MSBuild.exe",
            ]
        )
        for line in out.splitlines():
            line = line.strip()
            if line.lower().endswith("msbuild.exe") and Path(line).is_file():
                return Path(line)

    # Fallback: PATH
    from shutil import which

    w = which("msbuild.exe") or which("MSBuild")
    if w:
        return Path(w)

    print_red(
        "Could not find MSBuild. Install Visual Studio Build Tools or set MSBUILD to msbuild.exe."
    )
    sys.exit(1)


def run_vcpkg_install(repo_root: Path, triplet: str, vcpkg_root: Optional[Path]) -> None:
    root = vcpkg_root or (repo_root / "vcpkg")
    exe = root / "vcpkg.exe"
    if not exe.is_file():
        print_red(f"vcpkg.exe not found at {exe}. Run vcpkg\\bootstrap-vcpkg.bat first.")
        sys.exit(1)
    print_blue(f"Running vcpkg install --triplet {triplet} ...")
    env = {"VCPKG_ROOT": str(root)}
    run_command(
        [str(exe), "install", f"--triplet={triplet}"],
        cwd=str(repo_root),
        env=env,
    )
    print_green("vcpkg install finished.")


def main() -> None:
    parser = argparse.ArgumentParser(description="Build the Windows MSVC solution (e-dream.sln).")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("-r", "--release", action="store_true", help="Configuration Release (default).")
    mode.add_argument("-d", "--debug", action="store_true", help="Configuration Debug.")
    parser.add_argument(
        "--configuration",
        dest="configuration",
        default=None,
        help="MSBuild configuration (overrides -r / -d), e.g. DebugMD.",
    )
    parser.add_argument(
        "--platform",
        default="x64",
        help="MSBuild platform (default: x64).",
    )
    parser.add_argument(
        "--solution",
        default=None,
        help="Path to .sln (default: client_generic/MSVC/e-dream.sln).",
    )
    parser.add_argument(
        "--run-vcpkg",
        action="store_true",
        help="Run 'vcpkg install' for --triplet before building.",
    )
    parser.add_argument(
        "--triplet",
        default="x64-windows",
        help="vcpkg triplet for --run-vcpkg (default: x64-windows).",
    )
    parser.add_argument(
        "--vcpkg-root",
        default=None,
        help="VCPKG_ROOT directory (default: <repo>/vcpkg).",
    )
    parser.add_argument(
        "--no-restore",
        action="store_true",
        help="Do not pass /t:Restore to MSBuild.",
    )
    parser.add_argument(
        "--msbuild",
        default=None,
        help="Explicit path to MSBuild.exe (overrides discovery and MSBUILD env).",
    )
    parser.add_argument(
        "--target",
        default="Build",
        help="MSBuild target (default: Build).",
    )
    args = parser.parse_args()

    if args.configuration:
        configuration = args.configuration
    elif args.debug:
        configuration = "Debug"
    else:
        configuration = "Release"

    repo_root = repo_root_from_script()
    sln = Path(args.solution) if args.solution else repo_root / "client_generic" / "MSVC" / "e-dream.sln"
    if not sln.is_file():
        print_red(f"Solution not found: {sln}")
        sys.exit(1)

    vcpkg_root_path = Path(args.vcpkg_root) if args.vcpkg_root else None
    vcpkg_env_root = os.environ.get("VCPKG_ROOT")
    if vcpkg_env_root and not vcpkg_root_path:
        vcpkg_root_path = Path(vcpkg_env_root)

    if args.run_vcpkg:
        run_vcpkg_install(repo_root, args.triplet, vcpkg_root_path)

    if args.msbuild:
        msbuild = Path(args.msbuild)
        if not msbuild.is_file():
            print_red(f"--msbuild is not a file: {msbuild}")
            sys.exit(1)
    else:
        msbuild = find_msbuild()

    print_blue(f"Using MSBuild: {msbuild}")
    print_blue(f"Solution: {sln}")
    print_blue(f"Configuration={configuration} Platform={args.platform}")

    build_cmd: list[str] = [
        str(msbuild),
        str(sln),
        "/m",
        f"/p:Configuration={configuration}",
        f"/p:Platform={args.platform}",
    ]
    if not args.no_restore:
        build_cmd.append("/restore")
    build_cmd.append(f"/t:{args.target}")

    merge_env: dict[str, str] = {}
    if vcpkg_root_path and vcpkg_root_path.is_dir():
        merge_env["VCPKG_ROOT"] = str(vcpkg_root_path)

    try:
        run_command(build_cmd, env=merge_env if merge_env else None)
    except subprocess.CalledProcessError:
        print_red("MSBuild failed.")
        sys.exit(1)

    out_dir = sln.parent / configuration
    print_green("Build succeeded.")
    print_green(f"Output directory (typical): {out_dir}")


if __name__ == "__main__":
    main()
