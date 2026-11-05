import sys, shutil, pathlib
if len(sys.argv) != 3:
    sys.exit("usage: copy.py SRC DST")
pathlib.Path(sys.argv[2]).parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(sys.argv[1], sys.argv[2])
