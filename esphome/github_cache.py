"""GitHub download cache for ESPHome.

This module provides caching functionality for GitHub release downloads
to avoid redundant network I/O when switching between platforms.
"""

from __future__ import annotations

import hashlib
import json
import logging
from pathlib import Path
import shutil
import time
import urllib.error
import urllib.request

_LOGGER = logging.getLogger(__name__)


class GitHubCache:
    """Manages caching of GitHub release downloads."""

    # Cache expiration time in seconds (30 days)
    CACHE_EXPIRATION_SECONDS = 30 * 24 * 60 * 60

    def __init__(self, cache_dir: Path | None = None):
        """Initialize the cache manager.

        Args:
            cache_dir: Directory to store cached files.
                      Defaults to ~/.esphome_cache/github
        """
        if cache_dir is None:
            cache_dir = Path.home() / ".esphome_cache" / "github"
        self.cache_dir = cache_dir
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        self.metadata_file = self.cache_dir / "cache_metadata.json"
        # Prune old files on initialization
        try:
            self._prune_old_files()
        except Exception as e:
            _LOGGER.debug("Failed to prune old cache files: %s", e)

    def _load_metadata(self) -> dict:
        """Load cache metadata from disk."""
        if self.metadata_file.exists():
            try:
                with open(self.metadata_file) as f:
                    return json.load(f)
            except (OSError, ValueError, json.JSONDecodeError):
                return {}
        return {}

    def _save_metadata(self, metadata: dict) -> None:
        """Save cache metadata to disk."""
        try:
            with open(self.metadata_file, "w") as f:
                json.dump(metadata, f, indent=2)
        except OSError as e:
            _LOGGER.debug("Failed to save cache metadata: %s", e)

    @staticmethod
    def is_github_url(url: str) -> bool:
        """Check if URL is a GitHub release download."""
        return "github.com" in url.lower() and url.endswith(".zip")

    def _get_cache_key(self, url: str) -> str:
        """Get cache key (hash) for a URL."""
        return hashlib.sha256(url.encode()).hexdigest()

    def _get_cache_path(self, url: str) -> Path:
        """Get cache file path for a URL."""
        cache_key = self._get_cache_key(url)
        ext = Path(url.split("?")[0]).suffix
        return self.cache_dir / f"{cache_key}{ext}"

    def _check_if_modified(
        self,
        url: str,
        last_modified: str | None = None,
        etag: str | None = None,
    ) -> bool:
        """Check if a URL has been modified using HTTP 304.

        Args:
            url: URL to check
            last_modified: Last-Modified header from previous response
            etag: ETag header from previous response

        Returns:
            True if modified (or unable to check), False if not modified
        """
        if not last_modified and not etag:
            # No cache headers available, assume modified
            return True

        try:
            request = urllib.request.Request(url)
            request.get_method = lambda: "HEAD"

            if last_modified:
                request.add_header("If-Modified-Since", last_modified)
            if etag:
                request.add_header("If-None-Match", etag)

            try:
                urllib.request.urlopen(request, timeout=10)
                # 200 OK = file was modified
                return True
            except urllib.error.HTTPError as e:
                if e.code == 304:
                    # Not modified
                    _LOGGER.debug("File not modified (HTTP 304): %s", url)
                    return False
                # Other errors, assume modified to be safe
                return True
        except (OSError, urllib.error.URLError) as e:
            # If check fails, assume not modified (use cache)
            _LOGGER.debug("Failed to check if modified: %s", e)
            return False

    def get_cached_path(self, url: str, check_updates: bool = True) -> Path | None:
        """Get path to cached file if available and valid.

        Args:
            url: URL to check
            check_updates: Whether to check for updates using HTTP 304

        Returns:
            Path to cached file if valid, None if needs download
        """
        if not self.is_github_url(url):
            return None

        cache_path = self._get_cache_path(url)
        if not cache_path.exists():
            return None

        # Load metadata
        metadata = self._load_metadata()
        cache_key = self._get_cache_key(url)

        # Check if file should be re-downloaded
        should_redownload = False
        if check_updates and cache_key in metadata:
            last_modified = metadata[cache_key].get("last_modified")
            etag = metadata[cache_key].get("etag")
            if self._check_if_modified(url, last_modified, etag):
                # File was modified, need to re-download
                _LOGGER.debug("Cached file is outdated: %s", url)
                should_redownload = True

        if should_redownload:
            return None

        # File is valid, update cached_at timestamp to keep it fresh
        if cache_key in metadata:
            metadata[cache_key]["cached_at"] = time.time()
            self._save_metadata(metadata)

        # Log appropriate message
        if not check_updates:
            _LOGGER.debug("Using cached file (no update check): %s", url)
        elif cache_key not in metadata:
            _LOGGER.debug("Using cached file (no metadata): %s", url)
        else:
            _LOGGER.debug("Using cached file: %s", url)

        return cache_path

    def save_to_cache(self, url: str, source_path: Path) -> None:
        """Save a downloaded file to cache.

        Args:
            url: URL the file was downloaded from
            source_path: Path to the downloaded file
        """
        if not self.is_github_url(url):
            return

        try:
            cache_path = self._get_cache_path(url)
            # Only copy if source and destination are different
            if source_path.resolve() != cache_path.resolve():
                shutil.copy2(source_path, cache_path)

            # Try to get HTTP headers for caching
            last_modified = None
            etag = None
            try:
                request = urllib.request.Request(url)
                request.get_method = lambda: "HEAD"
                response = urllib.request.urlopen(request, timeout=10)
                last_modified = response.headers.get("Last-Modified")
                etag = response.headers.get("ETag")
            except (OSError, urllib.error.URLError):
                pass

            # Update metadata
            metadata = self._load_metadata()
            cache_key = self._get_cache_key(url)

            metadata[cache_key] = {
                "url": url,
                "size": cache_path.stat().st_size,
                "cached_at": time.time(),
                "last_modified": last_modified,
                "etag": etag,
            }
            self._save_metadata(metadata)

            _LOGGER.debug("Saved to cache: %s", url)

        except OSError as e:
            _LOGGER.debug("Failed to save to cache: %s", e)

    def copy_from_cache(self, url: str, destination: Path) -> bool:
        """Copy a cached file to destination.

        Args:
            url: URL of the cached file
            destination: Where to copy the file

        Returns:
            True if successful, False otherwise
        """
        cached_path = self.get_cached_path(url, check_updates=True)
        if not cached_path:
            return False

        try:
            shutil.copy2(cached_path, destination)
            _LOGGER.info("Using cached download for %s", url)
            return True
        except OSError as e:
            _LOGGER.warning("Failed to use cache: %s", e)
            return False

    def cache_size(self) -> int:
        """Get total size of cached files in bytes."""
        total = 0
        try:
            for file_path in self.cache_dir.glob("*"):
                if file_path.is_file() and file_path != self.metadata_file:
                    total += file_path.stat().st_size
        except OSError:
            pass
        return total

    def list_cached(self) -> list[dict]:
        """List all cached files with metadata."""
        cached_files = []
        metadata = self._load_metadata()

        for cache_key, meta in metadata.items():
            cache_path = (
                self.cache_dir / f"{cache_key}{Path(meta['url'].split('?')[0]).suffix}"
            )
            if cache_path.exists():
                cached_files.append(
                    {
                        "url": meta["url"],
                        "path": cache_path,
                        "size": meta["size"],
                        "cached_at": meta.get("cached_at"),
                        "last_modified": meta.get("last_modified"),
                        "etag": meta.get("etag"),
                    }
                )

        return cached_files

    def clear_cache(self) -> None:
        """Clear all cached files."""
        try:
            for file_path in self.cache_dir.glob("*"):
                if file_path.is_file():
                    file_path.unlink()
            _LOGGER.info("Cache cleared: %s", self.cache_dir)
        except OSError as e:
            _LOGGER.warning("Failed to clear cache: %s", e)

    def _prune_old_files(self) -> None:
        """Remove cache files older than CACHE_EXPIRATION_SECONDS."""
        current_time = time.time()
        metadata = self._load_metadata()
        removed_count = 0
        removed_size = 0

        # Check each file in metadata
        for cache_key, meta in list(metadata.items()):
            cached_at = meta.get("cached_at", 0)
            age_seconds = current_time - cached_at

            if age_seconds > self.CACHE_EXPIRATION_SECONDS:
                # File is too old, remove it
                cache_path = (
                    self.cache_dir
                    / f"{cache_key}{Path(meta['url'].split('?')[0]).suffix}"
                )
                if cache_path.exists():
                    file_size = cache_path.stat().st_size
                    cache_path.unlink()
                    removed_size += file_size
                    removed_count += 1
                    _LOGGER.debug(
                        "Pruned old cache file (age: %.1f days): %s",
                        age_seconds / (24 * 60 * 60),
                        meta["url"],
                    )

                # Remove from metadata
                del metadata[cache_key]

        # Also check for orphaned files (files without metadata)
        for file_path in self.cache_dir.glob("*.zip"):
            if file_path == self.metadata_file:
                continue

            # Check if file is in metadata
            found_in_metadata = False
            for cache_key in metadata:
                if file_path.name.startswith(cache_key):
                    found_in_metadata = True
                    break

            if not found_in_metadata:
                # Orphaned file - check age by modification time
                file_age = current_time - file_path.stat().st_mtime
                if file_age > self.CACHE_EXPIRATION_SECONDS:
                    file_size = file_path.stat().st_size
                    file_path.unlink()
                    removed_size += file_size
                    removed_count += 1
                    _LOGGER.debug(
                        "Pruned orphaned cache file (age: %.1f days): %s",
                        file_age / (24 * 60 * 60),
                        file_path.name,
                    )

        # Save updated metadata if anything was removed
        if removed_count > 0:
            self._save_metadata(metadata)
            removed_mb = removed_size / (1024 * 1024)
            _LOGGER.info(
                "Pruned %d old cache file(s), freed %.2f MB",
                removed_count,
                removed_mb,
            )


# Global cache instance
_cache: GitHubCache | None = None


def get_cache() -> GitHubCache:
    """Get the global GitHub cache instance."""
    global _cache  # noqa: PLW0603
    if _cache is None:
        _cache = GitHubCache()
    return _cache
