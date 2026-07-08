#pragma once

// CliArgs — 命令列參數解析（FR-033, IR-005, FR-051）
// 解析 `path`、`path:line` 或旗標（-n<line> -c<col> -ro -nosession -multiInst）；純邏輯、可單元測試。

#include <QString>
#include <QStringList>
#include <QVector>

namespace macpad::features {

struct FileArg {
    QString path;
    int line = 0;    // 0 = 未指定
    int column = 0;  // 0 = 未指定（FR-051 新增）
};

// FR-051：完整命令列解析結果
struct ParsedArgs {
    QVector<FileArg> files;
    bool readOnly = false;
    bool noSession = false;
    bool multiInstance = false;
    int gotoLine = 0;
    int gotoColumn = 0;
};

class CliArgs {
public:
    // 解析單一檔案參數；尾端 ":<digits>" 視為行號（避免誤判 Windows 磁碟代號）
    static FileArg parseFileArg(const QString &arg);

    // FR-051：解析完整命令列引數清單，辨識旗標並將其餘視為檔案路徑
    static ParsedArgs parse(const QStringList &args);
};

}  // namespace macpad::features
