import sys, pathlib
out = sys.argv[1]
pathlib.Path(out).parent.mkdir(parents=True, exist_ok=True)
pathlib.Path(out).touch()
header = sys.argv[2] if len(sys.argv) > 2 else ""
if header:
    pathlib.Path(out + ".d").write_text(f"{out}: {header}\n", encoding="utf-8")
