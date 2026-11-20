#!/usr/bin/env python3
"""
infinidream Release Script

Generates appcast.xml, fetches release notes from GitHub, adds them to appcast.xml,
and publishes to the landing-page repository. Production releases also update the
Homebrew cask in e-dream-ai/homebrew-tap and open a winget-pkgs PR for the
Windows installer.

Usage:
    ./release.py -v 0.14.0           # Publish to alpha/appcast.xml
    ./release.py -v 0.14.0 -s        # Publish to stage/appcast.xml
"""

import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Optional


# ANSI color codes
class Colors:
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    NC = '\033[0m'


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
    check: bool = True,
    capture_output: bool = False,
) -> subprocess.CompletedProcess:
    """Run a command and return the result."""
    try:
        result = subprocess.run(
            cmd,
            check=check,
            capture_output=capture_output,
            text=True,
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


def run_command_with_output(cmd: list[str]) -> str:
    """Run a command and return its output."""
    result = run_command(cmd, capture_output=True, check=False)
    return result.stdout.strip() if result.stdout else ""


def find_generate_appcast_tool(script_dir: Path) -> Optional[str]:
    """Find the Sparkle generate_appcast tool."""
    locations = [
        "/usr/local/bin/generate_appcast",
        Path.home() / "bin" / "generate_appcast",
        script_dir / "bin" / "generate_appcast",
    ]

    for loc in locations:
        loc_str = str(loc)
        if os.path.isfile(loc_str) and os.access(loc_str, os.X_OK):
            return loc_str

    # Check if it's in PATH
    if shutil.which("generate_appcast"):
        return "generate_appcast"

    return None


def generate_appcast(version: str, package_path: Path, output_dir: Path, script_dir: Path) -> Optional[Path]:
    """Generate appcast.xml using Sparkle's generate_appcast tool.

    package_path should be the distribution DMG (or other Sparkle-supported archive).
    Returns path to generated appcast.xml or None on failure.
    """
    print_yellow("Generating appcast.xml...")

    # Find the generate_appcast tool
    generate_appcast_tool = find_generate_appcast_tool(script_dir)
    if not generate_appcast_tool:
        print_red("Error: generate_appcast tool not found")
        print()
        print("Please download Sparkle tools from:")
        print("  https://github.com/sparkle-project/Sparkle/releases")
        print()
        print("Extract and place generate_appcast in one of these locations:")
        print("  - /usr/local/bin/generate_appcast")
        print("  - ~/bin/generate_appcast")
        print(f"  - {script_dir}/bin/generate_appcast")
        print()
        print("Or add it to your PATH.")
        return None

    print(f"Using generate_appcast: {generate_appcast_tool}")

    # Create a temporary directory for appcast generation
    with tempfile.TemporaryDirectory() as appcast_dir:
        # Copy package (DMG) to temp directory
        package_filename = package_path.name
        temp_package = Path(appcast_dir) / package_filename
        shutil.copy2(package_path, temp_package)

        # Construct download URL
        download_url_prefix = "https://github.com/e-dream-ai/client/releases/download"
        download_url = f"{download_url_prefix}/{version}/{package_filename}"
        print(f"Download URL: {download_url}")

        # Run generate_appcast with download URL prefix
        print_yellow("Running Sparkle's generate_appcast...")
        result = run_command(
            [generate_appcast_tool, "--download-url-prefix", f"{download_url_prefix}/{version}/", appcast_dir],
            check=False,
            capture_output=True
        )

        if result.returncode != 0:
            print_red("Error: generate_appcast failed")
            if result.stderr:
                print(result.stderr)
            if result.stdout:
                print(result.stdout)
            return None

        # Check if appcast was generated
        generated_appcast = Path(appcast_dir) / "appcast.xml"
        if not generated_appcast.exists():
            print_red("Error: generate_appcast did not create appcast.xml")
            return None

        # Read the generated content
        appcast_content = generated_appcast.read_text()

        # Update version in appcast.xml to match the -v argument
        # Sparkle's generate_appcast extracts version from the app bundle
        import re as re_module
        bundle_version_match = re_module.search(r'<sparkle:version>([^<]*)</sparkle:version>', appcast_content)
        if bundle_version_match:
            bundle_version = bundle_version_match.group(1)
            if bundle_version != version:
                print(f"Bundle version: {bundle_version} -> Updating to: {version}")
                appcast_content = appcast_content.replace(
                    f"<title>{bundle_version}</title>",
                    f"<title>{version}</title>"
                )
                appcast_content = appcast_content.replace(
                    f"<sparkle:version>{bundle_version}</sparkle:version>",
                    f"<sparkle:version>{version}</sparkle:version>"
                )
                appcast_content = appcast_content.replace(
                    f"<sparkle:shortVersionString>{bundle_version}</sparkle:shortVersionString>",
                    f"<sparkle:shortVersionString>{version}</sparkle:shortVersionString>"
                )

        # Check if signature was added
        if 'sparkle:edSignature' in appcast_content:
            print_green("EdDSA signature found in appcast")
        else:
            print_yellow("Warning: No EdDSA signature found in appcast")
            print()
            print("The appcast needs to be signed for Sparkle to accept updates.")
            print("Make sure the Sparkle private key is in your Keychain.")
            print()

        # Copy appcast to output directory
        appcast_path = output_dir / "appcast.xml"
        appcast_path.write_text(appcast_content)

        print_green(f"Appcast generated: {appcast_path}")
        return appcast_path


def get_release_notes(version: str) -> Optional[str]:
    """Fetch release notes from GitHub for the given version."""
    result = run_command(
        ['gh', 'release', 'view', version, '--repo', 'e-dream-ai/client',
         '--json', 'body', '-q', '.body'],
        check=False,
        capture_output=True
    )
    if result.returncode != 0:
        return None
    return result.stdout.strip()


def linkify_issue_references(markdown: str, repo: str = "e-dream-ai/client") -> str:
    """Convert #123 style references to GitHub issue links.

    Converts #123 to [#123](https://github.com/e-dream-ai/client/issues/123)
    """
    def replace_issue(match):
        issue_num = match.group(1)
        return f"[#{issue_num}](https://github.com/{repo}/issues/{issue_num})"

    # Match #123 but not already inside a markdown link or URL
    # Negative lookbehind for [ or / to avoid matching already-linked or URL refs
    return re.sub(r'(?<![\[\w/])#(\d+)\b', replace_issue, markdown)


def add_release_notes_to_appcast(appcast_path: Path, release_notes_markdown: str) -> str:
    """Add markdown release notes to appcast.xml, returning the modified content.

    Uses Sparkle's native markdown support (sparkle:format="markdown") which
    requires Sparkle 2.9+ and macOS 12+.
    """
    if not appcast_path.exists():
        print_red(f"Appcast file not found: {appcast_path}")
        sys.exit(1)

    content = appcast_path.read_text()

    # Ensure sparkle namespace is declared in the rss tag
    if 'xmlns:sparkle=' not in content:
        content = content.replace(
            '<rss ',
            '<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" '
        )

    # The new description tag with markdown format
    new_description = f'<description sparkle:format="markdown">{release_notes_markdown}</description>'

    # Look for existing description tags and replace
    if '<description sparkle:format="markdown">' in content:
        # Replace existing markdown description
        content = re.sub(
            r'<description sparkle:format="markdown">.*?</description>',
            new_description,
            content,
            flags=re.DOTALL
        )
    elif '<description></description>' in content:
        # Replace empty description
        content = content.replace('<description></description>', new_description)
    elif '<description><![CDATA[' in content:
        # Replace existing CDATA description
        content = re.sub(
            r'<description><!\[CDATA\[.*?\]\]></description>',
            new_description,
            content,
            flags=re.DOTALL
        )
    elif '<description>' in content:
        # Replace existing description
        content = re.sub(
            r'<description[^>]*>.*?</description>',
            new_description,
            content,
            flags=re.DOTALL
        )
    else:
        # Add description before </item>
        content = content.replace(
            '</item>',
            f'            {new_description}\n        </item>'
        )

    return content


def get_file_from_github(
    repo: str,
    path: str,
    ref: Optional[str] = None,
) -> tuple[Optional[str], Optional[str]]:
    """Get file content and SHA from GitHub. Returns (content, sha) or (None, None)."""
    sha = get_sha_from_github(repo, path, ref=ref)
    if not sha:
        return None, None
    # The base64 content is wrapped across lines, so decode all of it.
    ref_arg = f"?ref={ref}" if ref else ""
    result = run_command(
        ['gh', 'api', f'/repos/{repo}/contents/{path}{ref_arg}', '--jq', '.content'],
        check=False,
        capture_output=True
    )
    if result.returncode != 0:
        return None, None
    try:
        content = base64.b64decode(''.join(result.stdout.split())).decode('utf-8')
    except Exception:
        return None, None
    return content, sha


def get_sha_from_github(repo: str, path: str, ref: Optional[str] = None) -> Optional[str]:
    """Get current blob SHA of a file from GitHub. Works even when content is not returned (e.g. large file)."""
    url = f'/repos/{repo}/contents/{path}'
    if ref:
        url += f'?ref={ref}'
    cmd = ['gh', 'api', url, '--jq', '.sha']
    result = run_command(cmd, check=False, capture_output=True)
    if result.returncode != 0:
        return None
    sha = (result.stdout or '').strip()
    return sha if sha else None


def get_default_branch(repo: str) -> str:
    """Get default branch of repo (e.g. main)."""
    result = run_command(
        ['gh', 'api', f'/repos/{repo}', '--jq', '.default_branch'],
        check=False,
        capture_output=True
    )
    if result.returncode == 0 and result.stdout:
        return result.stdout.strip()
    return 'main'


def update_file_on_github(
    repo: str, path: str, content: str, sha: Optional[str], message: str, branch: Optional[str] = None
) -> tuple[bool, str]:
    """Update or create a file on GitHub. Returns (success, error_message)."""
    content_b64 = base64.b64encode(content.encode('utf-8')).decode('utf-8')

    args = [
        'gh', 'api', '-X', 'PUT', f'/repos/{repo}/contents/{path}',
        '-f', f'message={message}',
        '-f', f'content={content_b64}',
    ]
    if sha:
        args += ['-f', f'sha={sha}']
    if branch:
        args += ['-f', f'branch={branch}']
    result = run_command(args, check=False, capture_output=True)

    if result.returncode == 0:
        return True, ""
    err = (result.stderr or result.stdout or "").strip()
    return False, err


HOMEBREW_TAP_REPO = "e-dream-ai/homebrew-tap"
HOMEBREW_CASK_PATH = "Casks/infinidream.rb"


def get_release_asset_sha256(version: str, asset_name: str) -> Optional[str]:
    """sha256 of an uploaded release asset, as reported by GitHub, or None."""
    result = run_command(
        ['gh', 'api', f'/repos/e-dream-ai/client/releases/tags/{version}',
         '--jq', f'.assets[] | select(.name == "{asset_name}") | .digest'],
        check=False,
        capture_output=True
    )
    digest = (result.stdout or '').strip() if result.returncode == 0 else ''
    if not digest.startswith('sha256:'):
        return None
    return digest[len('sha256:'):]


def update_cask_text(cask: str, version: str, sha256: str) -> Optional[str]:
    """Set version and sha256 in the cask. None if either line isn't found."""
    cask, n_version = re.subn(r'^(\s*version\s+)"[^"]*"', rf'\g<1>"{version}"', cask, count=1, flags=re.M)
    cask, n_sha = re.subn(r'^(\s*sha256\s+)"[^"]*"', rf'\g<1>"{sha256}"', cask, count=1, flags=re.M)
    if n_version != 1 or n_sha != 1:
        return None
    return cask


def update_homebrew_cask(version: str, dmg_path: Path) -> bool:
    """Point the Homebrew cask at this release's DMG (#853). Returns success."""
    print_yellow(f"Updating Homebrew cask in {HOMEBREW_TAP_REPO}...")

    # The cask must match what users download, so take the hash GitHub reports
    # for the uploaded asset, and refuse if it differs from the local build.
    remote_sha = get_release_asset_sha256(version, dmg_path.name)
    if not remote_sha:
        print_red(f"Release {version} has no uploaded asset named {dmg_path.name}")
        return False
    local_sha = hashlib.sha256(dmg_path.read_bytes()).hexdigest()
    if local_sha != remote_sha:
        print_red(f"{dmg_path.name} on GitHub differs from the local build:")
        print(f"  GitHub: {remote_sha}")
        print(f"  local:  {local_sha}")
        return False

    branch = get_default_branch(HOMEBREW_TAP_REPO)
    cask, sha = get_file_from_github(HOMEBREW_TAP_REPO, HOMEBREW_CASK_PATH, ref=branch)
    if cask is None:
        print_red(f"Could not read {HOMEBREW_TAP_REPO}/{HOMEBREW_CASK_PATH}")
        return False
    updated = update_cask_text(cask, version, remote_sha)
    if updated is None:
        print_red("Could not find the version/sha256 lines in the cask")
        return False
    if updated == cask:
        print_green(f"Homebrew cask already at {version}")
        return True

    ok, err_msg = update_file_on_github(
        HOMEBREW_TAP_REPO, HOMEBREW_CASK_PATH, updated, sha,
        f"infinidream {version}", branch=branch
    )
    if not ok:
        print_red("Failed to update the Homebrew cask")
        if err_msg:
            print(err_msg)
        return False
    print_green(f"Homebrew cask updated to {version}")
    return True


WINGET_PACKAGE_ID = "infinidream.Infinidream"
# komac pushes each version's branch to <owner>/winget-pkgs and opens the PR from there.
# The fork is the org's, not a person's; the PR's author is whoever owns the token.
WINGET_FORK_OWNER = "e-dream-ai"


def windows_installer_name(version: str) -> str:
    """Name WinBuild/release.py gives the NSIS setup exe it uploads."""
    return f"infinidream-{version}.exe"


def windows_installer_url(version: str) -> str:
    return (f"https://github.com/e-dream-ai/client/releases/download/"
            f"{version}/{windows_installer_name(version)}")


def winget_update_command(version: str) -> list[str]:
    return ['komac', 'update', WINGET_PACKAGE_ID, '--version', version,
            '--urls', windows_installer_url(version), '--submit']


def winget_manual_command(version: str) -> str:
    return f"KOMAC_FORK_OWNER={WINGET_FORK_OWNER} {' '.join(winget_update_command(version))}"


def komac_env() -> dict[str, str]:
    """komac's environment: the org fork, and a token (GITHUB_TOKEN, else the gh login)."""
    env = dict(os.environ, KOMAC_FORK_OWNER=WINGET_FORK_OWNER)
    if not env.get('GITHUB_TOKEN'):
        result = run_command(['gh', 'auth', 'token'], check=False, capture_output=True)
        token = (result.stdout or '').strip()
        if result.returncode == 0 and token:
            env['GITHUB_TOKEN'] = token
    return env


def update_winget(version: str) -> bool:
    """Open a winget-pkgs PR for this release's Windows installer (#575). Returns success.

    komac downloads the installer, hashes it, pushes a branch to the e-dream-ai fork of
    microsoft/winget-pkgs and opens the PR from it, as the user who owns the token.
    Microsoft's bots then validate it; the PR merges on its own if validation passes.
    """
    print_yellow(f"Submitting {WINGET_PACKAGE_ID} {version} to winget...")

    # winget pins the installer's sha256, so it has to be the final upload. Never
    # re-upload this exe once the PR is open: ship a new version instead.
    if not get_release_asset_sha256(version, windows_installer_name(version)):
        print_red(f"Release {version} has no {windows_installer_name(version)}; "
                  f"run WinBuild/release.py -g first")
        return False
    if not shutil.which('komac'):
        print_red("komac not found (brew install komac)")
        return False

    env = komac_env()
    if not env.get('GITHUB_TOKEN'):
        print_red("No GitHub token: set GITHUB_TOKEN or run gh auth login")
        return False
    result = subprocess.run(winget_update_command(version), env=env)
    if result.returncode != 0:
        print_red("komac failed")
        return False
    print_green(f"winget PR opened for {WINGET_PACKAGE_ID} {version}")
    return True


def main() -> None:
    parser = argparse.ArgumentParser(
        description='Generate and publish appcast.xml with release notes to landing-page repo',
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    parser.add_argument('-v', '--version', type=str, required=True,
                        help='Version string (e.g., 0.14.0)')
    parser.add_argument('-s', '--stage', action='store_true',
                        help='Publish to stage/appcast.xml (default: alpha/appcast.xml)')
    parser.add_argument('-n', '--dry-run', action='store_true',
                        help='Show what would be published without actually doing it')

    args = parser.parse_args()

    version = args.version
    stage = args.stage
    dry_run = args.dry_run

    # Determine paths
    script_dir = Path(__file__).parent
    build_config = "Debug" if stage else "Release"
    build_dir = script_dir / "build" / build_config
    local_appcast = build_dir / "appcast.xml"

    # Determine DMG path
    if stage:
        dmg_path = build_dir / f"infinidream-{version}-stage.dmg"
    else:
        dmg_path = build_dir / f"infinidream-{version}.dmg"

    target_repo = "e-dream-ai/landing-page"
    frontend_repo = "e-dream-ai/frontend"
    frontend_version_path = "src/version.ts"
    frontend_branch = "stage" if stage else "main"
    if stage:
        target_path = "public/stage/appcast.xml"
    else:
        target_path = "public/alpha/appcast.xml"

    print("========================================")
    if dry_run:
        print("infinidream Release (DRY RUN)")
    else:
        print("infinidream Release")
    print("========================================")
    print(f"Version: {version}")
    print(f"DMG file: {dmg_path}")
    print(f"Target: {target_repo}/{target_path}")
    print(f"Frontend version target: {frontend_repo}/{frontend_version_path} (branch: {frontend_branch})")
    if dry_run:
        print_yellow("DRY RUN - no changes will be made")
    print()

    # Verify DMG file exists
    if not dmg_path.exists():
        print_red(f"Error: DMG file not found: {dmg_path}")
        print()
        print("Run build.py first to create the distribution package:")
        print(f"  ./build.py -r -n -g -v {version}" + (" -s" if stage else ""))
        sys.exit(1)

    # Step 1: Generate appcast.xml
    print()
    print("========================================")
    print("Step 1: Generating Appcast")
    print("========================================")
    appcast_path = generate_appcast(version, dmg_path, build_dir, script_dir)
    if not appcast_path:
        print_red("Failed to generate appcast.xml")
        sys.exit(1)
    print()

    # Step 2: Fetch release notes from GitHub
    print("========================================")
    print("Step 2: Fetching Release Notes")
    print("========================================")
    print_yellow("Fetching release notes from GitHub...")
    release_notes = get_release_notes(version)
    if not release_notes:
        print_red(f"Failed to fetch release notes for version {version}")
        print("Make sure the release exists: gh release view {version} --repo e-dream-ai/client")
        sys.exit(1)
    print_green("Release notes fetched")
    print()
    print("Release notes preview:")
    print("-" * 40)
    # Show first 500 chars
    preview = release_notes[:500] + "..." if len(release_notes) > 500 else release_notes
    print(preview)
    print("-" * 40)
    print()

    # Step 3: Process release notes and add to appcast
    print("========================================")
    print("Step 3: Adding Release Notes to Appcast")
    print("========================================")
    print_yellow("Processing release notes...")
    release_notes = linkify_issue_references(release_notes)
    print_green("Issue references linked (e.g., #123 -> GitHub link)")

    print_yellow(f"Reading local appcast from {local_appcast}...")
    updated_appcast = add_release_notes_to_appcast(local_appcast, release_notes)
    print_green("Release notes added to appcast (markdown format)")
    print()

    # Step 4: Get current file SHA from GitHub (needed for update)
    print("========================================")
    print("Step 4: Publishing to GitHub")
    print("========================================")
    default_branch = get_default_branch(target_repo)
    print_yellow(f"Checking existing file on GitHub (branch: {default_branch})...")
    existing_sha = get_sha_from_github(target_repo, target_path, ref=default_branch)
    if existing_sha:
        print(f"Existing file found (SHA: {existing_sha[:7]}...)")
    else:
        print("No existing file found, will create new")

    if dry_run:
        # Dry run - show what would be published
        print_yellow("DRY RUN - Final appcast.xml content:")
        print()
        print("=" * 60)
        print(updated_appcast)
        print("=" * 60)
        print()

        # Save to a local file for inspection
        dry_run_path = script_dir / "build" / build_config / "appcast-preview.xml"
        dry_run_path.write_text(updated_appcast)
        print_green(f"Preview saved to: {dry_run_path}")
        print()
        print_yellow("DRY RUN - Would perform these actions:")
        print(f"  1. Publish appcast.xml to {target_repo}/{target_path}")
        if not stage:
            print(f"  2. Update frontend version file on {frontend_repo} ({frontend_branch})")
            print(f"  3. Mark release {version} as latest (remove prerelease)")
            print(f"  4. Update Homebrew cask in {HOMEBREW_TAP_REPO} to {version}")
            print(f"  5. Open a winget-pkgs PR: {winget_manual_command(version)}")
        else:
            print("  2. (Stage: skip frontend version update)")
            print("  3. (Stage: skip marking as latest)")
            print("  4. (Stage: skip Homebrew cask)")
        print()
        print("Run without --dry-run to execute for real.")
        return

    # Push to GitHub (refetch SHA right before update to avoid 409 conflict)
    print_yellow(f"Publishing appcast.xml to {target_repo}...")
    commit_message = f"Update appcast.xml for v{version}"
    existing_sha = get_sha_from_github(target_repo, target_path, ref=default_branch)
    ok, err_msg = update_file_on_github(
        target_repo, target_path, updated_appcast, existing_sha, commit_message, branch=default_branch
    )
    if not ok and "409" in err_msg:
        # File changed since we fetched SHA; refetch and retry once
        print_yellow("File changed on GitHub, retrying with latest SHA...")
        existing_sha = get_sha_from_github(target_repo, target_path, ref=default_branch)
        ok, err_msg = update_file_on_github(
            target_repo, target_path, updated_appcast, existing_sha, commit_message, branch=default_branch
        )
    if not ok:
        print_red("Failed to publish appcast.xml")
        if err_msg:
            print(err_msg)
        print("Check that you have write access to the repository.")
        print(f"Try: gh repo view {target_repo}")
        sys.exit(1)

    print_green("Appcast published successfully!")

    # Step 5: Update frontend version file (only for production releases)
    if not stage:
        print_yellow("Updating frontend version file...")
        frontend_version_content = f'export const APP_VERSION = "{version}";\n'
        frontend_sha = get_sha_from_github(
            frontend_repo,
            frontend_version_path,
            ref=frontend_branch,
        )
        ok, err_msg = update_file_on_github(
            frontend_repo,
            frontend_version_path,
            frontend_version_content,
            frontend_sha,
            f"Update app version to {version}",
            branch=frontend_branch,
        )
        if not ok:
            print_red("Failed to update frontend version file")
            if err_msg:
                print(err_msg)
            print(f"Check that you have write access to {frontend_repo} and that branch '{frontend_branch}' exists.")
            sys.exit(1)

        print_green("Frontend version file updated!")
    else:
        print("Stage release: skipping frontend version update")

    # Step 6: Mark GitHub release as latest (only when not stage)
    if not stage:
        print_yellow("Marking GitHub release as latest...")
        result = run_command(
            ['gh', 'release', 'edit', version, '--repo', 'e-dream-ai/client',
             '--prerelease=false', '--latest'],
            check=False,
            capture_output=True
        )
        if result.returncode == 0:
            print_green("Release marked as latest")
        else:
            print_yellow("Warning: Could not update release status")
            if result.stderr:
                print(result.stderr)
    else:
        print("Stage release: skipping marking as latest")

    # Step 7: Update the Homebrew cask (only when not stage). Everything else
    # is published by now, so a failure here warns instead of aborting.
    cask_ok = True
    if not stage:
        cask_ok = update_homebrew_cask(version, dmg_path)
        if not cask_ok:
            print_yellow(f"Warning: update version and sha256 in {HOMEBREW_TAP_REPO}/{HOMEBREW_CASK_PATH} by hand")
    else:
        print("Stage release: skipping Homebrew cask")

    # Step 8: Submit the Windows installer to winget (only when not stage). Also
    # warns instead of aborting.
    winget_ok = True
    if not stage:
        winget_ok = update_winget(version)
        if not winget_ok:
            print_yellow(f"Warning: submit to winget by hand: {winget_manual_command(version)}")
    else:
        print("Stage release: skipping winget")

    print()
    print_green("========================================")
    print_green("Release Complete!")
    print_green("========================================")
    if stage:
        print(f"Appcast URL: https://infinidream.ai/stage/appcast.xml")
    else:
        print(f"Appcast URL: https://infinidream.ai/alpha/appcast.xml")
    print(f"GitHub Release: https://github.com/e-dream-ai/client/releases/tag/{version}")
    if not stage and cask_ok:
        print("Homebrew: brew install --cask e-dream-ai/tap/infinidream")
    if not stage and winget_ok:
        print(f"winget: winget install {WINGET_PACKAGE_ID} (once the winget-pkgs PR merges)")
    print()
    print("Users will receive the update notification on next app launch.")


if __name__ == '__main__':
    main()
