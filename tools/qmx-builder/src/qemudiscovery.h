/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

struct QemuOptionParameter
{
    QString name;
    QString type;
    bool optional = true;
};

struct QemuOption
{
    QString name;
    QList<QemuOptionParameter> parameters;
    bool argumentless = false;
};

struct QemuCapabilities
{
    QList<QemuOption> options;
    QStringList machines;
    QStringList cpus;
    QStringList accelerators;
    QString defaultMachine;
};

class QemuDiscovery : public QObject
{
public:
    explicit QemuDiscovery(QObject *parent = nullptr);

    bool discover(const QString &executable, QemuCapabilities *capabilities,
                  QString *errorMessage);
    static QStringList firmwareDirectories(const QString &executable,
                                           QString *errorMessage);
    static bool validate(const QString &executable, const QString &qmxFile,
                         QString *output);

private:
    static bool waitForObject(class QProcess &process, const QString &id,
                              class QJsonObject *response, QString *errorMessage);
};
