#pragma once

// FileEncoding — 編碼/BOM/EOL 偵測與編解碼（FR-019/020, CON-005）
// 偵測只讀開頭 buffer（CON-005 安全/效能）；內部一律 UTF-8。

#include <QByteArray>
#include <QString>
#include <QVector>

namespace macpad::core {

enum class Encoding {
    Utf8,       // UTF-8 無 BOM
    Utf8Bom,    // UTF-8 with BOM
    Utf16LE,
    Utf16BE,
    Latin1      // 無法判定時的回退（ANSI 近似）
};

enum class Eol {
    Lf,     // Unix \n
    CrLf,   // Windows \r\n
    Cr      // 舊 Mac \r
};

struct DetectResult {
    Encoding encoding = Encoding::Utf8;
    Eol eol = Eol::Lf;
    bool hasBom = false;
};

class FileEncoding {
public:
    // 偵測編碼（依 BOM 與 UTF-8 有效性）與 EOL（掃描開頭）。sampleBytes 為安全上限。
    static DetectResult detect(const QByteArray &head);

    // 依偵測結果將原始位元組解碼為 QString（內部 UTF-8 表示）
    static QString decode(const QByteArray &raw, Encoding enc);

    // 將 QString 依指定編碼編碼回位元組（含/不含 BOM 由 enc 決定）
    static QByteArray encode(const QString &text, Encoding enc);

    static QString encodingName(Encoding enc);
    static QString eolName(Eol eol);

    // === 傳統/區域編碼（Qt5Compat QTextCodec，複刻 Notepad++「Character sets」）===
    // 以具名 codec 解碼/編碼（如 "Big5"、"Shift_JIS"、"windows-1251"）。codec 不存在時回退 UTF-8。
    static QString decodeWithCodec(const QByteArray &raw, const QString &codecName);
    static QByteArray encodeWithCodec(const QString &text, const QString &codecName);

    // Character sets 選單資料：地區分組 → [(顯示名, codec 名)]
    struct Charset { QString display; QString codec; };
    struct CharsetGroup { QString region; QVector<Charset> items; };
    static QVector<CharsetGroup> characterSets();
};

}  // namespace macpad::core
