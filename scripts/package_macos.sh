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

echo "==> 對整個 bundle 進行 ad-hoc 重新簽名（修復 macdeployqt 改寫 install_name 後失效的簽名）"
# 未購買 Developer ID：以 ad-hoc（-）簽名，讓 bundle 具備有效簽名，避免「App 已損毀」。
# 使用者仍需移除 quarantine 屬性（見 README）。
codesign --force --deep --sign - --timestamp=none "$APP"
codesign --verify --deep --strict "$APP" && echo "   ad-hoc 簽名驗證通過" || echo "   （簽名驗證有警告，未簽名散佈可接受）"

echo "==> 驗證沒有殘留的 Homebrew 絕對路徑依賴"
if otool -L "$APP/Contents/MacOS/macpad++" | grep -E '/opt/homebrew|/usr/local/Cellar' ; then
  echo "!! 警告：仍有指向 Homebrew 的依賴，該機以外可能無法執行" >&2
else
  echo "   OK：無外部絕對路徑依賴"
fi

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
