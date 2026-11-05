import sys, pathlib
stamp = pathlib.Path(sys.argv[1])
if not stamp.exists():
    print(f"ORDER VIOLATION: {stamp} not created yet", file=sys.stderr)
    sys.exit(1)
src = pathlib.Path(sys.argv[2])
dst = pathlib.Path(sys.argv[3])
dst.write_text(src.read_text(encoding="utf-8"), encoding="utf-8")
