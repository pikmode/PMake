import sys, pathlib
src = pathlib.Path(sys.argv[1]); dst = pathlib.Path(sys.argv[2])
dst.parent.mkdir(parents=True, exist_ok=True)
if not src.exists():
    sys.exit(f"rspfile not found: {src}")
dst.write_text(src.read_text(encoding="utf-8"), encoding="utf-8")
