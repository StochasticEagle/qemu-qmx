/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class QmxDocument
{
public:
    enum class LineType {
        Blank,
        Comment,
        Assignment,
        Invalid,
    };

    struct Line {
        LineType type = LineType::Blank;
        QString raw;
        QString key;
        QString value;
    };

    bool parse(const QString &text, QStringList *errors = nullptr);
    QString serialize() const;

    const QList<Line> &lines() const { return m_lines; }
    QString value(const QString &key) const;
    bool contains(const QString &key) const;
    void setValue(const QString &key, const QString &value);
    void remove(const QString &key);
    QStringList keys() const;

private:
    static bool validKey(const QString &key);
    QList<Line> m_lines;
    bool m_finalNewline = true;
};
