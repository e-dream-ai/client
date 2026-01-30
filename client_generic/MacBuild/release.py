#!/usr/bin/env python3
"""
infinidream Release Script

Fetches release notes from GitHub, adds them to appcast.xml,
and publishes to the landing-page repository.

Usage:
    ./release.py -v 0.14.0           # Publish to alpha/appcast.xml
    ./release.py -v 0.14.0 -s        # Publish to stage/appcast.xml
"""

import argparse
import base64
import json
import re
import subprocess
import sys
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
    return re.sub(r'(?<![[\w/])#(\d+)\b', replace_issue, markdown)


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
    ref_arg = f"?ref={ref}" if ref else ""
    result = run_command(
        ['gh', 'api', f'/repos/{repo}/contents/{path}{ref_arg}',
         '--jq', '.content, .sha'],
        check=False,
        capture_output=True
    )
    if result.returncode != 0:
        return None, None

    lines = result.stdout.strip().split('\n')
    if len(lines) >= 2:
        content_b64 = lines[0]
        sha = lines[1]
        try:
            content = base64.b64decode(content_b64).decode('utf-8')
            return content, sha
        except Exception:
            return None, None
    return None, None


def update_file_on_github(
    repo: str,
    path: str,
    content: str,
    sha: Optional[str],
    message: str,
    branch: Optional[str] = None,
) -> bool:
    """Update or create a file on GitHub. Returns True on success."""
    content_b64 = base64.b64encode(content.encode('utf-8')).decode('utf-8')

    # Build the request body
    body = {
        'message': message,
        'content': content_b64,
    }
    if sha:
        body['sha'] = sha

    cmd = [
        'gh', 'api', '-X', 'PUT', f'/repos/{repo}/contents/{path}',
        '-f', f'message={message}',
        '-f', f'content={content_b64}',
    ]
    if sha:
        cmd += ['-f', f'sha={sha}']
    if branch:
        cmd += ['-f', f'branch={branch}']

    result = run_command(cmd, check=False, capture_output=True)

    return result.returncode == 0


def main() -> None:
    parser = argparse.ArgumentParser(
        description='Publish appcast.xml with release notes to landing-page repo',
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
    build_config = "Release"  # Appcast is only generated for release builds
    local_appcast = script_dir / "build" / build_config / "appcast.xml"

    target_repo = "e-dream-ai/landing-page"
    frontend_repo = "e-dream-ai/frontend"
    frontend_version_path = "src/version.ts"
    frontend_branch = "stage" if stage else "main"
    if stage:
        target_path = "public/stage/appcast.xml"
    else:
        target_path = "public/alpha/appcast.xml"

    print_blue("========================================")
    if dry_run:
        print_blue("infinidream Release (DRY RUN)")
    else:
        print_blue("infinidream Release")
    print_blue("========================================")
    print(f"Version: {version}")
    print(f"Target: {target_repo}/{target_path}")
    print(f"Frontend version target: {frontend_repo}/{frontend_version_path} (branch: {frontend_branch})")
    if dry_run:
        print_yellow("DRY RUN - no changes will be made")
    print()

    # Step 1: Fetch release notes from GitHub
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

    # Step 2: Process release notes and add to appcast
    print_yellow("Processing release notes...")
    release_notes = linkify_issue_references(release_notes)
    print_green("Issue references linked (e.g., #123 -> GitHub link)")

    print_yellow(f"Reading local appcast from {local_appcast}...")
    if not local_appcast.exists():
        print_red(f"Local appcast not found: {local_appcast}")
        print("Run build.py with -a flag first to generate appcast.xml")
        sys.exit(1)

    updated_appcast = add_release_notes_to_appcast(local_appcast, release_notes)
    print_green("Release notes added to appcast (markdown format)")

    # Step 3: Get current file SHA from GitHub (needed for update)
    print_yellow(f"Checking existing file on GitHub...")
    _, existing_sha = get_file_from_github(target_repo, target_path)
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
        print(f"  2. Update frontend version file on {frontend_repo} ({frontend_branch})")
        print(f"  3. Mark release {version} as latest (remove prerelease)")
        print()
        print("Run without --dry-run to execute for real.")
        return

    # Step 4: Push to GitHub
    print_yellow(f"Publishing appcast.xml to {target_repo}...")
    commit_message = f"Update appcast.xml for v{version}"

    if not update_file_on_github(target_repo, target_path, updated_appcast, existing_sha, commit_message):
        print_red("Failed to publish appcast.xml")
        print("Check that you have write access to the repository.")
        print(f"Try: gh repo view {target_repo}")
        sys.exit(1)

    print_green("Appcast published successfully!")

    # Step 5: Update frontend version file
    print_yellow("Updating frontend version file...")
    frontend_version_content = f'export const APP_VERSION = "{version}";\n'
    _, frontend_sha = get_file_from_github(
        frontend_repo,
        frontend_version_path,
        ref=frontend_branch,
    )
    if not update_file_on_github(
        frontend_repo,
        frontend_version_path,
        frontend_version_content,
        frontend_sha,
        f"Update app version to {version}",
        branch=frontend_branch,
    ):
        print_red("Failed to update frontend version file")
        print(f"Check that you have write access to {frontend_repo} and that branch '{frontend_branch}' exists.")
        sys.exit(1)

    print_green("Frontend version file updated!")

    # Step 6: Mark GitHub release as latest (remove prerelease label)
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

    print()
    print_green("========================================")
    print_green("Release Complete!")
    print_green("========================================")
    if stage:
        print(f"Appcast URL: https://infinidream.ai/stage/appcast.xml")
    else:
        print(f"Appcast URL: https://infinidream.ai/alpha/appcast.xml")
    print(f"GitHub Release: https://github.com/e-dream-ai/client/releases/tag/{version}")
    print()
    print("Users will receive the update notification on next app launch.")


if __name__ == '__main__':
    main()
