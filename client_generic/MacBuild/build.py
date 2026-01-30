#!/usr/bin/env python3
"""
infinidream Unified Build Script

Builds screensaver and app, with optional notarization.
Combines the functionality of build_screensaver.sh, notarize_screensaver.sh,
and build_app_with_screensaver.sh.

Auto-discovers code signing credentials from keychain, with fallback to defaults.
Override via environment variables: DEVELOPER_ID_CERT, TEAM_ID, KEYCHAIN_PROFILE
"""

import argparse
import hashlib
import os
import plistlib
import re
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from typing import Optional


# ANSI color codes
class Colors:
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    NC = '\033[0m'  # No Color


def print_color(color: str, message: str) -> None:
    """Print a colored message."""
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
    check: bool = True,
    capture_output: bool = False,
    env: Optional[dict] = None,
    cwd: Optional[str] = None,
) -> subprocess.CompletedProcess:
    """Run a command and return the result."""
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)

    try:
        result = subprocess.run(
            cmd,
            check=check,
            capture_output=capture_output,
            text=True,
            env=merged_env,
            cwd=cwd,
        )
        return result
    except subprocess.CalledProcessError as e:
        if capture_output:
            print(f"Command failed: {' '.join(cmd)}")
            if e.stdout:
                print(f"stdout: {e.stdout}")
            if e.stderr:
                print(f"stderr: {e.stderr}")
        raise


def run_command_with_output(cmd: list[str], env: Optional[dict] = None) -> str:
    """Run a command and return its output."""
    result = run_command(cmd, capture_output=True, check=False, env=env)
    return result.stdout.strip() if result.stdout else ""


def discover_developer_id_cert() -> tuple[str, bool]:
    """Auto-discover Developer ID certificate from keychain."""
    env_cert = os.environ.get('DEVELOPER_ID_CERT')
    if env_cert:
        return env_cert, False

    try:
        output = run_command_with_output([
            'security', 'find-identity', '-v', '-p', 'codesigning'
        ])
        for line in output.split('\n'):
            if 'Developer ID Application' in line:
                match = re.search(r'"([^"]+)"', line)
                if match:
                    return match.group(1), True
    except Exception:
        pass

    # Fallback to default
    return "Developer ID Application: Guillaume Louel (3L54M5L5KK)", False


def extract_team_id(cert: str) -> tuple[str, bool]:
    """Extract Team ID from certificate name."""
    env_team = os.environ.get('TEAM_ID')
    if env_team:
        return env_team, False

    match = re.search(r'\(([^)]+)\)', cert)
    if match:
        return match.group(1), True

    # Fallback to default
    return "3L54M5L5KK", False


def get_keychain_profile() -> str:
    """Get keychain profile for notarization."""
    return os.environ.get('KEYCHAIN_PROFILE', 'infinidream-notarization')


def read_plist_version(plist_path: Path) -> Optional[str]:
    """Read version from Info.plist file."""
    try:
        with open(plist_path, 'rb') as f:
            plist = plistlib.load(f)
        return plist.get('CFBundleShortVersionString')
    except Exception:
        return None


def update_plist_version(plist_path: Path, version: str) -> None:
    """Update version in Info.plist file."""
    # Use PlistBuddy for consistent behavior with the shell script
    run_command([
        '/usr/libexec/PlistBuddy',
        '-c', f'Set :CFBundleShortVersionString {version}',
        str(plist_path)
    ])
    run_command([
        '/usr/libexec/PlistBuddy',
        '-c', f'Set :CFBundleVersion {version}',
        str(plist_path)
    ])


def compute_directory_md5(directory: Path) -> str:
    """Compute MD5 hash of all files in a directory.

    Matches the shell script method:
    find DIR -type f -exec md5 -q {} \\; | sort | md5 -q
    """
    # Use the same method as the shell script for consistency
    result = run_command_with_output([
        'bash', '-c',
        f'find "{directory}" -type f -exec md5 -q {{}} \\; | sort | md5 -q'
    ])
    return result.strip()


def sign_component(component_path: str, cert: str) -> None:
    """Sign a component with the given certificate."""
    run_command([
        'codesign', '--force', '--options', 'runtime', '--timestamp',
        '--sign', cert, component_path
    ])


def verify_signature(path: str) -> bool:
    """Verify code signature of a bundle."""
    result = run_command(
        ['codesign', '--verify', '--deep', '--strict', path],
        check=False,
        capture_output=True
    )
    return result.returncode == 0


def has_xcpretty() -> bool:
    """Check if xcpretty is available."""
    return shutil.which('xcpretty') is not None


def run_xcodebuild(args: list[str], env: Optional[dict] = None) -> None:
    """Run xcodebuild with optional xcpretty."""
    if has_xcpretty():
        # Pipe through xcpretty
        xcode_proc = subprocess.Popen(
            ['xcodebuild'] + args,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env={**os.environ, **(env or {})}
        )
        xcpretty_proc = subprocess.Popen(
            ['xcpretty', '--color'],
            stdin=xcode_proc.stdout
        )
        xcode_proc.stdout.close()
        xcpretty_proc.wait()
        xcode_proc.wait()
        if xcode_proc.returncode != 0:
            raise subprocess.CalledProcessError(xcode_proc.returncode, 'xcodebuild')
    else:
        run_command(['xcodebuild'] + args, env=env)


