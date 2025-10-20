#!/usr/bin/env python3
"""
PlatformIO Download Wrapper with Caching

This script can be used as a wrapper around PlatformIO downloads to add caching.
It intercepts download operations and uses the GitHub download cache.

This is designed to be called from PlatformIO's extra_scripts if needed.
"""

from pathlib import Path
import sys

# Import the cache manager
sys.path.insert(0, str(Path(__file__).parent))
from github_download_cache import GitHubDownloadCache


def is_github_url(url: str) -> bool:
    """Check if a URL is a GitHub URL."""
    return "github.com" in url.lower()


def cached_download_handler(source, target, env):
    """PlatformIO download handler that uses caching for GitHub URLs.

    This function can be registered as a custom download handler in PlatformIO.

    Args:
        source: Source URL
        target: Target file path
        env: SCons environment
    """
    import shutil
    import urllib.request

    url = str(source[0])
    target_path = Path(str(target[0]))

    # Only cache GitHub URLs
    if not is_github_url(url):
        # Fall back to default download
        print(f"Downloading (no cache): {url}")
        with (
            urllib.request.urlopen(url) as response,
            open(target_path, "wb") as out_file,
        ):
            shutil.copyfileobj(response, out_file)
        return

    # Use cache for GitHub URLs
    cache = GitHubDownloadCache()
    print(f"Downloading with cache: {url}")

    try:
        cached_path = cache.download_with_cache(url, check_updates=True)

        # Copy from cache to target
        shutil.copy2(cached_path, target_path)
        print(f"  Copied to: {target_path}")

    except Exception as e:
        print(f"Cache download failed, using direct download: {e}")
        # Fall back to direct download
        with (
            urllib.request.urlopen(url) as response,
            open(target_path, "wb") as out_file,
        ):
            shutil.copyfileobj(response, out_file)


def setup_platformio_caching():
    """Setup PlatformIO to use cached downloads.

    This should be called from an extra_scripts file in platformio.ini.

    Example extra_scripts file (e.g., platformio_hooks.py):
        Import("env")
        from script.platformio_download_wrapper import setup_platformio_caching
        setup_platformio_caching()
    """
    try:
        from SCons.Script import DefaultEnvironment

        DefaultEnvironment()

        # Register custom download handler
        # Note: This may not work with all PlatformIO versions
        # as the download mechanism is internal
        print("Note: Direct download interception is not fully supported.")
        print("Please use the cache_platformio_downloads.py script instead.")

    except ImportError:
        print("Warning: SCons not available, cannot setup download caching")


if __name__ == "__main__":
    # CLI mode - can be used to manually download a URL with caching
    import argparse

    parser = argparse.ArgumentParser(description="Download a URL with caching")
    parser.add_argument("url", help="URL to download")
    parser.add_argument("target", help="Target file path")
    parser.add_argument("--cache-dir", type=Path, help="Cache directory")

    args = parser.parse_args()

    cache = GitHubDownloadCache(args.cache_dir)
    target_path = Path(args.target)

    try:
        if is_github_url(args.url):
            print(f"Downloading with cache: {args.url}")
            cached_path = cache.download_with_cache(args.url)

            # Copy to target
            import shutil

            target_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(cached_path, target_path)
            print(f"Copied to: {target_path}")
        else:
            print(f"Downloading directly (not a GitHub URL): {args.url}")
            import shutil
            import urllib.request

            target_path.parent.mkdir(parents=True, exist_ok=True)
            with (
                urllib.request.urlopen(args.url) as response,
                open(target_path, "wb") as out_file,
            ):
                shutil.copyfileobj(response, out_file)

        sys.exit(0)

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
