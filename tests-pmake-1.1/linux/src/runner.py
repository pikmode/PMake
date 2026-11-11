# -*- coding: utf-8 -*-
"""runner.py — прогоняет tests/, пишет results/."""
from __future__ import annotations
import argparse
import fnmatch
import json
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path


def find_pmake(hint):
    if hint:
        p = Path(hint)
        if p.exists(): return p.absolute()
        w = shutil.which(hint)
        if w: return Path(w).absolute()
        raise SystemExit(f"pmake not found: {hint}")
    w = shutil.which("pmake") or shutil.which("pmake.exe")
    if w: return Path(w).absolute()
    raise SystemExit("pmake.exe not found. Use --pmake PATH")


def run_pmake(exe, args, cwd, timeout, kill_after_ms=None):
    t0 = time.time()
    if kill_after_ms is not None:
        try:
            p = subprocess.Popen(
                [str(exe)] + list(args), cwd=str(cwd),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            time.sleep(kill_after_ms / 1000.0)
            if p.poll() is None: p.kill()
            try:
                out_b, err_b = p.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                p.kill(); out_b, err_b = p.communicate()
            return {"exit": p.returncode,
                    "stdout": (out_b or b"").decode("utf-8", "replace"),
                    "stderr": (err_b or b"").decode("utf-8", "replace"),
                    "elapsed": time.time() - t0, "timedOut": False, "killed": True}
        except Exception as e:
            return {"exit": -998, "stdout": "",
                    "stderr": f"[runner/kill] {type(e).__name__}: {e}",
                    "elapsed": time.time() - t0, "timedOut": False}
    try:
        p = subprocess.run(
            [str(exe)] + list(args), cwd=str(cwd), timeout=timeout,
            capture_output=True, text=True, encoding="utf-8", errors="replace",
        )
        return {"exit": p.returncode, "stdout": p.stdout or "",
                "stderr": p.stderr or "", "elapsed": time.time() - t0,
                "timedOut": False}
    except subprocess.TimeoutExpired as e:
        def dec(x):
            if x is None: return ""
            if isinstance(x, bytes): return x.decode("utf-8", "replace")
            return x
        return {"exit": -999, "stdout": dec(e.stdout),
                "stderr": dec(e.stderr) + f"\n[TIMEOUT {timeout}s]",
                "elapsed": time.time() - t0, "timedOut": True}
    except Exception as e:
        return {"exit": -998, "stdout": "",
                "stderr": f"[runner] {type(e).__name__}: {e}",
                "elapsed": time.time() - t0, "timedOut": False}


def apply_actions(actions, cwd):
    for a in actions:
        if "sleep" in a: time.sleep(a["sleep"])
        elif "touch" in a:
            for path in a["touch"]:
                p = cwd / path
                p.parent.mkdir(parents=True, exist_ok=True)
                p.touch()
        elif "delete" in a:
            for path in a["delete"]:
                p = cwd / path
                if p.exists(): p.unlink()
        elif "modify" in a:
            for path, content in a["modify"].items():
                p = cwd / path
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(content.encode("utf-8"))


def parse_task_count(stdout):
    seen = set()
    for line in stdout.splitlines():
        s = line.strip()
        if not s.startswith("["): continue
        end = s.find("]")
        if end < 2: continue
        inner = s[1:end]
        if "/" not in inner: continue
        parts = inner.split("/")
        if len(parts) == 2 and parts[0].strip().isdigit() and parts[1].strip().isdigit():
            seen.add((int(parts[0]), int(parts[1])))
    return len(seen)


def check_expect(expect, result, cwd):
    fails = []
    if "exit" in expect and result["exit"] != expect["exit"]:
        fails.append(f"exit={result['exit']}, expected {expect['exit']}")
    if result.get("timedOut"):
        fails.append("TIMEOUT"); return fails
    tc = parse_task_count(result["stdout"])
    if "taskCount" in expect and tc != expect["taskCount"]:
        fails.append(f"taskCount={tc}, expected {expect['taskCount']}")
    if "taskCountMin" in expect and tc < expect["taskCountMin"]:
        fails.append(f"taskCount={tc} < min {expect['taskCountMin']}")
    if "taskCountMax" in expect and tc > expect["taskCountMax"]:
        fails.append(f"taskCount={tc} > max {expect['taskCountMax']}")
    if "elapsedMin" in expect and result["elapsed"] < expect["elapsedMin"]:
        fails.append(f"elapsed={result['elapsed']:.2f}s < min {expect['elapsedMin']}")
    if "elapsedMax" in expect and result["elapsed"] > expect["elapsedMax"]:
        fails.append(f"elapsed={result['elapsed']:.2f}s > max {expect['elapsedMax']}")
    for s in expect.get("stdoutHas", []):
        if s not in result["stdout"]: fails.append(f"stdout missing: {s!r}")
    for s in expect.get("stderrHas", []):
        if s not in result["stderr"]: fails.append(f"stderr missing: {s!r}")
    for pat in expect.get("stdoutMatches", []):
        if not re.search(pat, result["stdout"]):
            fails.append(f"stdout no match: {pat!r}")
    for pat in expect.get("stderrMatches", []):
        if not re.search(pat, result["stderr"]):
            fails.append(f"stderr no match: {pat!r}")
    for f in expect.get("files", []):
        if not (cwd / f).exists(): fails.append(f"missing file: {f}")
    for f in expect.get("filesNot", []):
        if (cwd / f).exists(): fails.append(f"unexpected file: {f}")
    for f, content in expect.get("filesWith", {}).items():
        p = cwd / f
        if not p.exists():
            fails.append(f"missing file: {f}"); continue
        try:
            actual = p.read_text(encoding="utf-8", errors="replace").strip()
            if actual != content.strip():
                fails.append(f"file {f}: expected {content!r}, got {actual[:200]!r}")
        except Exception as e:
            fails.append(f"file {f}: read error: {e}")
    return fails


def run_test(pmake, test_dir, helpers_dir, results_dir, keep_temp):
    meta = json.loads((test_dir / "meta.json").read_text(encoding="utf-8"))
    name = test_dir.name
    temp_root = Path(tempfile.mkdtemp(prefix=f"pmake_{name}_"))
    work = temp_root / "work"
    work.mkdir()
    for f in test_dir.iterdir():
        if f.name == "meta.json": continue
        if f.is_file(): shutil.copy2(f, work / f.name)
        elif f.is_dir(): shutil.copytree(f, work / f.name)
    for h in helpers_dir.iterdir():
        if h.is_file(): shutil.copy2(h, work / h.name)

    timeout = int(meta.get("timeoutSec", 60))
    default_args = meta.get("args", ["-f", "build.pmake"])
    logs, all_fails, total_elapsed = [], [], 0.0

    for run in meta.get("runs", []):
        run_name = run.get("name", "run")
        try: apply_actions(run.get("before", []), work)
        except Exception as e:
            all_fails.append(f"[{run_name}] before: {type(e).__name__}: {e}")
            break
        args = run.get("args", default_args)
        kill_after_ms = run.get("kill_after_ms")
        result = run_pmake(pmake, args, work, timeout, kill_after_ms=kill_after_ms)
        total_elapsed += result["elapsed"]
        logs.append(f"=== {run_name} ===\nargs: {args}\nexit: {result['exit']}\n"
                    f"killed: {result.get('killed', False)}\n"
                    f"elapsed: {result['elapsed']:.2f}s\n\n"
                    f"--- STDOUT ---\n{result['stdout']}\n"
                    f"--- STDERR ---\n{result['stderr']}\n")
        fails = check_expect(run.get("expect", {}), result, work)
        if fails:
            all_fails.extend([f"[{run_name}] {x}" for x in fails])
            if not run.get("continue", False): break

    results_dir.mkdir(parents=True, exist_ok=True)
    log_path = results_dir / f"{name}.log"
    header = f"=== TEST: {name} ===\n"
    if meta.get("description"): header += f"Description: {meta['description']}\n"
    if meta.get("bug"): header += f"Known bug: {meta['bug']}\n"
    log_path.write_text(header + "\n" + "\n".join(logs), encoding="utf-8")
    if not keep_temp: shutil.rmtree(temp_root, ignore_errors=True)
    return {"name": name, "status": "PASS" if not all_fails else "FAIL",
            "failures": all_fails, "elapsed": total_elapsed,
            "meta": meta, "log": str(log_path)}


def main(root):
    ap = argparse.ArgumentParser()
    ap.add_argument("--pmake")
    ap.add_argument("--filter", default="*")
    ap.add_argument("--keep-temp", action="store_true")
    args, _ = ap.parse_known_args()

    pmake = find_pmake(args.pmake)
    tests_dir = root / "tests"
    helpers_dir = root / "helpers"
    results_dir = root / "results"
    results_dir.mkdir(parents=True, exist_ok=True)
    for f in results_dir.glob("*.log"): f.unlink()
    for f in results_dir.glob("*.json"): f.unlink()

    test_dirs = sorted(d for d in tests_dir.iterdir()
                       if d.is_dir() and (d / "meta.json").exists())
    if args.filter != "*":
        test_dirs = [d for d in test_dirs if fnmatch.fnmatch(d.name, args.filter)]

    print("=" * 72)
    print(f" PMAKE TEST SUITE ({len(test_dirs)} tests)")
    print(f" pmake: {pmake}")
    print("=" * 72); print()

    results = []
    for td in test_dirs:
        r = run_test(pmake, td, helpers_dir, results_dir, args.keep_temp)
        results.append(r)
        if r["status"] == "PASS":
            print(f"[PASS] {r['name']:<32} ({r['elapsed']:>6.2f}s)")
        elif r["meta"].get("bug"):
            print(f"[BUG ] {r['name']:<32} ({r['elapsed']:>6.2f}s)  KNOWN")
            print(f"       bug: {r['meta']['bug']}")
            for f in r["failures"]: print(f"       - {f}")
        else:
            print(f"[FAIL] {r['name']:<32} ({r['elapsed']:>6.2f}s)")
            for f in r["failures"]: print(f"       {f}")
            print(f"       log: {r['log']}")

    passed = sum(1 for r in results if r["status"] == "PASS")
    known = sum(1 for r in results if r["status"] == "FAIL" and r["meta"].get("bug"))
    regr = sum(1 for r in results if r["status"] == "FAIL" and not r["meta"].get("bug"))
    print(); print("=" * 72)
    print(f" PASS: {passed}")
    print(f" KNOWN BUGS: {known}")
    print(f" REGRESSIONS: {regr}")
    print("=" * 72)
    report = {"timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
              "pmake": str(pmake), "total": len(results),
              "passed": passed, "known_bugs": known, "regressions": regr,
              "results": [{"name": r["name"], "status": r["status"],
                           "elapsed": r["elapsed"], "failures": r["failures"],
                           "description": r["meta"].get("description", ""),
                           "bug": r["meta"].get("bug", ""), "log": r["log"]}
                          for r in results]}
    (results_dir / "report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\n Report: {results_dir / 'report.json'}")
    return 0 if regr == 0 and known == 0 else 1
