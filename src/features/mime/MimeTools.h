#pragma once

// MimeTools — MIME 編碼工具（Notepad++ MIME Tools 選單對等）
// 全部為純函式，輸入輸出皆 QString，可完整單元測試。
// Base64 編碼/解碼、URL 編碼/解碼；文字一律視為 UTF-8。

#include <QString>

namespace macpad::features {

class MimeTools {
public:
    // --- Base64 ---
    static QString base64Encode(const QString &s);
    static QString base64Decode(const QString &s);

    // --- URL ---
    static QString urlEncode(const QString &s);
    static QString urlDecode(const QString &s);
};

}  // namespace macpad::features
