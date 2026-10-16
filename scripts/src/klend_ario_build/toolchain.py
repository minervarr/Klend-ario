"""Find the external tools: ``cmake``, ``ninja`` and a C++ compiler.

Ninja resolution order — explicit ``--ninja``, then ``$KLEND_ARIO_NINJA``, then
``PATH``. Missing tools are reported together, in one line.
"""

from __future__ import annotations

import os
import shutil
from collections.abc import Mapping
from dataclasses import dataclass

from .errors import BuildError
from .ui import note, warn

NINJA_ENV = "KLEND_ARIO_NINJA"
POSIX_COMPILERS = ("g++", "clang++")


@dataclass(frozen=True)
class Toolchain:
    """The resolved tools one build will use."""

    cmake: str
    ninja: str
    compiler: str | None


def _which(name: str, env: Mapping[str, str] | None) -> str | None:
    return shutil.which(name, path=(env or os.environ).get("PATH"))


def jobs(env: Mapping[str, str] | None = None) -> int:
    """Parallel compile jobs: the CPU count, or 1 when unknown."""
    _ = env
    return os.cpu_count() or 1


def find_ninja(explicit: str | None = None, env: Mapping[str, str] | None = None) -> str:
    """Resolve ninja, preferring an explicit path over the environment."""
    for candidate in (explicit, (env or os.environ).get(NINJA_ENV)):
        if not candidate:
            continue
        found = candidate if os.path.isfile(candidate) else _which(candidate, env)
        if not found:
            raise BuildError(f"ninja not found at {candidate!r}")
        return found
    found = _which("ninja", env)
    if found:
        return found
    raise BuildError(f"ninja not found: put it on PATH, set ${NINJA_ENV}, or pass --ninja <path>")


def discover(
    *,
    ninja: str | None = None,
    env: Mapping[str, str] | None = None,
    dry_run: bool = False,
) -> Toolchain:
    """Resolve every tool, or fail once with the complete list of what is missing.

    In *dry run* nothing is required: placeholders keep ``--dry-run`` usable on
    a machine that has no toolchain at all, which is the point of the flag.
    """
    missing: list[str] = []

    cmake = _which("cmake", env)
    if not cmake:
        missing.append("cmake")

    compiler = next((c for c in (_which(name, env) for name in POSIX_COMPILERS) if c), None)
    if not compiler:
        missing.append(" or ".join(POSIX_COMPILERS))

    resolved_ninja = ""
    try:
        resolved_ninja = find_ninja(ninja, env)
    except BuildError as exc:
        missing.append(str(exc))

    if missing:
        if not dry_run:
            raise BuildError("missing required tool(s): " + "; ".join(missing))
        warn("dry run with missing tool(s): " + "; ".join(missing))

    toolchain = Toolchain(cmake or "cmake", resolved_ninja or "ninja", compiler)
    if not missing:
        note(f"    cmake: {toolchain.cmake}")
        note(f"    ninja: {toolchain.ninja}")
        note(f"    c++:   {toolchain.compiler}")
    return toolchain
