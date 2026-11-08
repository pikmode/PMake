import json
import sys

try:
    # Явно указываем кодировку UTF-8, чтобы Windows не ругался на русские буквы
    with open("results/report.json", encoding="utf-8") as f:
        r = json.load(f)
except FileNotFoundError:
    print("ERROR: report.json not found. Tests likely failed to run.")
    sys.exit(1)
except Exception as e:
    print(f"ERROR reading report.json: {e}")
    sys.exit(1)

print(f"PASS: {r['passed']}/{r['total']}")
print(f"REGRESSIONS: {r['regressions']}")
print(f"KNOWN BUGS: {r['known_bugs']}")

if r['regressions'] > 0:
    print("FAILED: Regressions found.")
    sys.exit(1)

print("SUCCESS: All tests passed.")
sys.exit(0)