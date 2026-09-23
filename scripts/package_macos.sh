#!/usr/bin/env bash
# package_macos.sh — 建置 macpad++、以 macdeployqt 同梱 Qt/QScintilla，打包成可散佈的 .dmg。
# 未簽名散佈：使用者初次開啟需右鍵→開啟，或 `xattr -dr com.apple.quarantine macpad++.app`。
#
# 用法：
#   scripts/package_macos.sh <version> [arch]
# 例：
#   scripts/package_macos.sh 0.1.0            # 用主機架構
#   scripts/package_macos.sh 0.1.0 arm64      # DMG 檔名帶架構後綴
#
# 產出：dist/macpad++-<version>[-<arch>].dmg
set -euo pipefail

VERSION="${1:?用法：package_macos.sh <version> [arch]}"
ARCH="${2:-$(uname -m)}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

QT_PREFIX="$(brew --prefix qt)"
MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"
BUILD_DIR="build-release"
APP="$BUILD_DIR/src/macpad++.app"
DIST="dist"
DMG_NAME="macpad++-${VERSION}${ARCH:+-$ARCH}"

echo "==> 設定並建置 Release (${ARCH})"
cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DMACPAD_BUILD_TESTS=OFF \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX"
cmake --build "$BUILD_DIR" -j

echo "==> macdeployqt 同梱 Qt 框架與 plugin"
# -always-overwrite 讓重複執行可覆蓋；不加 -codesign 即未簽名。
"$MACDEPLOYQT" "$APP" -verbose=1 -always-overwrite

