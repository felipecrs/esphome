#!/usr/bin/env python3
"""
GitHub Download Cache CLI

This script provides a command-line interface to the GitHub download cache.
The actual caching logic is in esphome/github_cache.py.

Usage:
    python3 script/github_download_cache.py download URL
    python3 script/github_download_cache.py list
    python3 script/github_download_cache.py stats
    python3 script/github_download_cache.py clear
"""

import argparse
from pathlib import Path
import sys
import urllib.request

# Add parent directory to path to import esphome modules
sys.path.insert(0, str(Path(__file__).parent.parent))

from esphome.github_cache import GitHubCache


def download_with_progress(
    cache: GitHubCache, url: str, force: bool = False, check_updates: bool = True
) -> Path:
    """Download a URL with progress indicator and caching.

    Args:
        cache: GitHubCache instance
        url: URL to download
        force: Force re-download even if cached
        check_updates: Check for updates using HTTP 304

    Returns:
        Path to cached file
    """
    # If force, skip cache check
    if not force:
        cached_path = cache.get_cached_path(url, check_updates=check_updates)
        if cached_path:
            print(f"Using cached file for {url}")
            print(f"  Cache: {cached_path}")
            return cached_path

    # Need to download
    print(f"Downloading {url}")
    cache_path = cache._get_cache_path(url)
    print(f"  Cache: {cache_path}")

    # Download with progress
    temp_path = cache_path.with_suffix(cache_path.suffix + ".tmp")

    try:
        with urllib.request.urlopen(url) as response:
            total_size = int(response.headers.get("Content-Length", 0))
            downloaded = 0

            with open(temp_path, "wb") as f:
                while True:
                    chunk = response.read(8192)
                    if not chunk:
                        break
                    f.write(chunk)
                    downloaded += len(chunk)

                    if total_size > 0:
                        percent = (downloaded / total_size) * 100
                        print(f"\r  Progress: {percent:.1f}%", end="", flush=True)

            print()  # New line after progress

        # Move to final location
        temp_path.replace(cache_path)

        # Let cache handle metadata
        cache.save_to_cache(url, cache_path)

        return cache_path

    except (OSError, urllib.error.URLError) as e:
        if temp_path.exists():
            temp_path.unlink()
        raise RuntimeError(f"Failed to download {url}: {e}") from e


def main():
    """CLI entry point."""
    parser = argparse.ArgumentParser(
        description="GitHub Download Cache Manager",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Download and cache a URL
  %(prog)s download https://github.com/pioarduino/registry/releases/download/0.0.1/esptoolpy-v5.1.0.zip

  # List cached files
  %(prog)s list

  # Show cache statistics
  %(prog)s stats

  # Clear cache
  %(prog)s clear
        """,
    )

    parser.add_argument(
        "--cache-dir",
        type=Path,
        help="Cache directory (default: ~/.platformio/esphome_download_cache)",
    )

    subparsers = parser.add_subparsers(dest="command", help="Command to execute")

    # Download command
    download_parser = subparsers.add_parser("download", help="Download and cache a URL")
    download_parser.add_argument("url", help="URL to download")
    download_parser.add_argument(
        "--force", action="store_true", help="Force re-download even if cached"
    )
    download_parser.add_argument(
        "--no-check-updates",
        action="store_true",
        help="Skip checking for updates (don't use HTTP 304)",
    )

    # List command
    subparsers.add_parser("list", help="List cached files")

    # Stats command
    subparsers.add_parser("stats", help="Show cache statistics")

    # Clear command
    subparsers.add_parser("clear", help="Clear all cached files")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return 1

    # Use PlatformIO cache directory by default
    if args.cache_dir is None:
        args.cache_dir = Path.home() / ".platformio" / "esphome_download_cache"

    cache = GitHubCache(args.cache_dir)

    if args.command == "download":
        try:
            check_updates = not args.no_check_updates
            cache_path = download_with_progress(
                cache, args.url, force=args.force, check_updates=check_updates
            )
            print(f"\nCached at: {cache_path}")
            return 0
        except Exception as e:
            print(f"Error: {e}", file=sys.stderr)
            return 1

    elif args.command == "list":
        cached = cache.list_cached()
        if not cached:
            print("No cached files")
            return 0

        print(f"Cached files ({len(cached)}):")
        for item in cached:
            size_mb = item["size"] / (1024 * 1024)
            print(f"  {item['url']}")
            print(f"    Size: {size_mb:.2f} MB")
            print(f"    Path: {item['path']}")
        return 0

    elif args.command == "stats":
        total_size = cache.cache_size()
        cached_count = len(cache.list_cached())
        size_mb = total_size / (1024 * 1024)

        print(f"Cache directory: {cache.cache_dir}")
        print(f"Cached files: {cached_count}")
        print(f"Total size: {size_mb:.2f} MB")
        return 0

    elif args.command == "clear":
        cache.clear_cache()
        return 0

    return 1


if __name__ == "__main__":
    sys.exit(main())
