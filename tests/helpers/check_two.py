import sys, pathlib
s1 = pathlib.Path(sys.argv[1])
s2 = pathlib.Path(sys.argv[2])
if not s1.exists():
    print(f"ORDER VIOLATION: {s1} not created yet", file=sys.stderr)
    sys.exit(1)
if not s2.exists():
    print(f"ORDER VIOLATION: {s2} not created yet", file=sys.stderr)
    sys.exit(1)
dst = pathlib.Path(sys.argv[3])
dst.write_text(s1.read_text(encoding="utf-8") + s2.read_text(encoding="utf-8"), encoding="utf-8")
