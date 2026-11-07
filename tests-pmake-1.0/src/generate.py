# -*- coding: utf-8 -*-
"""generate.py — собирает все TESTS и пишет тестовый проект в CWD."""
from __future__ import annotations
import json
import shutil
import time
from pathlib import Path

from .helpers import HELPERS
from .tests_basic import TESTS as T_BASIC
from .tests_incremental import TESTS as T_INCR
from .tests_restat import TESTS as T_RESTAT
from .tests_order_only import TESTS as T_ORDER
from .tests_phony import TESTS as T_PHONY
from .tests_depfile import TESTS as T_DEPFILE
from .tests_command_hash import TESTS as T_HASH
from .tests_include import TESTS as T_INCLUDE
from .tests_errors import TESTS as T_ERRORS
from .tests_parallel import TESTS as T_PARALLEL
from .tests_paths import TESTS as T_PATHS
from .tests_modules import TESTS as T_MODULES
from .tests_builtins import TESTS as T_BUILTINS
from .tests_pool import TESTS as T_POOL
from .tests_rspfile import TESTS as T_RSPFILE
from .tests_generator import TESTS as T_GEN
from .tests_cmake import TESTS as T_CMAKE
from .tests_misc import TESTS as T_MISC
from .tests_hard import TESTS as T_HARD

ALL_TESTS = (
    T_BASIC + T_INCR + T_RESTAT + T_ORDER + T_PHONY + T_DEPFILE +
    T_HASH + T_INCLUDE + T_ERRORS + T_PARALLEL + T_PATHS +
    T_MODULES + T_BUILTINS + T_POOL + T_RSPFILE + T_GEN +
    T_CMAKE + T_MISC + T_HARD
)


def generate_project(root: Path) -> None:
    tests_dir = root / "tests"
    helpers_dir = root / "helpers"
    results_dir = root / "results"

    if tests_dir.exists():
        shutil.rmtree(tests_dir)
    tests_dir.mkdir(parents=True)
    helpers_dir.mkdir(parents=True, exist_ok=True)
    results_dir.mkdir(parents=True, exist_ok=True)

    for name, content in HELPERS.items():
        (helpers_dir / name).write_text(content.lstrip(), encoding="utf-8")

    for t in ALL_TESTS:
        td = tests_dir / t["name"]
        td.mkdir(parents=True, exist_ok=True)
        for rel, content in t["files"].items():
            f = td / rel
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_text(content, encoding="utf-8")
        (td / "meta.json").write_text(
            json.dumps(t["meta"], indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )

    (root / ".generated").write_text(
        f"generated at {time.strftime('%Y-%m-%dT%H:%M:%S')}\n"
        f"tests: {len(ALL_TESTS)}\n"
        f"helpers: {len(HELPERS)}\n",
        encoding="utf-8",
    )
