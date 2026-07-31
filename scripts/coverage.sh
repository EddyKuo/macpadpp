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

# llvm-cov report 的欄位順序：
#   Filename Regions MissedRegions Cover Functions MissedFunctions Executed
#   Lines MissedLines Cover Branches MissedBranches Cover
# 故第 10 欄才是「行覆蓋率」——第 4 欄是 region、第 7 欄是 function，很容易看錯。
read -r LINE_COV LINES_MISSED <<<"$(awk '$1=="TOTAL" {gsub("%","",$10); print $10, $9}' "$REPORT")"
echo "行覆蓋率：${LINE_COV}%（未覆蓋 ${LINES_MISSED} 行）"

# 有設門檻才檢查：沒設時本腳本純粹是報告工具，不改變結束碼。
if [ -n "${MIN_LINE_COVERAGE:-}" ]; then
    # bash 無浮點運算，用 awk 比較
    if awk -v c="$LINE_COV" -v m="$MIN_LINE_COVERAGE" 'BEGIN { exit !(c < m) }'; then
        echo "❌ 行覆蓋率 ${LINE_COV}% 低於門檻 ${MIN_LINE_COVERAGE}%" >&2
        echo "   覆蓋率最低的檔案：" >&2
        awk 'NR>2 && NF>=10 && $1!~/^-/ && $1!="TOTAL" {c=$10; gsub("%","",c);
             if (c+0 < 90) printf "     %-46s %s（未覆蓋 %s 行）\n", $1, $10, $9}' \
            "$REPORT" | sort -k2 -n | head -12 >&2
        exit 1
    fi
    echo "✅ 行覆蓋率 ${LINE_COV}% ≥ 門檻 ${MIN_LINE_COVERAGE}%"
fi
