/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qmxdocument.h"

#include <QRegularExpression>
#include <QSet>

bool QmxDocument::validKey(const QString &key)
{
    static const QRegularExpression expression(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_-]*(\\.[A-Za-z_][A-Za-z0-9_-]*)?$"));
    return expression.match(key).hasMatch();
}

bool QmxDocument::parse(const QString &text, QStringList *errors)
{
    const QString normalized = QString(text).replace(QStringLiteral("\r\n"),
                                                      QStringLiteral("\n"));
    QStringList sourceLines = normalized.split(QLatin1Char('\n'));
    QSet<QString> seen;
    bool ok = true;

    m_lines.clear();
    m_finalNewline = normalized.endsWith(QLatin1Char('\n'));
    if (m_finalNewline && !sourceLines.isEmpty()) {
        sourceLines.removeLast();
    }

    for (qsizetype index = 0; index < sourceLines.size(); ++index) {
        const QString raw = sourceLines.at(index);
        const QString trimmed = raw.trimmed();
        Line line;
        line.raw = raw;

        if (trimmed.isEmpty()) {
            line.type = LineType::Blank;
        } else if (trimmed.startsWith(QLatin1Char('#'))) {
            line.type = LineType::Comment;
        } else {
            const qsizetype equals = raw.indexOf(QLatin1Char('='));
            if (equals < 0) {
                line.type = LineType::Invalid;
                ok = false;
                if (errors) {
                    errors->append(QStringLiteral("Line %1: missing '='").arg(index + 1));
                }
            } else {
                line.key = raw.left(equals).trimmed();
                line.value = raw.mid(equals + 1).trimmed();
                line.type = LineType::Assignment;
                if (!validKey(line.key)) {
                    line.type = LineType::Invalid;
                    ok = false;
                    if (errors) {
                        errors->append(QStringLiteral("Line %1: invalid key '%2'")
                                           .arg(index + 1).arg(line.key));
                    }
                } else if (line.value.isEmpty()) {
                    line.type = LineType::Invalid;
                    ok = false;
                    if (errors) {
                        errors->append(QStringLiteral("Line %1: empty value")
                                           .arg(index + 1));
                    }
                } else if (seen.contains(line.key)) {
                    ok = false;
                    if (errors) {
                        errors->append(QStringLiteral("Line %1: duplicate key '%2'")
                                           .arg(index + 1).arg(line.key));
                    }
                } else {
                    seen.insert(line.key);
                }
            }
        }
        m_lines.append(line);
    }
    return ok;
}

QString QmxDocument::serialize() const
{
    QStringList output;
    output.reserve(m_lines.size());
    for (const Line &line : m_lines) {
        output.append(line.raw);
    }
    QString text = output.join(QLatin1Char('\n'));
    if (m_finalNewline || text.isEmpty()) {
        text.append(QLatin1Char('\n'));
    }
    return text;
}

QString QmxDocument::value(const QString &key) const
{
    for (const Line &line : m_lines) {
        if (line.type == LineType::Assignment && line.key == key) {
            return line.value;
        }
    }
    return {};
}

bool QmxDocument::contains(const QString &key) const
{
    return !value(key).isNull();
}

void QmxDocument::setValue(const QString &key, const QString &value)
{
    for (Line &line : m_lines) {
        if (line.type == LineType::Assignment && line.key == key) {
            line.value = value;
            line.raw = QStringLiteral("%1 = %2").arg(key, value);
            return;
        }
    }

    if (!m_lines.isEmpty() && m_lines.constLast().type != LineType::Blank) {
        m_lines.append({LineType::Blank, {}, {}, {}});
    }
    m_lines.append({LineType::Assignment,
                    QStringLiteral("%1 = %2").arg(key, value), key, value});
}

void QmxDocument::remove(const QString &key)
{
    for (qsizetype index = m_lines.size() - 1; index >= 0; --index) {
        const Line &line = m_lines.at(index);
        if (line.type == LineType::Assignment && line.key == key) {
            m_lines.removeAt(index);
        }
    }
}

QStringList QmxDocument::keys() const
{
    QStringList result;
    for (const Line &line : m_lines) {
        if (line.type == LineType::Assignment) {
            result.append(line.key);
        }
    }
    return result;
}
