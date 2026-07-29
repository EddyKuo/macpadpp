#!/usr/bin/env bash
# 由母版點陣圖重建應用程式圖示：macpad.iconset/ + macpad.icns（macOS）+ macpad.ico（Windows），
# 並更新 SOURCE.sha256 供 CI 檢查「母版改了但衍生檔沒重建」。
#
# 母版是 macpad-1024.png 而不是 macpad.svg：把 SVG 轉點陣需要 rsvg-convert，
# 而 ImageMagick 少了這個 delegate 時會退回內建解析器、算繪結果明顯走樣。
# 綁在一個未必存在的外部工具上，只會讓重建腳本在最需要它的時候失效。
# macpad.svg 仍是設計母源；改了 SVG 就要用設計工具重新匯出 macpad-1024.png。
#
# 需要：macOS（sips / iconutil）與 ImageMagick（magick）。
# 用法：scripts/icons/build_app_icons.sh [輸出目錄]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC_DIR="$ROOT/resources/icon"
OUT="${1:-$SRC_DIR}"
MASTER="$SRC_DIR/macpad-1024.png"

for tool in sips iconutil magick; do
    command -v "$tool" >/dev/null || { echo "缺少必要工具：$tool" >&2; exit 1; }
done
[ -f "$MASTER" ] || { echo "找不到母版：$MASTER" >&2; exit 1; }

mkdir -p "$OUT"
ICONSET="$OUT/macpad.iconset"
rm -rf "$ICONSET"; mkdir -p "$ICONSET"

# macOS iconset 的檔名與尺寸由 iconutil 規定，不可任意命名
for pair in "16 icon_16x16" "32 icon_16x16@2x" "32 icon_32x32" "64 icon_32x32@2x" \
            "128 icon_128x128" "256 icon_128x128@2x" "256 icon_256x256" \
            "512 icon_256x256@2x" "512 icon_512x512" "1024 icon_512x512@2x"; do
    set -- $pair
    sips -z "$1" "$1" "$MASTER" --out "$ICONSET/$2.png" >/dev/null
done

iconutil -c icns "$ICONSET" -o "$OUT/macpad.icns"

# Windows .ico：多尺寸單檔。含 256 才能在檔案總管「特大圖示」不糊。
magick "$MASTER" -define icon:auto-resize=256,128,64,48,32,24,16 "$OUT/macpad.ico"

# 記錄「這批衍生檔是由哪個母版產生的」。CI 比對此檔，母版改了卻沒重建就會失敗。
{
    echo "# 由 scripts/icons/build_app_icons.sh 產生，請勿手動編輯。"
    echo "# 母版變更後重新執行該腳本即可更新本檔。"
    (cd "$SRC_DIR" && shasum -a 256 macpad-1024.png macpad.svg)
} > "$OUT/SOURCE.sha256"

echo "已重建：${OUT}/macpad.icns ${OUT}/macpad.ico ${ICONSET} ${OUT}/SOURCE.sha256"
