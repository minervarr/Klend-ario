"""Klend-ario's build tool: a stdlib-only Python zipapp around cmake + ninja.

Linux-only for now (essentialAgent/MEMORY.md, Phase 0). The runtime imports
nothing outside the standard library, which is why it can be shipped as a
single ``.pyz`` and run by a stock interpreter.
"""

from __future__ import annotations

__version__ = "0.1.0"
