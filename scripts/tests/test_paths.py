"""Tests for the build tool's pure parts (paths), which need no toolchain.

Run:  uv run --directory scripts pytest -q
"""

from __future__ import annotations

from pathlib import Path

from klend_ario_build import paths


def test_build_dir_naming() -> None:
    root = Path("/repo")
    assert paths.build_dir(root, debug=True) == root / "build"
    assert paths.build_dir(root, debug=False) == root / "build_release"


def test_binary_path_is_under_cli() -> None:
    assert paths.binary_path(Path("/repo/build")) == Path("/repo/build/cli/klend_ario_cli")


def test_find_repo_root_from_this_file() -> None:
    # This file lives at <root>/scripts/tests/, so walking up must land on the
    # directory holding CMakeLists.txt + manifest.json.
    root = paths.find_repo_root(Path(__file__))
    assert (root / "CMakeLists.txt").is_file()
    assert (root / "manifest.json").is_file()


def test_version_comes_from_manifest() -> None:
    root = paths.find_repo_root(Path(__file__))
    assert paths.version(root) == paths.load_manifest(root)["version"]


def test_display_shortens_paths_under_root() -> None:
    root = Path("/repo")
    assert paths.display(root, Path("/repo/build/cli")) == "build/cli"
    assert paths.display(root, Path("/elsewhere/x")) == "/elsewhere/x"
