/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qmxdocument.h"

#include <QtTest/QTest>

class TestQmxDocument : public QObject
{
    Q_OBJECT

private slots:
    void preservesUnknownContent();
    void editsOneAssignment();
    void rejectsDuplicateKeys();
};

void TestQmxDocument::preservesUnknownContent()
{
    const QString source = QStringLiteral(
        "# retained comment\nqmx = 1\nfuture.option = opaque,value\n\n");
    QmxDocument document;
    QVERIFY(document.parse(source));
    QCOMPARE(document.serialize(), source);
}

void TestQmxDocument::editsOneAssignment()
{
    QmxDocument document;
    QVERIFY(document.parse(QStringLiteral("qmx = 1\nmemory = 4G\n")));
    document.setValue(QStringLiteral("memory"), QStringLiteral("8G"));
    QCOMPARE(document.serialize(), QStringLiteral("qmx = 1\nmemory = 8G\n"));
}

void TestQmxDocument::rejectsDuplicateKeys()
{
    QmxDocument document;
    QStringList errors;
    QVERIFY(!document.parse(QStringLiteral("qmx = 1\nqmx = 1\n"), &errors));
    QVERIFY(!errors.isEmpty());
}

QTEST_MAIN(TestQmxDocument)
#include "test_qmxdocument.moc"
