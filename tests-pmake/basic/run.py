#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""run.py — entry point.

Без флагов: генерирует тестовый проект в CWD + прогоняет тесты.
--generate-only: только генерирует (для build.pmake).
--pmake PATH, --filter PATTERN, --keep-temp.

ВСЕ пути относительно CWD (место вызова). Модули src/ — относительно __file__.
"""
from __future__ import annotations
import sys as _sys
try:
    _sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    _sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from src.generate import generate_project
from src import runner


def main() -> int:
    generate_only = "--generate-only" in sys.argv
    root = Path.cwd()
    generate_project(root)
    if generate_only:
        print(f"[OK] test project generated in {root}")
        return 0
    return runner.main(root)


if __name__ == "__main__":
    sys.exit(main())
