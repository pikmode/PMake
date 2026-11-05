import sys, pathlib
p = pathlib.Path(sys.argv[1])
p.parent.mkdir(parents=True, exist_ok=True)
p.touch()
p.write_text("constant\n", encoding="utf-8")
