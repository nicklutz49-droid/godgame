#!/bin/sh
# One-command build: configure + compile + self-test + stage a playable dist/.
# Works on whatever state the source tree is in — the CMake project lists its
# sources explicitly and defaults to Release, so new features need no edits here.
#
#   ./build.sh               build, run the headless self-test suite, stage dist/
#   ./build.sh --skip-tests  build and stage without the test gate
#   ./build.sh --run         all of the above, then launch the game
set -e
cd "$(dirname "$0")"

SKIP_TESTS=0
RUN_AFTER=0
for arg in "$@"; do
  case "$arg" in
    --skip-tests) SKIP_TESTS=1 ;;
    --run)        RUN_AFTER=1 ;;
    *) echo "unknown option: $arg (known: --skip-tests --run)"; exit 2 ;;
  esac
done

GEN=""
command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"

echo "== configure =="
cmake -B build $GEN -DCMAKE_BUILD_TYPE=Release

echo "== build =="
cmake --build build --parallel

BIN=build/godgame
[ -x "$BIN" ] || { echo "build finished but $BIN is missing"; exit 1; }

if [ "$SKIP_TESTS" = 0 ]; then
  echo "== self-test (--headless) =="
  if ! "$BIN" --headless > build/headless.log 2>&1; then
    tail -40 build/headless.log
    echo "SELF-TEST FAILED — dist/ not staged (full log: build/headless.log)"
    exit 1
  fi
  tail -1 build/headless.log
fi

echo "== stage dist/ =="
mkdir -p dist
cp "$BIN" dist/godgame
cp docs/PLAY.txt dist/PLAY.txt
echo "ready: dist/godgame   (see dist/PLAY.txt; maps/ and saves/ appear beside it as you play)"

[ "$RUN_AFTER" = 1 ] && exec ./dist/godgame
exit 0
