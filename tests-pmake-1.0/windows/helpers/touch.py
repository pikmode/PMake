import sys, pathlib
for arg in sys.argv[1:]:
    p = pathlib.Path(arg)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.touch()
