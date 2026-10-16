"""The start menu: build type.

A convenience over the flags, never a separate code path — the answer is the
same value the command line accepts.
"""

from __future__ import annotations

from collections.abc import Callable

from .errors import UsageError

Reader = Callable[[str], str]

BUILD_TYPE_OPTIONS: tuple[tuple[str, str], ...] = (
    ("Debug", "asserts and symbols, tests included (the dev loop)"),
    ("Release", "optimised, no tests"),
)


def choose(
    prompt: str,
    options: tuple[tuple[str, str], ...],
    *,
    default: str,
    reader: Reader = input,
) -> str:
    """Print a numbered menu and return the chosen key."""
    print(f"\n{prompt}")
    for index, (key, description) in enumerate(options, start=1):
        marker = " (default)" if key == default else ""
        print(f"  {index}) {key} - {description}{marker}")
    while True:
        try:
            answer = reader(f"Choice [1-{len(options)}]: ").strip()
        except EOFError as exc:
            raise UsageError("no input available: pass --debug or --release") from exc
        if not answer:
            return default
        if answer.isdigit() and 1 <= int(answer) <= len(options):
            return options[int(answer) - 1][0]
        for key, _ in options:
            if answer.lower() == key.lower():
                return key
        print(f"Please enter 1-{len(options)}.")


def choose_build_type(*, reader: Reader = input) -> str:
    """Return ``Debug`` or ``Release``."""
    return choose("Select build type:", BUILD_TYPE_OPTIONS, default="Debug", reader=reader)
