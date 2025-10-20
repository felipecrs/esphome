#!/usr/bin/env python3
"""
Pre-cache PlatformIO GitHub Downloads

This script extracts GitHub URLs from platformio.ini and pre-caches them
to avoid redundant downloads when switching between ESP8266 and ESP32 builds.

Usage:
    python3 script/cache_platformio_downloads.py [platformio.ini]
"""

import argparse
import configparser
from pathlib import Path
import re
import sys

# Import the cache manager
sys.path.insert(0, str(Path(__file__).parent.parent))
from esphome.github_cache import GitHubCache


def extract_github_urls(platformio_ini: Path) -> list[str]:
    """Extract all GitHub URLs from platformio.ini.

    Args:
        platformio_ini: Path to platformio.ini file

    Returns:
        List of GitHub URLs found
    """
    config = configparser.ConfigParser(inline_comment_prefixes=(";",))
    config.read(platformio_ini)

    urls = []
    github_pattern = re.compile(r"https://github\.com/[^\s;]+\.zip")

    for section in config.sections():
        conf = config[section]

        # Check platform
        if "platform" in conf:
            platform_value = conf["platform"]
            matches = github_pattern.findall(platform_value)
            urls.extend(matches)

        # Check platform_packages
        if "platform_packages" in conf:
            for line in conf["platform_packages"].splitlines():
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                matches = github_pattern.findall(line)
                urls.extend(matches)

    # Remove duplicates while preserving order using dict
    return list(dict.fromkeys(urls))


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="Pre-cache PlatformIO GitHub downloads",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
This script scans platformio.ini for GitHub URLs and pre-caches them.
This avoids redundant downloads when switching between platforms (e.g., ESP8266 and ESP32).

Examples:
  # Cache downloads from default platformio.ini
  %(prog)s

  # Cache downloads from specific file
  %(prog)s custom_platformio.ini

  # Show what would be cached without downloading
  %(prog)s --dry-run
        """,
    )

    parser.add_argument(
        "platformio_ini",
        nargs="?",
        default="platformio.ini",
        help="Path to platformio.ini (default: platformio.ini)",
    )

    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would be cached without downloading",
    )

    parser.add_argument(
        "--cache-dir",
        type=Path,
        help="Cache directory (default: ~/.platformio/esphome_download_cache)",
    )

    parser.add_argument(
        "--force",
        action="store_true",
        help="Force re-download even if cached",
    )

    args = parser.parse_args()

    platformio_ini = Path(args.platformio_ini)

    if not platformio_ini.exists():
        print(f"Error: {platformio_ini} not found", file=sys.stderr)
        return 1

    # Extract URLs
    print(f"Scanning {platformio_ini} for GitHub URLs...")
    urls = extract_github_urls(platformio_ini)

    if not urls:
        print("No GitHub URLs found in platformio.ini")
        return 0

    print(f"Found {len(urls)} unique GitHub URL(s):")
    for url in urls:
        print(f"  - {url}")
    print()

    if args.dry_run:
        print("Dry run - not downloading")
        return 0

    # Initialize cache (use PlatformIO directory by default)
    cache_dir = args.cache_dir
    if cache_dir is None:
        cache_dir = Path.home() / ".platformio" / "esphome_download_cache"
    cache = GitHubCache(cache_dir)

    # Cache each URL
    success_count = 0
    for i, url in enumerate(urls, 1):
        print(f"[{i}/{len(urls)}] Checking {url}")
        try:
            # Use the download_with_progress from github_download_cache CLI
            from script.github_download_cache import download_with_progress

            download_with_progress(cache, url, force=args.force, check_updates=True)
            success_count += 1
            print()
        except Exception as e:
            print(f"Error caching {url}: {e}", file=sys.stderr)
            print()

    # Show cache stats
    total_size = cache.cache_size()
    size_mb = total_size / (1024 * 1024)
    print("\nCache summary:")
    print(f"  Successfully cached: {success_count}/{len(urls)}")
    print(f"  Total cache size: {size_mb:.2f} MB")
    print(f"  Cache location: {cache.cache_dir}")

    return 0 if success_count == len(urls) else 1


if __name__ == "__main__":
    sys.exit(main())
