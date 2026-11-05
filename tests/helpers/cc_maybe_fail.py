import sys, pathlib, time
src = pathlib.Path(sys.argv[1])
out = sys.argv[2]
if src.exists() and "FAIL" in src.read_text(encoding="utf-8"):
    time.sleep(0.2)
    print(f"intentional failure for {out}", file=sys.stderr)
    sys.exit(1)
pathlib.Path(out).parent.mkdir(parents=True, exist_ok=True)
pathlib.Path(out).touch()
pathlib.Path(out + ".d").write_text(f"{out}: {src.name} common.h\n", encoding="utf-8")