echo "==> 檢查 QScintilla 是否已同梱（macdeployqt 有時漏抓）"
QSCI_LIB="$(otool -L "$APP/Contents/MacOS/macpad++" | awk '/qscintilla2/{print $1; exit}')"
if [ -n "${QSCI_LIB:-}" ] && [[ "$QSCI_LIB" != @rpath/* ]] && [[ "$QSCI_LIB" != @executable_path/* ]]; then
  echo "   QScintilla 仍指向 $QSCI_LIB —— 手動複製並改寫路徑"
  FRAMEWORKS="$APP/Contents/Frameworks"
  mkdir -p "$FRAMEWORKS"
  BASENAME="$(basename "$QSCI_LIB")"
  cp -f "$QSCI_LIB" "$FRAMEWORKS/$BASENAME"
  chmod u+w "$FRAMEWORKS/$BASENAME"
  install_name_tool -change "$QSCI_LIB" "@rpath/$BASENAME" "$APP/Contents/MacOS/macpad++"
  install_name_tool -id "@rpath/$BASENAME" "$FRAMEWORKS/$BASENAME"
  # 讓剛複製進來的 QScintilla 依賴的 Qt 也走 bundle 內的 @rpath（再跑一次 deploy）
  "$MACDEPLOYQT" "$APP" -verbose=1 -always-overwrite
fi

echo "==> 補上 macdeployqt 漏抓的 transitive 依賴（如 WebEngine 影像編解碼 libwebp/libsharpyuv）"
FRAMEWORKS="$APP/Contents/Frameworks"
mkdir -p "$FRAMEWORKS"
for missing in libwebp.7.dylib libsharpyuv.0.dylib libwebpdemux.2.dylib libwebpmux.3.dylib; do
  if [ ! -e "$FRAMEWORKS/$missing" ]; then
    SRC="$(find "$(brew --prefix)/opt" -maxdepth 3 -name "$missing" 2>/dev/null | head -1)"
    if [ -n "$SRC" ]; then
      echo "   複製 $missing ← $SRC"
      cp -f "$SRC" "$FRAMEWORKS/$missing"
      chmod u+w "$FRAMEWORKS/$missing"
      install_name_tool -id "@rpath/$missing" "$FRAMEWORKS/$missing"
    fi
  fi
done

echo "==> 修正 QtWebEngineProcess helper（Markdown 預覽依賴它，macdeployqt 不處理）"
# QtWebEngine 的每個網頁都跑在獨立的 helper 程序裡，其位置是巢狀在 framework 內的另一個 .app：
#   Contents/Frameworks/QtWebEngineCore.framework/Versions/A/Helpers/QtWebEngineProcess.app
# macdeployqt 完全不碰它，造成兩個問題（v0.7.0 起的發佈版 Markdown 預覽都受影響）：
#  1. helper 本身的 Qt 依賴仍是 Homebrew 絕對路徑 → 沒裝 Homebrew Qt 的電腦直接載入失敗。
#     改寫為 @rpath/…；helper 自帶的 rpath「@loader_path/../../../../../../../」剛好指回
#     主程式的 Contents/Frameworks，不必另加。
#  2. macdeployqt 把各 Qt framework 之間的依賴改成 @executable_path/../Frameworks/…，
#     在 helper 程序裡 @executable_path 是 helper 自己的 MacOS/，會找不到（例：QtWebChannel）。
#     在 helper 的 Contents/ 放一個指回主程式 Frameworks/ 的相對 symlink，兩種程序就解析到同一處。
find "$FRAMEWORKS" -path '*/Helpers/*.app' -type d -prune | while read -r HELPER_APP; do
  HELPER_NAME="$(basename "$HELPER_APP" .app)"
  HELPER_BIN="$HELPER_APP/Contents/MacOS/$HELPER_NAME"
  [ -f "$HELPER_BIN" ] || continue
  echo "   $HELPER_NAME"
  otool -L "$HELPER_BIN" | tail -n +2 | awk '{print $1}' \
    | { grep -E '^(/opt/homebrew|/usr/local)/' || true; } | while read -r DEP; do
      FW_REL="$(echo "$DEP" | grep -oE '[^/]+\.framework/.*$' || true)"
      if [ -z "$FW_REL" ]; then
        echo "!! $HELPER_NAME 依賴非 framework 的外部函式庫：$DEP" >&2; exit 1
      fi
      if [ ! -e "$FRAMEWORKS/$FW_REL" ]; then
        echo "!! $HELPER_NAME 需要的 $FW_REL 未同梱進 bundle" >&2; exit 1
      fi
      install_name_tool -change "$DEP" "@rpath/$FW_REL" "$HELPER_BIN"
    done
  # 從 helper 的 Contents/ 往上六層即主程式的 Contents/Frameworks：
  # Contents → QtWebEngineProcess.app → Helpers → A → Versions → QtWebEngineCore.framework → Frameworks
  ln -sfn "../../../../../.." "$HELPER_APP/Contents/Frameworks"
  [ -e "$HELPER_APP/Contents/Frameworks/QtCore.framework" ] \
    || { echo "!! helper 的 Frameworks symlink 沒有指到主程式的 Frameworks/" >&2; exit 1; }
done

# macdeployqt 會留下某些 dylib 自身 install id 的 Homebrew 路徑（例 libbrotlicommon），改為 @rpath。
for LIB in "$FRAMEWORKS"/*.dylib; do
  ID="$(otool -D "$LIB" | tail -n +2)"
  case "$ID" in /opt/homebrew/*|/usr/local/*) install_name_tool -id "@rpath/$(basename "$LIB")" "$LIB" ;; esac
done

echo "==> 對整個 bundle 進行 ad-hoc 重新簽名（修復 macdeployqt 改寫 install_name 後失效的簽名）"
# 未購買 Developer ID：以 ad-hoc（-）簽名，讓 bundle 具備有效簽名，避免「App 已損毀」。
# 使用者仍需移除 quarantine 屬性（見 README）。
codesign --force --deep --sign - --timestamp=none "$APP"
codesign --verify --deep --strict "$APP" && echo "   ad-hoc 簽名驗證通過" || echo "   （簽名驗證有警告，未簽名散佈可接受）"

echo "==> 驗證整個 bundle（含巢狀 helper）沒有殘留的 Homebrew 絕對路徑依賴"
# 先前只檢查主程式且只警告，巢狀 helper 的問題因此從 v0.7.0 一路漏到發佈版。
# 改為掃描 bundle 內每個 Mach-O，有殘留就讓打包失敗。
LEAKS="$(find "$APP" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 \
  | while IFS= read -r -d '' F; do
      file -b "$F" | grep -q 'Mach-O' || continue
      otool -L "$F" | tail -n +2 | awk '{print $1}' \
        | { grep -E '^(/opt/homebrew|/usr/local)/' || true; } | sed "s|^|${F#$APP/}: |"
    done)"
if [ -n "$LEAKS" ]; then
  echo "!! 仍有指向 Homebrew 的依賴，該機以外無法執行：" >&2
  echo "$LEAKS" >&2
  exit 1
fi
echo "   OK：無外部絕對路徑依賴"

echo "==> 產生 DMG：$DIST/$DMG_NAME.dmg"
rm -rf "$DIST"; mkdir -p "$DIST"
STAGING="$(mktemp -d)"
cp -R "$APP" "$STAGING/"
ln -s /Applications "$STAGING/Applications"   # 讓使用者拖曳安裝
hdiutil create \
  -volname "macpad++ $VERSION" \
  -srcfolder "$STAGING" \
  -ov -format UDZO \
  "$DIST/$DMG_NAME.dmg"
rm -rf "$STAGING"

echo "==> 完成：$DIST/$DMG_NAME.dmg"
ls -lh "$DIST/$DMG_NAME.dmg"
