#include "features/findinfiles/FindInFilesEngine.h"

#include "core/FileEncoding.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

namespace macpad::features {

static QRegularExpression buildRegex(const FindInFilesOptions &o, bool *ok)
{
    QString pat = o.regex ? o.pattern : QRegularExpression::escape(o.pattern);
    if (o.wholeWord)
        pat = QStringLiteral("\\b") + pat + QStringLiteral("\\b");
    QRegularExpression::PatternOptions po = QRegularExpression::NoPatternOption;
    if (!o.caseSensitive)
        po |= QRegularExpression::CaseInsensitiveOption;
    QRegularExpression re(pat, po);
    *ok = re.isValid();
    return re;
}

// 以「預先編譯好的 regex」逐檔比對（NFR-005：全程只編譯一次）
static QVector<FindMatch> searchInTextWithRe(const QString &filePath, const QString &content,
                                             const QRegularExpression &re)
{
    QVector<FindMatch> out;
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        auto it = re.globalMatch(line);
        while (it.hasNext()) {
            const auto m = it.next();
            out.push_back({filePath, i + 1, int(m.capturedStart()) + 1, line});
        }
    }
    return out;
}

// 以「預先編譯好的 regex」取代單一內容（NFR-005：全程只編譯一次）
static QString replaceInTextWithRe(const QString &content, const QRegularExpression &re,
                                   const QString &replacement, int *count)
{
    int n = 0;
    auto it = re.globalMatch(content);
    while (it.hasNext()) { it.next(); ++n; }
    if (count) *count = n;
    QString out = content;
    if (n > 0)
        out.replace(re, replacement);
    return out;
}

QVector<FindMatch> FindInFilesEngine::searchInText(const QString &filePath,
                                                   const QString &content,
                                                   const FindInFilesOptions &opts)
{
    QVector<FindMatch> out;
    bool ok = false;
    const QRegularExpression re = buildRegex(opts, &ok);
    if (!ok || opts.pattern.isEmpty())
        return out;
    return searchInTextWithRe(filePath, content, re);
}

QString FindInFilesEngine::replaceInText(const QString &content, const FindInFilesOptions &opts,
                                         const QString &replacement, int *count)
{
    bool ok = false;
    const QRegularExpression re = buildRegex(opts, &ok);
    if (!ok || opts.pattern.isEmpty()) {
        if (count) *count = 0;
        return content;
    }
    return replaceInTextWithRe(content, re, replacement, count);
}

FindInFilesEngine::ReplaceResult FindInFilesEngine::replaceInFiles(
    const QString &rootDir, const FindInFilesOptions &opts,
    const QString &replacement, std::atomic<bool> *cancel)
{
    ReplaceResult result;
    bool ok = false;
    const QRegularExpression re = buildRegex(opts, &ok);  // 全程只編譯一次（NFR-005）
    if (!ok || opts.pattern.isEmpty())
        return result;

    const QDir::Filters filters = QDir::Files | QDir::NoDotAndDotDot;
    const QDirIterator::IteratorFlags flags =
        opts.recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
    QDirIterator it(rootDir, opts.fileFilters, filters, flags);
    while (it.hasNext()) {
        if (cancel && cancel->load())
            break;
        const QString path = it.next();
        if (QFileInfo(path).size() > opts.maxFileBytes)
            continue;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray raw = f.readAll();
        f.close();
        if (raw.contains('\0'))
            continue;

        // 依偵測編碼解碼，取代後以「同一編碼」寫回，避免非 UTF-8 檔被 mojibake 破壞（IL-4）
        const core::DetectResult det = core::FileEncoding::detect(raw.left(65536));
        int n = 0;
        const QString newContent =
            replaceInTextWithRe(core::FileEncoding::decode(raw, det.encoding), re, replacement, &n);
        if (n > 0) {
            QSaveFile out(path);
            if (out.open(QIODevice::WriteOnly)) {
                const QByteArray bytes = core::FileEncoding::encode(newContent, det.encoding);
                out.write(bytes);
                if (out.commit()) {
                    result.filesChanged++;
                    result.replacements += n;
                }
            }
        }
    }
    return result;
}

QVector<FindMatch> FindInFilesEngine::search(const QString &rootDir,
                                             const FindInFilesOptions &opts,
                                             std::atomic<bool> *cancel)
{
    QVector<FindMatch> out;
    bool ok = false;
    const QRegularExpression re = buildRegex(opts, &ok);  // 全程只編譯一次（NFR-005）
    if (!ok || opts.pattern.isEmpty())
        return out;

    const QDir::Filters filters = QDir::Files | QDir::NoDotAndDotDot;
    const QDirIterator::IteratorFlags flags =
        opts.recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;

    QStringList nameFilters = opts.fileFilters;
    QDirIterator it(rootDir, nameFilters, filters, flags);
    while (it.hasNext()) {
        if (cancel && cancel->load())
            break;
        const QString path = it.next();
        const QFileInfo fi(path);
        if (fi.size() > opts.maxFileBytes)
            continue;

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray raw = f.readAll();
        f.close();

        // 略過二進位檔（含 NUL）
        if (raw.contains('\0'))
            continue;

        // 依偵測編碼解碼，避免非 UTF-8 檔在預覽出現 mojibake
        const core::DetectResult det = core::FileEncoding::detect(raw.left(65536));
        out += searchInTextWithRe(path, core::FileEncoding::decode(raw, det.encoding), re);
    }
    return out;
}

}  // namespace macpad::features
