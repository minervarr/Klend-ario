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


def test_gui_flag_adds_the_cmake_option(capsys: pytest.CaptureFixture[str]) -> None:
    # --gui wins without probing the machine, so this works on any toolchain.
    assert cli.main(["build", "--debug", "--dry-run", "--gui"]) == 0
    out = capsys.readouterr().out
    assert "-DKLEND_BUILD_GUI=ON" in out
    assert "gui:" in out


def test_no_gui_flag_leaves_the_option_out(capsys: pytest.CaptureFixture[str]) -> None:
    assert cli.main(["build", "--debug", "--dry-run", "--no-gui"]) == 0
    out = capsys.readouterr().out
    assert "KLEND_BUILD_GUI" not in out


def test_gui_and_no_gui_are_mutually_exclusive() -> None:
    with pytest.raises(SystemExit) as excinfo:
        cli.main(["build", "--gui", "--no-gui"])
    assert excinfo.value.code == 2


def test_confirm_defaults_and_answers() -> None:
    from klend_ario_build import menu

    assert menu.confirm("gui?", default=True, reader=lambda _: "") is True
    assert menu.confirm("gui?", default=False, reader=lambda _: "") is False
    assert menu.confirm("gui?", default=True, reader=lambda _: "n") is False
    assert menu.confirm("gui?", default=False, reader=lambda _: "yes") is True
    assert menu.confirm("gui?", default=False, reader=lambda _: "N") is False
