import sys, pathlib
out = pathlib.Path(sys.argv[1])
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("".join(pathlib.Path(p).read_text(encoding="utf-8") for p in sys.argv[2:]), encoding="utf-8")