def clean_stale_rpaths(binary_path: Path) -> bool:
    """Remove stale rpaths from a binary. Returns True if any were removed."""
    if not binary_path.exists():
        return False

    output = run_command_with_output(['otool', '-l', str(binary_path)])
    stale_patterns = ['DerivedData', 'Build', 'Intermediates']

    removed_any = False
    lines = output.split('\n')
    for i, line in enumerate(lines):
        if 'LC_RPATH' in line:
            # Look for the path line after LC_RPATH
            for j in range(i, min(i + 5, len(lines))):
                if 'path ' in lines[j]:
                    match = re.search(r'path\s+(\S+)\s+\(offset', lines[j])
                    if match:
                        rpath = match.group(1)
                        if any(pattern in rpath for pattern in stale_patterns):
                            print(f"  Removing stale rpath: {rpath}")
                            run_command(
                                ['install_name_tool', '-delete_rpath', rpath, str(binary_path)],
                                check=False
                            )
                            removed_any = True
                    break

    return removed_any


class BuildConfig:
    """Build configuration settings."""

    def __init__(self, args: argparse.Namespace):
        self.release = args.release
        self.stage = args.stage
        self.notarize = args.notarize
        self.generate_appcast = args.appcast
        self.github_release = args.github
        self.version = args.version

        # Configuration derived from flags
        self.build_config = "Release" if self.release else "Debug"

        if self.stage:
            self.screensaver_scheme = "ScreenSaver Stage"
            self.app_scheme = "infinidream App Stage"
            self.screensaver_name = "infinidream-stage.saver"
            self.app_name = "infinidream stage.app"
        else:
            self.screensaver_scheme = "ScreenSaver Prod"
            self.app_scheme = "infinidream App Prod"
            self.screensaver_name = "infinidream.saver"
            self.app_name = "infinidream.app"

        # Signing configuration
        self.developer_id_cert, self.cert_auto_discovered = discover_developer_id_cert()
        self.team_id, self.team_auto_discovered = extract_team_id(self.developer_id_cert)
        self.keychain_profile = get_keychain_profile()

        # Paths
        self.project = "infinidream.xcodeproj"
        self.build_dir = Path("build")
        self.derived_data = self.build_dir / "DerivedData"
        self.products_dir = self.derived_data / "Build" / "Products" / self.build_config

        if self.stage:
            self.app_info_plist = Path("App-Stage-Info.plist")
            self.saver_info_plist = Path("Screensaver-Stage-Info.plist")
        else:
            self.app_info_plist = Path("App-Info.plist")
            self.saver_info_plist = Path("Screensaver-Info.plist")

        # Compute plist version (with -stage suffix for stage builds)
        if self.version and self.stage:
            self.plist_version = f"{self.version}-stage"
        else:
            self.plist_version = self.version or ""

        # If version not provided, try to extract from Info.plist
        if not self.version:
            self.version = read_plist_version(self.app_info_plist)
            self.plist_version = self.version or ""


def validate_config(config: BuildConfig) -> None:
    """Validate build configuration."""
    if config.notarize and not config.release:
        print_red("Error: Notarization requires Release mode. Use -r flag with -n")
        sys.exit(1)

    if config.generate_appcast and not config.version:
        print_red("Error: Appcast generation requires version. Use -v flag with -a")
        sys.exit(1)

    if config.github_release and not config.version:
        print_red("Error: GitHub release requires version. Use -v flag with -g")
        sys.exit(1)

    # Check gh CLI is available if GitHub release is requested
    if config.github_release and not shutil.which('gh'):
        print_red("Error: GitHub CLI (gh) not found. Install with: brew install gh")
        sys.exit(1)


def print_build_info(config: BuildConfig) -> None:
    """Print build configuration information."""
    print_blue("========================================")
    print_blue("infinidream Unified Build")
    print_blue("========================================")

    if config.stage:
        print(f"Environment: {Colors.YELLOW}STAGE{Colors.NC}")
    else:
        print(f"Environment: {Colors.GREEN}PRODUCTION{Colors.NC}")

    print(f"Configuration: {config.build_config}")
    print(f"Screensaver Scheme: {config.screensaver_scheme}")
    print(f"App Scheme: {config.app_scheme}")
    print(f"Notarization: {'Enabled' if config.notarize else 'Disabled'}")
    print(f"Generate Appcast: {'Enabled' if config.generate_appcast else 'Disabled'}")
    print(f"GitHub Release: {'Enabled' if config.github_release else 'Disabled'}")

    if config.version:
        print(f"Version: {config.version}")

    # Show signing configuration if Release build
    if config.release:
        print("Code Signing:")
        status = f"{Colors.GREEN}(auto-discovered){Colors.NC}" if config.cert_auto_discovered else f"{Colors.YELLOW}(default){Colors.NC}"
        print(f"  Certificate: {config.developer_id_cert} {status}")

        status = f"{Colors.GREEN}(auto-discovered){Colors.NC}" if config.team_auto_discovered else f"{Colors.YELLOW}(default){Colors.NC}"
        print(f"  Team ID: {config.team_id} {status}")
    else:
        print("Code Signing: Disabled")

    if config.notarize:
        print(f"Notarization Profile: {config.keychain_profile}")

    print_blue("========================================")
    print()


