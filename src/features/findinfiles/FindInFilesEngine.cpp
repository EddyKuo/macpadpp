#include "features/findinfiles/FindInFilesEngine.h"

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

QVector<FindMatch> FindInFilesEngine::searchInText(const QString &filePath,
                                                   const QString &content,
                                                   const FindInFilesOptions &opts)
{
    QVector<FindMatch> out;
    bool ok = false;
    const QRegularExpression re = buildRegex(opts, &ok);
    if (!ok || opts.pattern.isEmpty())
        return out;

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

QString FindInFilesEngine::replaceInText(const QString &content, const FindInFilesOptions &opts,
                                         const QString &replacement, int *count)
{
    bool ok = false;
    const QRegularExpression re = buildRegex(opts, &ok);
    if (!ok || opts.pattern.isEmpty()) {
        if (count) *count = 0;
        return content;
    }
    int n = 0;
    auto it = re.globalMatch(content);
    while (it.hasNext()) { it.next(); ++n; }
    if (count) *count = n;
    QString out = content;
    if (n > 0)
        out.replace(re, replacement);
    return out;
}

FindInFilesEngine::ReplaceResult FindInFilesEngine::replaceInFiles(
    const QString &rootDir, const FindInFilesOptions &opts,
    const QString &replacement, std::atomic<bool> *cancel)
{
    ReplaceResult result;
    bool ok = false;
    buildRegex(opts, &ok);
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

        int n = 0;
        const QString newContent = replaceInText(QString::fromUtf8(raw), opts, replacement, &n);
        if (n > 0) {
            QSaveFile out(path);
            if (out.open(QIODevice::WriteOnly)) {
                out.write(newContent.toUtf8());
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
    buildRegex(opts, &ok);
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

        out += searchInText(path, QString::fromUtf8(raw), opts);
    }
    return out;
}

}  // namespace macpad::features
