"""Tests for the command line: it must not touch a toolchain to parse.

Run:  uv run --directory scripts pytest -q
"""

from __future__ import annotations

import pytest

from klend_ario_build import cli


def test_version_prints_and_exits_zero(capsys: pytest.CaptureFixture[str]) -> None:
    assert cli.main(["--version"]) == 0
    assert "klend-ario-build" in capsys.readouterr().out


def test_unknown_command_is_a_usage_error(capsys: pytest.CaptureFixture[str]) -> None:
    # argparse itself exits 2 for a command outside the choices.
    with pytest.raises(SystemExit) as excinfo:
        cli.main(["frobnicate"])
    assert excinfo.value.code == 2


def test_dry_run_build_runs_nothing(capsys: pytest.CaptureFixture[str]) -> None:
    # --dry-run needs no toolchain and must still print the cmake lines.
    assert cli.main(["build", "--debug", "--dry-run"]) == 0
    out = capsys.readouterr().out
    assert "cmake" in out
    assert "-DCMAKE_BUILD_TYPE=Debug" in out


def test_extra_cmake_argument_must_be_d() -> None:
    with pytest.raises(SystemExit) as excinfo:
        cli.main(["build", "--dry-run", "notadefine"])
    assert excinfo.value.code == 2