def clean_previous_builds(build_dir: Path) -> None:
    """Clean previous build directory."""
    print_yellow("Cleaning previous builds...")
    if build_dir.exists():
        try:
            shutil.rmtree(build_dir)
        except Exception:
            print_yellow("Standard cleanup failed, trying alternative method...")
            # Remove .DS_Store files first
            for ds_store in build_dir.rglob('.DS_Store'):
                ds_store.unlink()
            shutil.rmtree(build_dir)


def step0_resign_sparkle(config: BuildConfig) -> None:
    """Re-sign Sparkle.framework for distribution (Release builds only)."""
    if not config.release:
        return

    print()
    print_blue("========================================")
    print_blue("STEP 0: Re-signing Sparkle.framework")
    print_blue("========================================")

    sparkle_path = Path("Frameworks/Sparkle.framework")

    if not sparkle_path.exists():
        print_yellow(f"Warning: Sparkle.framework not found at {sparkle_path} - skipping re-sign")
        return

    print_yellow("Re-signing Sparkle.framework with Developer ID certificate...")

    # Sign components from innermost to outermost
    components = [
        sparkle_path / "Versions/B/XPCServices/Installer.xpc",
        sparkle_path / "Versions/B/XPCServices/Downloader.xpc",
        sparkle_path / "Versions/B/Updater.app",
        sparkle_path / "Versions/B/Autoupdate",
        sparkle_path,
    ]

    for component in components:
        name = component.name
        print(f"Signing {name}...")
        sign_component(str(component), config.developer_id_cert)

    # Verify the signature
    print_yellow("Verifying Sparkle.framework signature...")
    if verify_signature(str(sparkle_path)):
        print_green("Sparkle.framework signed successfully")
    else:
        print_red("Sparkle.framework signature verification failed")
        sys.exit(1)


def step1_build_screensaver(config: BuildConfig) -> Path:
    """Build the screensaver. Returns path to built screensaver."""
    print()
    print_blue("========================================")
    print_blue("STEP 1: Building Screensaver")
    print_blue("========================================")

    env = {'BUILD_VERSION': config.plist_version} if config.plist_version else {}

    common_args = [
        '-project', config.project,
        '-scheme', config.screensaver_scheme,
        '-configuration', config.build_config,
        '-derivedDataPath', str(config.derived_data),
    ]

    if config.release:
        # Release build with manual code signing
        args = common_args + [
            'CODE_SIGN_STYLE=Manual',
            f'CODE_SIGN_IDENTITY={config.developer_id_cert}',
            f'DEVELOPMENT_TEAM={config.team_id}',
            'OTHER_CODE_SIGN_FLAGS=--timestamp',
        ]
    else:
        # Debug build without code signing
        args = common_args + [
            'CODE_SIGN_IDENTITY=',
            'CODE_SIGNING_REQUIRED=NO',
            'CODE_SIGNING_ALLOWED=NO',
        ]

    run_xcodebuild(args, env=env)

    # Verify screensaver build
    screensaver_path = config.products_dir / config.screensaver_name
    if not screensaver_path.exists():
        print_red(f"Failed to build {config.screensaver_name}")
        print_red(f"Expected at: {screensaver_path}")
        sys.exit(1)

    print_green(f"{config.screensaver_name} built successfully")

    # Verify code signing for Release builds
    if config.release:
        print_yellow("Verifying code signature...")
        if verify_signature(str(screensaver_path)):
            print_green("Code signature verified")

            # Check with spctl for more detailed info
            print_yellow("Checking signature details with spctl...")
            result = run_command(
                ['spctl', '-vvv', '--assess', '--type', 'exec', str(screensaver_path)],
                check=False,
                capture_output=True
            )
            spctl_output = result.stderr if result.stderr else result.stdout

            if spctl_output:
                print(f"Signature status: {spctl_output}")
                if 'Developer ID Application' in spctl_output:
                    print_green("Signed with Developer ID Application certificate")
                else:
                    print_yellow("Warning: Not signed with Developer ID Application certificate")
            else:
                print_yellow("Warning: Could not get detailed signature information")
        else:
            print_red("Code signature verification failed")
            print("This may cause notarization to fail.")

    return screensaver_path


