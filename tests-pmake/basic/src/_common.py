# -*- coding: utf-8 -*-
"""_common.py — общие утилиты."""
from __future__ import annotations
import platform

import sys
REAL_PYTHON = sys.executable.replace("\\", "/")

IS_WINDOWS = platform.system() == "Windows"
SHELL_PREFIX = "cmd /c" if IS_WINDOWS else "sh -c"


def pp(s: str) -> str:
    return s.replace("@PYTHON@", REAL_PYTHON)


def test(name, files, runs, description="", bug="", timeout=60, args=None):
    return {
        "name": name,
        "files": {k: pp(v) if isinstance(v, str) else v for k, v in files.items()},
        "meta": {
            "description": description,
            "bug": bug,
            "timeoutSec": timeout,
            "args": args or ["-f", "build.pmake"],
            "runs": runs,
        },
    }
