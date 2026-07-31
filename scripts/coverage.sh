#!/usr/bin/env bash
# 以 clang source-based coverage 量測 src/ 的行覆蓋率。
#
# 用法：scripts/coverage.sh [輸出報告路徑]
#
# 為何不是 gcov：Homebrew 的 clang++ 產出的 .gcda 需要相符的 gcov 版本，
# 在 macOS 上版本錯配是常態；llvm-cov 走 profraw/profdata，與編譯器同源，不會有這問題。
set -euo pipefail

cd "$(dirname "$0")/.."
BUILD=build-cov
REPORT="${1:-$BUILD/coverage.txt}"

cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
    -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate" \
    -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" >/dev/null
cmake --build "$BUILD" -j >/dev/null

PROF="$BUILD/covprof"
rm -rf "$PROF" && mkdir -p "$PROF"

# 每個測試各自輸出一份 profraw，避免同名覆蓋（%p 帶入 pid）。
# 用 while read 而非 mapfile：macOS 內建的是 bash 3.2，沒有 mapfile。
TESTS=()
while IFS= read -r line; do
    TESTS+=("$line")
done < <(find "$BUILD/tests" -type f -perm -u+x -name 'test_*' ! -path '*.dSYM*' | sort)
for t in "${TESTS[@]}"; do
    LLVM_PROFILE_FILE="$PROF/$(basename "$t")-%p.profraw" "$t" >/dev/null 2>&1 || true
done

XCRUN=(xcrun)
"${XCRUN[@]}" llvm-profdata merge -sparse "$PROF"/*.profraw -o "$BUILD/merged.profdata"

# 以任一測試執行檔為 coverage mapping 來源；其餘用 -object 疊加，
# 這樣才涵蓋「只被某一個測試連結到」的程式碼。
OBJS=()
for t in "${TESTS[@]}"; do OBJS+=(-object "$t"); done

"${XCRUN[@]}" llvm-cov report "${OBJS[@]}" \
    -instr-profile="$BUILD/merged.profdata" \
    -ignore-filename-regex='(tests/|/opt/homebrew/|\.moc$|moc_|qrc_|/usr/)' \
    > "$REPORT"

echo "報告：$REPORT"
tail -3 "$REPORT"
