import json
import sys

try:
    with open("results/report.json") as f:
        r = json.load(f)
except FileNotFoundError:
    print("ERROR: report.json not found. Tests likely failed to run.")
    sys.exit(1)

print(f"PASS: {r['passed']}/{r['total']}")
print(f"REGRESSIONS: {r['regressions']}")
print(f"KNOWN BUGS: {r['known_bugs']}")

if r['regressions'] > 0:
    print("FAILED: Regressions found.")
    sys.exit(1)

print("SUCCESS: All tests passed.")
sys.exit(0)