import sys, pathlib, os
src = pathlib.Path(sys.argv[1]); content = src.read_bytes()
for out_s in sys.argv[2:]:
    out = pathlib.Path(out_s)
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        st = out.stat(); old = out.read_bytes()
        if old == content:
            out.write_bytes(content)
            os.utime(out, ns=(st.st_atime_ns, st.st_mtime_ns)); continue
    out.write_bytes(content)