def copy_screensaver(config: BuildConfig, source_path: Path) -> tuple[Path, Path, Path]:
    """Copy screensaver to output and Resources directories. Returns (output_saver, project_saver, zip_path)."""
    output_dir = config.build_dir / config.build_config
    output_dir.mkdir(parents=True, exist_ok=True)

    # Copy to output directory
    print_yellow("Copying screensaver to output directory...")
    output_saver = output_dir / config.screensaver_name
    if output_saver.exists():
        shutil.rmtree(output_saver)
    # Use ditto for proper macOS bundle copying (preserves resource forks and extended attributes)
    run_command(['ditto', str(source_path), str(output_saver)])

    if not output_saver.exists():
        print_red("Failed to copy screensaver to output directory")
        sys.exit(1)

    # Copy to project Resources directory
    print_yellow("Copying screensaver to project Resources directory...")
    project_saver = Path("Resources") / config.screensaver_name

    if project_saver.exists():
        print_yellow("Removing existing screensaver in Resources...")
        shutil.rmtree(project_saver)

    # Use ditto for proper macOS bundle copying
    run_command(['ditto', str(source_path), str(project_saver)])

    if not project_saver.exists():
        print_red("Failed to copy screensaver to project Resources")
        sys.exit(1)

    # Verify copy with MD5
    print_yellow("Verifying copy integrity with MD5...")
    source_md5 = compute_directory_md5(source_path)
    dest_md5 = compute_directory_md5(project_saver)

    if source_md5 != dest_md5:
        print_red("MD5 mismatch! Copy verification failed")
        print(f"Source MD5: {source_md5}")
        print(f"Dest MD5: {dest_md5}")
        sys.exit(1)

    print_green("MD5 verification passed - screensaver copied correctly")

    # Create zip for notarization
    print_yellow("Creating zip of screensaver for notarization...")
    screensaver_zip = output_dir / "screensaver.zip"
    run_command(['ditto', '-c', '-k', '--keepParent', str(source_path), str(screensaver_zip)])

    if not screensaver_zip.exists():
        print_red("Failed to create zip file")
        sys.exit(1)

    return output_saver, project_saver, screensaver_zip


def step2_notarize_screensaver(config: BuildConfig, project_saver: Path, screensaver_zip: Path) -> None:
    """Notarize the screensaver."""
    if not config.notarize:
        return

    print()
    print_blue("========================================")
    print_blue("STEP 2: Notarizing Screensaver")
    print_blue("========================================")

    print_yellow("Submitting screensaver to Apple for notarization...")
    print_yellow("This may take several minutes...")

    result = run_command(
        ['xcrun', 'notarytool', 'submit', str(screensaver_zip),
         '--keychain-profile', config.keychain_profile, '--wait'],
        capture_output=True,
        check=False
    )

    notary_output = result.stdout + (result.stderr or "")
    print(notary_output)

    # Extract submission ID
    submission_id = None
    match = re.search(r'id: ([a-f0-9-]+)', notary_output)
    if match:
        submission_id = match.group(1)

    # Check for invalid or rejected status
    if 'status: Invalid' in notary_output or 'status: Rejected' in notary_output:
        print()
        print_red("Screensaver notarization failed - Status: Invalid/Rejected")
        print()
        if submission_id:
            print(f"Submission ID: {submission_id}")
            print()
            print("To see why notarization failed, run:")
            print(f"  xcrun notarytool log {submission_id} --keychain-profile '{config.keychain_profile}'")
            print()
        print("Common issues:")
        print("- Missing code signature")
        print("- Invalid or expired certificate")
        print("- Unsigned binaries in the bundle")
        print("- Missing entitlements")
        sys.exit(1)

    if 'status: Accepted' not in notary_output:
        print()
        print_red("Screensaver notarization failed")
        print()
        print("Troubleshooting tips:")
        print(f"1. Ensure keychain profile '{config.keychain_profile}' is configured:")
        print(f"   xcrun notarytool store-credentials '{config.keychain_profile}' \\")
        print("   --apple-id YOUR_APPLE_ID --team-id YOUR_TEAM_ID")
        print()
        print("2. Check notarization history:")
        print(f"   xcrun notarytool history --keychain-profile '{config.keychain_profile}'")
        sys.exit(1)

    print_green("Screensaver notarization successful")

    # Staple the notarization ticket
    print_yellow("Stapling notarization ticket to screensaver...")
    result = run_command(
        ['xcrun', 'stapler', 'staple', str(project_saver)],
        check=False
    )

    if result.returncode != 0:
        print_red("Failed to staple notarization ticket")
        print("The screensaver was notarized but stapling failed.")
        print("You may continue, but the screensaver will need internet access to verify.")
        sys.exit(1)

    print_green("Notarization ticket stapled successfully")

    # Verify stapling
    print_yellow("Verifying notarization...")
    result = run_command(
        ['spctl', '--assess', '--type', 'install', '-vvv', str(project_saver)],
        check=False,
        capture_output=True
    )

    if 'accepted' in (result.stderr or result.stdout or ''):
        print_green("Notarization verification passed")
    else:
        print_yellow("Warning: Could not verify notarization (this may be normal for screensavers)")


