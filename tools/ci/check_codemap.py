#!/usr/bin/env python3
# CODEMAP.md가 git이 추적하는 소스 파일(.cpp/.h/.hpp/.ixx, src/ThirdParty 제외)을 빠짐없이 담는지 검사한다.
# 표의 파일 이름은 바로 위 "## 폴더/" 제목 기준 상대 경로다("## (루트)"는 저장소 루트).
#   python3 tools/ci/check_codemap.py [저장소 루트]
# 종료 코드: 0 일치, 1 빠진 파일 또는 없는 파일이 있음.
import pathlib
import re
import subprocess
import sys

SOURCE_SUFFIXES = (".cpp", ".h", ".hpp", ".ixx")
EXCLUDED_PREFIXES = ("src/ThirdParty/",)

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
tracked = subprocess.run(["git", "-C", str(root), "ls-files"], check=True, capture_output=True, text=True).stdout
sources = {
    path for path in tracked.splitlines()
    if path.endswith(SOURCE_SUFFIXES) and not path.startswith(EXCLUDED_PREFIXES)
}

listed = set()
folder = None
for line in (root / "CODEMAP.md").read_text(encoding="utf-8").splitlines():
    heading = re.match(r"##\s+(\S+)", line)
    if heading:
        name = heading.group(1)
        folder = "" if name == "(루트)" else name.rstrip("/") + "/"
        continue
    if folder is None or not line.startswith("|"):
        continue
    first_cell = line.split("|")[1]
    for name in re.findall(r"`([^`]+)`", first_cell):
        listed.add(folder + name)

missing = sorted(sources - listed)
stale = sorted(listed - sources)
for path in missing:
    print(f"CODEMAP.md에 없음: {path}  (해당 폴더 표에 한 줄 추가)")
for path in stale:
    print(f"CODEMAP.md에만 있음: {path}  (파일이 없거나 git이 추적하지 않음)")
if missing or stale:
    sys.exit(1)
print(f"CODEMAP.md OK: {len(sources)} files")
