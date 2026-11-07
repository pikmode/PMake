# -*- coding: utf-8 -*-
"""helpers.py — Python-хелперы для build.pmake правил."""

HELPERS = {
    "touch.py": """
import sys, pathlib
for arg in sys.argv[1:]:
    p = pathlib.Path(arg)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.touch()
""",
    "echo_to.py": """
import sys, pathlib
p = pathlib.Path(sys.argv[1])
p.parent.mkdir(parents=True, exist_ok=True)
p.write_text(sys.argv[2] + "\\n", encoding="utf-8")
""",
    "copy.py": """
import sys, shutil, pathlib
pathlib.Path(sys.argv[2]).parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(sys.argv[1], sys.argv[2])
""",
    "copy_preserve.py": """
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
""",
    "copy_preserve_multi.py": """
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
""",
    "cc_make_dep.py": """
import sys, pathlib
src, out = sys.argv[1], sys.argv[2]
hdr = sys.argv[3] if len(sys.argv) > 3 else ""
pathlib.Path(out).parent.mkdir(parents=True, exist_ok=True)
pathlib.Path(out).touch()
if hdr:
    pathlib.Path(out + ".d").write_text(f"{out}: {src} {hdr}\\n", encoding="utf-8")
""",
    "cc_maybe_fail.py": """
import sys, pathlib, time
src = pathlib.Path(sys.argv[1]); out = sys.argv[2]
if src.exists() and "FAIL" in src.read_text(encoding="utf-8"):
    time.sleep(0.2)
    print(f"intentional failure for {out}", file=sys.stderr); sys.exit(1)
pathlib.Path(out).parent.mkdir(parents=True, exist_ok=True)
pathlib.Path(out).touch()
pathlib.Path(out + ".d").write_text(f"{out}: {src.name} common.h\\n", encoding="utf-8")
""",
    "concat.py": """
import sys, pathlib
out = pathlib.Path(sys.argv[1])
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("".join(pathlib.Path(p).read_text(encoding="utf-8") for p in sys.argv[2:]), encoding="utf-8")
""",
    "multi_out.py": """
import sys, pathlib
for out in sys.argv[1:]:
    p = pathlib.Path(out)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(f"from_multi_out_{p.name}\\n", encoding="utf-8")
""",
    "fail.py": """
import sys
print("intentional failure", file=sys.stderr)
sys.exit(1)
""",
    "sleep.py": """
import sys, time
time.sleep(int(sys.argv[1]) / 1000.0)
""",
    "sleep_and_touch.py": """
import sys, time, pathlib
time.sleep(int(sys.argv[1]) / 1000.0)
p = pathlib.Path(sys.argv[2])
p.parent.mkdir(parents=True, exist_ok=True)
p.touch()
""",
    "check_stamp.py": """
import sys, pathlib
stamp = pathlib.Path(sys.argv[1])
if not stamp.exists():
    print(f"ORDER VIOLATION: {stamp} not created yet", file=sys.stderr); sys.exit(1)
src = pathlib.Path(sys.argv[2]); dst = pathlib.Path(sys.argv[3])
dst.write_text(src.read_text(encoding="utf-8"), encoding="utf-8")
""",
    "check_two.py": """
import sys, pathlib
s1 = pathlib.Path(sys.argv[1]); s2 = pathlib.Path(sys.argv[2])
if not s1.exists():
    print(f"ORDER VIOLATION: {s1} not created yet", file=sys.stderr); sys.exit(1)
if not s2.exists():
    print(f"ORDER VIOLATION: {s2} not created yet", file=sys.stderr); sys.exit(1)
dst = pathlib.Path(sys.argv[3])
dst.write_text(s1.read_text(encoding="utf-8") + s2.read_text(encoding="utf-8"), encoding="utf-8")
""",
    "write_all.py": """
import sys, pathlib
p = pathlib.Path(sys.argv[1])
p.parent.mkdir(parents=True, exist_ok=True)
p.write_text(" ".join(sys.argv[2:]) + "\\n", encoding="utf-8")
""",
    "make_dep.py": """
import sys, pathlib
out = sys.argv[1]
pathlib.Path(out).parent.mkdir(parents=True, exist_ok=True)
pathlib.Path(out).touch()
header = sys.argv[2] if len(sys.argv) > 2 else ""
if header:
    pathlib.Path(out + ".d").write_text(f"{out}: {header}\\n", encoding="utf-8")
""",
    "restat.py": """
import sys, pathlib
p = pathlib.Path(sys.argv[1])
p.parent.mkdir(parents=True, exist_ok=True)
p.touch()
p.write_text("constant\\n", encoding="utf-8")
""",
    "cat_rspfile.py": """
import sys, pathlib
src = pathlib.Path(sys.argv[1]); dst = pathlib.Path(sys.argv[2])
dst.parent.mkdir(parents=True, exist_ok=True)
if not src.exists():
    sys.exit(f"rspfile not found: {src}")
dst.write_text(src.read_text(encoding="utf-8"), encoding="utf-8")
""",
    "delayed_echo.py": """
import sys, pathlib, time
time.sleep(int(sys.argv[1]) / 1000.0)
p = pathlib.Path(sys.argv[2])
p.parent.mkdir(parents=True, exist_ok=True)
p.write_text(sys.argv[3] + "\\n", encoding="utf-8")
""",
}