def step3_archive_app(config: BuildConfig) -> Path:
    """Archive the app. Returns path to archive."""
    print()
    print_blue("========================================")
    print_blue("STEP 3: Archiving App")
    print_blue("========================================")

    print_green("Found notarized screensaver in project Resources")

    print_yellow(f"Archiving app ({config.app_scheme})...")

    archive_path = config.build_dir / config.build_config / f"{config.app_name}.xcarchive"

    env = {'BUILD_VERSION': config.plist_version} if config.plist_version else {}

    common_args = [
        'archive',
        '-project', config.project,
        '-scheme', config.app_scheme,
        '-configuration', config.build_config,
        '-derivedDataPath', str(config.derived_data),
        '-archivePath', str(archive_path),
    ]

    if config.release:
        args = common_args + [
            'CODE_SIGN_STYLE=Manual',
            f'CODE_SIGN_IDENTITY={config.developer_id_cert}',
            f'DEVELOPMENT_TEAM={config.team_id}',
            'OTHER_CODE_SIGN_FLAGS=--timestamp',
        ]
    else:
        args = common_args + [
            'CODE_SIGN_IDENTITY=',
            'CODE_SIGNING_REQUIRED=NO',
            'CODE_SIGNING_ALLOWED=NO',
        ]

    run_xcodebuild(args, env=env)

    if not archive_path.exists():
        print_red("Failed to create app archive")
        print_red(f"Expected at: {archive_path}")
        sys.exit(1)

    print_green("App archive created successfully")
    return archive_path


def step4_export_app(config: BuildConfig, archive_path: Path) -> Path:
    """Export the app from archive. Returns path to exported app."""
    print()
    print_blue("========================================")
    print_blue("STEP 4: Exporting App")
    print_blue("========================================")

    export_options_path = config.build_dir / config.build_config / "AppExportOptions.plist"

    if config.release:
        export_options = {
            'method': 'developer-id',
            'teamID': config.team_id,
            'signingStyle': 'automatic',
            'signingCertificate': 'Developer ID Application',
        }
    else:
        export_options = {
            'method': 'development',
            'teamID': config.team_id,
            'signingStyle': 'automatic',
        }

    with open(export_options_path, 'wb') as f:
        plistlib.dump(export_options, f)

    print_yellow("Exporting app for distribution...")
    export_path = config.build_dir / config.build_config / "AppExport"

    run_xcodebuild([
        '-exportArchive',
        '-archivePath', str(archive_path),
        '-exportPath', str(export_path),
        '-exportOptionsPlist', str(export_options_path),
    ])

    # Find the exported app
    exported_apps = list(export_path.glob("*.app"))
    if not exported_apps:
        print_red("Failed to export app")
        print("Export path contents:")
        if export_path.exists():
            for item in export_path.iterdir():
                print(f"  {item}")
        else:
            print("Export path not found")
        sys.exit(1)

    exported_app = exported_apps[0]
    print_green("App exported successfully")
    print(f"Exported app: {exported_app}")

    # Clean up stale build rpaths
    print_yellow("Cleaning up stale rpaths from app binary...")
    app_binary = exported_app / "Contents" / "MacOS" / config.app_name.replace('.app', '')

    # Try without .app suffix first, then with the actual binary name
    if not app_binary.exists():
        # Look for any executable in MacOS folder
        macos_dir = exported_app / "Contents" / "MacOS"
        if macos_dir.exists():
            for item in macos_dir.iterdir():
                if item.is_file() and os.access(item, os.X_OK):
                    app_binary = item
                    break

    if app_binary.exists():
        if clean_stale_rpaths(app_binary):
            print_green("Stale rpaths removed")
            # Re-sign the binary after modification
            run_command(['codesign', '--force', '--sign', '-', str(app_binary)], check=False)
        else:
            print("  No stale rpaths found")

    return exported_app


