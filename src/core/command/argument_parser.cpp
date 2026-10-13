#include "core/command/argument_parser.h"

#include <QRegularExpression>

namespace snipaste::core {

QList<int> parseIntegerList(QString value)
{
    value.replace(QChar(';'), QChar(','));
    value.replace(QChar('x'), QChar(','));
    value.replace(QChar('X'), QChar(','));
    const QStringList parts = value.split(QRegularExpression(QStringLiteral("[, \\t]+")),
                                          Qt::SkipEmptyParts);
    QList<int> result;
    for (const QString &part : parts) {
        bool ok = false;
        const int number = part.toInt(&ok);
        if (!ok)
            return {};
        result.append(number);
    }
    return result;
}

QRect parseRectSpec(const QString &value)
{
    const QList<int> numbers = parseIntegerList(value);
    return numbers.size() == 4
        ? QRect(numbers.at(0), numbers.at(1), numbers.at(2), numbers.at(3))
        : QRect();
}

QSize parseSizeSpec(const QString &value)
{
    const QList<int> numbers = parseIntegerList(value);
    return numbers.size() == 2 ? QSize(numbers.at(0), numbers.at(1)) : QSize();
}

QPoint parsePointSpec(const QString &value)
{
    const QList<int> numbers = parseIntegerList(value);
    return numbers.size() == 2 ? QPoint(numbers.at(0), numbers.at(1)) : QPoint();
}

QStringList normalizeMultiValueOptions(QStringList arguments)
{
    const auto numeric = [](const QString &value) {
        bool ok = false;
        value.toInt(&ok);
        return ok;
    };
    const auto joinValues = [&arguments, &numeric](const QString &option, int count) {
        for (int index = 1; index < arguments.size(); ++index) {
            if (arguments.at(index) != option)
                continue;
            if (index + count >= arguments.size())
                continue;
            bool valid = true;
            QStringList values;
            for (int offset = 1; offset <= count; ++offset) {
                const QString value = arguments.at(index + offset);
                if (value.startsWith('-') && !numeric(value)) {
                    valid = false;
                    break;
                }
                values.append(value);
            }
            if (!valid)
                continue;
            arguments[index] = option + QStringLiteral("=") + values.join(QChar(','));
            arguments.erase(arguments.begin() + index + 1,
                            arguments.begin() + index + 1 + count);
            --index;
        }
    };
    joinValues(QStringLiteral("--area"), 4);
    joinValues(QStringLiteral("--size"), 2);
    joinValues(QStringLiteral("--pos"), 2);
    for (int index = 1; index < arguments.size(); ++index) {
        if (arguments.at(index) == QStringLiteral("--block")
            && (index + 1 >= arguments.size() || arguments.at(index + 1).startsWith('-')))
            arguments[index] = QStringLiteral("--block=");
    }
    return arguments;
}

QStringList splitOutputSpec(const QString &spec)
{
    return spec.split(QChar(';'), Qt::SkipEmptyParts);
}

QString expandOutputFilePattern(const QString &pattern, const QDateTime &dateTime)
{
    QString expanded = pattern;
    const QRegularExpression token(QStringLiteral("\\$([^$]+)\\$"));
    QRegularExpressionMatch match = token.match(expanded);
    while (match.hasMatch()) {
        expanded.replace(match.capturedStart(), match.capturedLength(),
                         dateTime.toString(match.captured(1)));
        match = token.match(expanded);
    }
    return expanded;
}

} // namespace snipaste::core
