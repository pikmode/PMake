import sys, pathlib
src, out = sys.argv[1], sys.argv[2]
hdr = sys.argv[3] if len(sys.argv) > 3 else ""
pathlib.Path(out).parent.mkdir(parents=True, exist_ok=True)
pathlib.Path(out).touch()
if hdr:
    pathlib.Path(out + ".d").write_text(f"{out}: {src} {hdr}\n", encoding="utf-8")