def step5_notarize_app(config: BuildConfig, exported_app: Path) -> None:
    """Notarize the app."""
    if not config.notarize:
        return

    print()
    print_blue("========================================")
    print_blue("STEP 5: Notarizing App")
    print_blue("========================================")

    # Create zip for notarization
    print_yellow("Creating zip of app for notarization...")
    app_zip = config.build_dir / config.build_config / "app.zip"
    run_command(['ditto', '-c', '-k', '--keepParent', str(exported_app), str(app_zip)])

    # Submit for notarization
    print_yellow("Submitting app to Apple for notarization...")
    print_yellow("This may take several minutes...")

    result = run_command(
        ['xcrun', 'notarytool', 'submit', str(app_zip),
         '--keychain-profile', config.keychain_profile, '--wait'],
        capture_output=True,
        check=False
    )

    notary_output = result.stdout + (result.stderr or "")
    print(notary_output)

    # Extract submission ID
    submission_id = None
    match = re.search(r'id: ([a-f0-9-]+)', notary_output)
    if match:
        submission_id = match.group(1)

    # Check for invalid or rejected status
    if 'status: Invalid' in notary_output or 'status: Rejected' in notary_output:
        print()
        print_red("App notarization failed - Status: Invalid/Rejected")
        print()
        if submission_id:
            print(f"Submission ID: {submission_id}")
            print()
            print("To see why notarization failed, run:")
            print(f"  xcrun notarytool log {submission_id} --keychain-profile '{config.keychain_profile}'")
            print()
        print("Common issues:")
        print("- Missing code signature")
        print("- Invalid or expired certificate")
        print("- Unsigned binaries in the bundle")
        print("- Missing entitlements")
        print("- Embedded screensaver not properly signed")
        print()
        print("The app has been built and exported.")
        print("To retry notarization after fixing issues:")
        print(f"1. ditto -c -k --keepParent '{exported_app}' '{app_zip}'")
        print(f"2. xcrun notarytool submit '{app_zip}' --keychain-profile '{config.keychain_profile}' --wait")
        print(f"3. xcrun stapler staple '{exported_app}'")
        app_zip.unlink()
        sys.exit(1)

    if 'status: Accepted' not in notary_output:
        print()
        print_red("App notarization failed")
        print()
        print("The app has been built but notarization failed.")
        print("You can try notarizing manually or check your credentials.")
        print()
        print("To notarize manually:")
        print(f"1. xcrun notarytool submit '{app_zip}' --keychain-profile '{config.keychain_profile}' --wait")
        print(f"2. xcrun stapler staple '{exported_app}'")
        app_zip.unlink()
        sys.exit(1)

    print_green("App notarization successful")

    # Staple the notarization ticket
    print_yellow("Stapling notarization ticket to app...")
    result = run_command(
        ['xcrun', 'stapler', 'staple', str(exported_app)],
        check=False
    )

    if result.returncode != 0:
        print_red("Failed to staple notarization ticket to app")
        print("The app was notarized but stapling failed.")
        print("Users will need internet access to verify the app on first launch.")
    else:
        print_green("Notarization ticket stapled successfully")

    # Verify notarization
    print_yellow("Verifying app notarization...")
    result = run_command(
        ['spctl', '--assess', '--type', 'execute', '-vvv', str(exported_app)],
        check=False,
        capture_output=True
    )

    if 'accepted' in (result.stderr or result.stdout or ''):
        print_green("App notarization verification passed")
    else:
        print_yellow("Warning: Could not fully verify notarization (app may still be valid)")

    # Clean up app zip
    app_zip.unlink()


def step6_create_distribution(config: BuildConfig, exported_app: Path, output_saver: Path) -> Path:
    """Create final distribution package. Returns path to final zip."""
    print()
    print_blue("========================================")
    print_blue("STEP 6: Creating Distribution Package")
    print_blue("========================================")

    # Determine output filename
    if config.version:
        final_zip = config.build_dir / config.build_config / f"infinidream-{config.version}.zip"
    else:
        date_str = datetime.now().strftime("%Y%m%d")
        final_zip = config.build_dir / config.build_config / f"infinidream-{date_str}.zip"

    print_yellow("Creating final distribution package...")
    run_command([
        'ditto', '-c', '-k', '--keepParent', '--norsrc',
        str(exported_app), str(final_zip)
    ])

    return final_zip


def get_dir_size(path: Path) -> str:
    """Get human-readable size of a directory."""
    result = run_command_with_output(['du', '-sh', str(path)])
    return result.split()[0] if result else "?"


def get_file_size(path: Path) -> str:
    """Get human-readable size of a file."""
    result = run_command_with_output(['du', '-h', str(path)])
    return result.split()[0] if result else "?"


def step7_generate_appcast(config: BuildConfig, final_zip: Path) -> Optional[Path]:
    """Generate appcast for Sparkle updates. Returns path to appcast or None."""
    if not config.generate_appcast:
        return None

    print()
    print_blue("========================================")
    print_blue("STEP 7: Generating Appcast")
    print_blue("========================================")

    script_dir = Path(__file__).parent
    appcast_script = script_dir / "generate_appcast.sh"

    if not appcast_script.exists() or not os.access(appcast_script, os.X_OK):
        print_red("Error: generate_appcast.sh not found or not executable")
        print(f"Expected at: {appcast_script}")
        return None

    print_yellow("Generating appcast.xml...")
    output_dir = config.build_dir / config.build_config

    result = run_command(
        [str(appcast_script), '-v', config.version, '-z', str(final_zip), '-o', str(output_dir)],
        check=False
    )

    if result.returncode == 0:
        appcast_path = output_dir / "appcast.xml"
        print_green("Appcast generated successfully")
        return appcast_path
    else:
        print_yellow("Warning: Appcast generation failed (continuing without appcast)")
        return None


