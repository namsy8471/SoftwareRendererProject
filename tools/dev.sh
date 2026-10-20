#!/usr/bin/env bash
# Linux/macOS/클라우드용 headless 개발 루프. Windows는 tests/RunTests.ps1, tests/Bench.ps1을 쓴다.
#   tools/dev.sh build            # CMake Release 빌드 (out/release)
#   tools/dev.sh test [필터]      # 빌드 + RendererTests (PASS 줄은 숨김)
#   tools/dev.sh golden           # 골든 PNG 갱신 (의도한 화면 변경일 때만)
#   tools/dev.sh bench [모드...]  # srcli bench --json (기본 fill wire normal)
#   tools/dev.sh render 모드 파일 # 마지막 프레임 PNG 저장
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="$root/out/release"

build() {
  if [ ! -f "$build_dir/build.ninja" ]; then
    cmake --preset release -S "$root" >/dev/null
  fi
  cmake --build "$build_dir" 2>&1 | grep -E "error|warning:|FAILED" || true
  test -x "$build_dir/RendererTests" -a -x "$build_dir/srcli"
}

cmd="${1:-test}"
shift || true
case "$cmd" in
  build) build ;;
  test)
    build
    args=("$root")
    if [ $# -gt 0 ]; then args+=(--filter "$1"); fi
    "$build_dir/RendererTests" "${args[@]}" | grep -v '^PASS '
    ;;
  golden)
    build
    "$build_dir/RendererTests" "$root" --filter golden --update-golden | grep -v '^PASS '
    ;;
  bench)
    build
    modes=("$@")
    if [ ${#modes[@]} -eq 0 ]; then modes=(fill wire normal); fi
    for mode in "${modes[@]}"; do
      "$build_dir/srcli" bench --mode "$mode" --json
    done
    ;;
  render)
    build
    "$build_dir/srcli" render --mode "${1:-fill}" --out "${2:-$root/tests/out/frame.png}"
    ;;
  *) echo "unknown command: $cmd" >&2; exit 2 ;;
esac
