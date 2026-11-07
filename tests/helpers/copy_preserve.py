import sys, pathlib, os
src = pathlib.Path(sys.argv[1]); dst = pathlib.Path(sys.argv[2])
dst.parent.mkdir(parents=True, exist_ok=True)
content = src.read_bytes()
if dst.exists():
    st = dst.stat(); old = dst.read_bytes()
    if old == content:
        dst.write_bytes(content)
        os.utime(dst, ns=(st.st_atime_ns, st.st_mtime_ns)); sys.exit(0)
    dst.write_bytes(content)
else:
    dst.write_bytes(content)