def step8_github_release(config: BuildConfig, final_zip: Path) -> Optional[str]:
    """Create GitHub release with tag and upload zip. Returns release URL or None."""
    if not config.github_release:
        return None

    print()
    print_blue("========================================")
    print_blue("STEP 8: Creating GitHub Release")
    print_blue("========================================")

    tag = config.version
    release_name = f"v{config.version}"

    # Check if tag already exists locally
    result = run_command(
        ['git', 'tag', '-l', tag],
        capture_output=True,
        check=False
    )
    tag_exists_locally = bool(result.stdout.strip())

    # Check if tag exists on remote
    result = run_command(
        ['git', 'ls-remote', '--tags', 'origin', f'refs/tags/{tag}'],
        capture_output=True,
        check=False
    )
    tag_exists_remote = bool(result.stdout.strip())

    # Delete existing tags so we can recreate at current commit
    if tag_exists_locally:
        print_yellow(f"Deleting existing local tag {tag}...")
        run_command(['git', 'tag', '-d', tag], check=False, capture_output=True)

    if tag_exists_remote:
        print_yellow(f"Deleting existing remote tag {tag}...")
        run_command(['git', 'push', 'origin', f':refs/tags/{tag}'], check=False, capture_output=True)

    # Create and push tag
    print_yellow(f"Creating git tag {tag}...")
    result = run_command(
        ['git', 'tag', tag],
        check=False,
        capture_output=True
    )
    if result.returncode != 0:
        print_red(f"Failed to create tag {tag}")
        print(result.stderr or result.stdout)
        return None
    print_green(f"Tag {tag} created")

    print_yellow(f"Pushing tag {tag} to origin...")
    result = run_command(
        ['git', 'push', 'origin', tag],
        check=False,
        capture_output=True
    )
    if result.returncode != 0:
        print_red(f"Failed to push tag {tag}")
        print(result.stderr or result.stdout)
        return None
    print_green(f"Tag {tag} pushed to origin")

    # Check if release already exists
    result = run_command(
        ['gh', 'release', 'view', tag],
        check=False,
        capture_output=True
    )
    release_exists = result.returncode == 0

    if release_exists:
        print_yellow(f"Release {tag} already exists, deleting it...")
        run_command(
            ['gh', 'release', 'delete', tag, '--yes'],
            check=False,
            capture_output=True
        )

    # Create new release with auto-generated notes as prerelease
    print_yellow(f"Creating GitHub release {release_name}...")
    result = run_command(
        [
            'gh', 'release', 'create', tag,
            str(final_zip),
            '--title', release_name,
            '--generate-notes',
            '--prerelease'
        ],
        check=False,
        capture_output=True
    )
    if result.returncode != 0:
        print_red("Failed to create GitHub release")
        print(result.stderr or result.stdout)
        return None
    print_green(f"GitHub release {release_name} created as prerelease")

    # Clean up release notes - remove "by @user in <link>" from each line
    print_yellow("Cleaning up release notes...")
    result = run_command(
        ['gh', 'release', 'view', tag, '--json', 'body', '-q', '.body'],
        check=False,
        capture_output=True
    )
    if result.returncode == 0 and result.stdout:
        original_notes = result.stdout
        # Remove "by @username in https://..." pattern from each line
        cleaned_notes = re.sub(r' by @[\w-]+ in https://[^\s]+', '', original_notes)

        if cleaned_notes != original_notes:
            # Update the release with cleaned notes
            result = run_command(
                ['gh', 'release', 'edit', tag, '--notes', cleaned_notes],
                check=False,
                capture_output=True
            )
            if result.returncode == 0:
                print_green("Release notes cleaned up")
            else:
                print_yellow("Warning: Could not update release notes")

    # Get release URL
    result = run_command(
        ['gh', 'release', 'view', tag, '--json', 'url', '-q', '.url'],
        check=False,
        capture_output=True
    )
    release_url = result.stdout.strip() if result.returncode == 0 else None

    if release_url:
        print_green(f"Release URL: {release_url}")

    return release_url


