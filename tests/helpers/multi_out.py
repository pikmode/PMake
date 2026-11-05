import sys, pathlib
for out in sys.argv[1:]:
    p = pathlib.Path(out)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(f"from_multi_out_{p.name}\n", encoding="utf-8")
