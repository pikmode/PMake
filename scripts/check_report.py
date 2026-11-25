import json
import sys
from pathlib import Path

try:
    # Явно указываем кодировку UTF-8, чтобы Windows не ругался на русские буквы
    project_root = Path(__file__).resolve().parents[1]
    report_path = project_root / "tests-pmake" / "basic" / "results" / "report.json"
    with report_path.open(encoding="utf-8") as f:
        r = json.load(f)
except FileNotFoundError:
    print("ERROR: report.json not found. Tests likely failed to run.")
    sys.exit(1)
except (OSError, json.JSONDecodeError) as e:
    print(f"ERROR reading report.json: {e}")
    sys.exit(1)

try:
    passed = int(r["passed"])
    total = int(r["total"])
    regressions = int(r["regressions"])
    known_bugs = int(r["known_bugs"])
except (KeyError, TypeError, ValueError) as e:
    print(f"ERROR: invalid report format: {e}")
    sys.exit(1)

print(f"PASS: {passed}/{total}")
print(f"REGRESSIONS: {regressions}")
print(f"KNOWN BUGS: {known_bugs}")

if passed != total or regressions > 0 or known_bugs > 0:
    print("FAILED: Test failures found.")
    sys.exit(1)

print("SUCCESS: All tests passed.")
sys.exit(0)
