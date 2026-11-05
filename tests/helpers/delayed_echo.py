import sys, pathlib, time
time.sleep(int(sys.argv[1]) / 1000.0)
p = pathlib.Path(sys.argv[2])
p.parent.mkdir(parents=True, exist_ok=True)
p.write_text(sys.argv[3] + "\n", encoding="utf-8")