def print_summary(
    config: BuildConfig,
    output_saver: Path,
    project_saver: Path,
    screensaver_zip: Path,
    archive_path: Path,
    exported_app: Path,
    final_zip: Path,
    appcast_path: Optional[Path],
    release_url: Optional[str] = None,
) -> None:
    """Print build summary."""
    saver_size = get_dir_size(output_saver)
    app_size = get_dir_size(exported_app)
    zip_size = get_file_size(final_zip)

    print()
    print_green("========================================")
    print_green("Build Complete!")
    print_green("========================================")

    if config.stage:
        print("Environment: STAGE")
    else:
        print("Environment: PRODUCTION")

    print(f"Configuration: {config.build_config}")

    if config.version:
        print(f"Version: {config.version}")

    print()
    print("Build outputs:")
    print(f"  Screensaver (DerivedData): {config.products_dir / config.screensaver_name} ({saver_size})")
    print(f"  Screensaver (output): {output_saver}")
    print(f"  Screensaver (Resources): {project_saver}")
    print(f"  Screensaver zip: {screensaver_zip}")
    print(f"  App archive: {archive_path}")
    print(f"  App (exported): {exported_app} ({app_size})")
    print()

    if config.notarize:
        print("Notarization: Complete (screensaver and app)")
    else:
        print("Notarization: Skipped")

    if release_url:
        print(f"GitHub Release: {release_url}")
    elif config.github_release:
        print("GitHub Release: Failed")
    else:
        print("GitHub Release: Skipped")

    print()
    print(f"Distribution package: {final_zip} ({zip_size})")

    if appcast_path and appcast_path.exists():
        print(f"Appcast: {appcast_path}")

    print()
    print("To test the app, run:")
    print(f"  open '{exported_app}'")
    print()

    # Show upload instructions if appcast was generated
    if appcast_path and appcast_path.exists():
        print_blue("========================================")
        print_blue("Upload Instructions for Sparkle Updates")
        print_blue("========================================")
        print()
        if release_url:
            print(f"1. GitHub release created: {release_url}")
            print()
            print("2. Upload appcast.xml to your web server:")
        else:
            print(f"1. Create GitHub release for version {config.version}:")
            print(f"   https://github.com/e-dream-ai/client/releases/new?tag={config.version}")
            print()
            print("2. Upload the zip file to the release:")
            print(f"   {final_zip}")
            print()
            print("3. Upload appcast.xml to your web server:")
        print(f"   {appcast_path} -> https://infinidream.ai/alpha/appcast.xml")
        print()

    if config.release and not config.notarize:
        stage_flag = " -s" if config.stage else ""
        print_yellow("Note: This is a Release build without notarization.")
        print_yellow(f"To notarize, run: ./build.py -r -n{stage_flag}")
        print()


def update_version_in_plists(config: BuildConfig) -> None:
    """Update version in Info.plist files if version was specified."""
    if not config.version:
        return

    # Update App Info.plist
    current_version = read_plist_version(config.app_info_plist)
    if current_version != config.plist_version:
        print_yellow(f"Updating {config.app_info_plist} version from {current_version} to {config.plist_version}...")
        update_plist_version(config.app_info_plist, config.plist_version)
        print_green(f"Version updated in {config.app_info_plist}")

    # Update Screensaver Info.plist
    current_version = read_plist_version(config.saver_info_plist)
    if current_version != config.plist_version:
        print_yellow(f"Updating {config.saver_info_plist} version from {current_version} to {config.plist_version}...")
        update_plist_version(config.saver_info_plist, config.plist_version)
        print_green(f"Version updated in {config.saver_info_plist}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description='infinidream Unified Build Script',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Environment variables (optional overrides):
  DEVELOPER_ID_CERT : Code signing certificate name
  TEAM_ID           : Apple Developer Team ID
  KEYCHAIN_PROFILE  : Notarization keychain profile name
"""
    )

    parser.add_argument('-r', '--release', action='store_true',
                        help='Build in Release mode (default: Debug)')
    parser.add_argument('-s', '--stage', action='store_true',
                        help='Build stage version (default: production)')
    parser.add_argument('-n', '--notarize', action='store_true',
                        help='Enable notarization (requires -r)')
    parser.add_argument('-a', '--appcast', action='store_true',
                        help='Generate appcast.xml for Sparkle updates')
    parser.add_argument('-g', '--github', action='store_true',
                        help='Create GitHub release with tag (requires -v)')
    parser.add_argument('-v', '--version', type=str,
                        help='Version string (e.g., 0.12.0) - used for zip naming, appcast, and GitHub release')

    args = parser.parse_args()

    config = BuildConfig(args)
    validate_config(config)
    print_build_info(config)

    # Update version in plists
    update_version_in_plists(config)

    # Clean previous builds
    clean_previous_builds(config.build_dir)

    # Create output directory
    output_dir = config.build_dir / config.build_config
    output_dir.mkdir(parents=True, exist_ok=True)

    # Step 0: Re-sign Sparkle.framework
    step0_resign_sparkle(config)

    # Step 1: Build screensaver
    screensaver_path = step1_build_screensaver(config)

    # Copy screensaver to various locations
    output_saver, project_saver, screensaver_zip = copy_screensaver(config, screensaver_path)

    # Step 2: Notarize screensaver
    step2_notarize_screensaver(config, project_saver, screensaver_zip)

    # Step 3: Archive app
    archive_path = step3_archive_app(config)

    # Step 4: Export app
    exported_app = step4_export_app(config, archive_path)

    # Step 5: Notarize app
    step5_notarize_app(config, exported_app)

    # Step 6: Create distribution package
    final_zip = step6_create_distribution(config, exported_app, output_saver)

    # Step 7: Generate appcast
    appcast_path = step7_generate_appcast(config, final_zip)

    # Step 8: GitHub release
    release_url = step8_github_release(config, final_zip)

    # Print summary
    print_summary(
        config, output_saver, project_saver, screensaver_zip,
        archive_path, exported_app, final_zip, appcast_path, release_url
    )


if __name__ == '__main__':
    main()
