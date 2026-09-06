/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemudiscovery.h"

#include <algorithm>

#include <QCollator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

static QString qmpError(const QJsonObject &response)
{
    const QJsonObject error = response.value(QStringLiteral("error")).toObject();
    return error.value(QStringLiteral("desc")).toString();
}

static void naturalSort(QStringList *items)
{
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(items->begin(), items->end(),
              [&collator](const QString &left, const QString &right) {
        return collator.compare(left, right) < 0;
    });
}

QemuDiscovery::QemuDiscovery(QObject *parent)
    : QObject(parent)
{
}

bool QemuDiscovery::waitForObject(QProcess &process, const QString &id,
                                  QJsonObject *response, QString *errorMessage)
{
    QByteArray pending;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (!process.waitForReadyRead(100) && process.state() == QProcess::NotRunning) {
            if (errorMessage) {
                *errorMessage = QString::fromUtf8(process.readAllStandardError()).trimmed();
            }
            return false;
        }
        pending.append(process.readAllStandardOutput());
        while (true) {
            const qsizetype newline = pending.indexOf('\n');
            if (newline < 0) {
                break;
            }
            const QByteArray line = pending.left(newline).trimmed();
            pending.remove(0, newline + 1);
            const QJsonDocument document = QJsonDocument::fromJson(line);
            if (!document.isObject()) {
                continue;
            }
            const QJsonObject object = document.object();
            if (id.isEmpty() || object.value(QStringLiteral("id")).toString() == id) {
                *response = object;
                return true;
            }
        }
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("QEMU did not answer the QMP request within 10 seconds");
    }
    return false;
}

static void sendCommand(QProcess &process, const QString &command, const QString &id)
{
    QJsonObject request{{QStringLiteral("execute"), command},
                        {QStringLiteral("id"), id}};
    process.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
    process.write("\n");
    process.waitForBytesWritten(1000);
}

static QString commandOutput(const QString &executable, const QStringList &arguments)
{
    QProcess process;
    process.start(executable, arguments);
    if (!process.waitForStarted(5000) || !process.waitForFinished(10000) ||
        process.exitStatus() != QProcess::NormalExit) {
        process.kill();
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput()
                             + process.readAllStandardError());
}

static void appendOptionsFromHelp(const QString &output,
                                  QList<QemuOption> *options)
{
    const QRegularExpression optionPattern(
        QStringLiteral("^\\s*-([A-Za-z0-9][A-Za-z0-9_-]*)"));
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = optionPattern.match(line);
        if (!match.hasMatch()) {
            continue;
        }
        const QString name = match.captured(1);
        bool present = false;
        for (const QemuOption &option : *options) {
            if (option.name == name) {
                present = true;
                break;
            }
        }
        if (!present) {
            options->append({name, {}, false});
        }
    }
}

static QStringList namesFromHelp(const QString &output, const QString &heading)
{
    QStringList names;
    bool reading = false;
    for (QString line : output.split(QLatin1Char('\n'))) {
        if (line.trimmed() == heading) {
            reading = true;
            continue;
        }
        if (!reading) {
            continue;
        }
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QStringLiteral("Recognized "))) {
            break;
        }
        const QString name = line.section(
            QRegularExpression(QStringLiteral("\\s+")), 0, 0);
        if (!name.isEmpty()) {
            names.append(name);
        }
    }
    names.removeDuplicates();
    naturalSort(&names);
    return names;
}

bool QemuDiscovery::discover(const QString &executable,
                             QemuCapabilities *capabilities,
                             QString *errorMessage)
{
    *capabilities = {};

    QProcess process;
    process.setProgram(executable);
    process.setArguments({QStringLiteral("-machine"), QStringLiteral("none"),
                          QStringLiteral("-nodefaults"),
                          QStringLiteral("-display"), QStringLiteral("none"),
                          QStringLiteral("-qmp"), QStringLiteral("stdio")});
    process.start();
    if (!process.waitForStarted(5000)) {
        if (errorMessage) {
            *errorMessage = process.errorString();
        }
        return false;
    }

    QJsonObject response;
    if (!waitForObject(process, {}, &response, errorMessage)) {
        process.kill();
        return false;
    }

    sendCommand(process, QStringLiteral("qmp_capabilities"), QStringLiteral("caps"));
    if (!waitForObject(process, QStringLiteral("caps"), &response, errorMessage)) {
        process.kill();
        return false;
    }
    if (!qmpError(response).isEmpty()) {
        if (errorMessage) {
            *errorMessage = qmpError(response);
        }
        process.kill();
        return false;
    }

    sendCommand(process, QStringLiteral("query-command-line-options"),
                QStringLiteral("options"));
    if (!waitForObject(process, QStringLiteral("options"), &response, errorMessage)) {
        process.kill();
        return false;
    }
    if (!qmpError(response).isEmpty()) {
        if (errorMessage) {
            *errorMessage = qmpError(response);
        }
        process.kill();
        return false;
    }
    for (const QJsonValue &value : response.value(QStringLiteral("return")).toArray()) {
        const QJsonObject object = value.toObject();
        QemuOption option;
        option.name = object.value(QStringLiteral("option")).toString().trimmed();
        if (option.name.isEmpty()) {
            continue;
        }
        for (const QJsonValue &parameterValue :
             object.value(QStringLiteral("parameters")).toArray()) {
            const QJsonObject parameter = parameterValue.toObject();
            option.parameters.append({parameter.value(QStringLiteral("name")).toString(),
                                      parameter.value(QStringLiteral("type")).toString(),
                                      parameter.value(QStringLiteral("optional")).toBool(true)});
        }
        option.argumentless = option.parameters.isEmpty();
        capabilities->options.append(option);
    }

    sendCommand(process, QStringLiteral("query-machines"), QStringLiteral("machines"));
    if (!waitForObject(process, QStringLiteral("machines"), &response, errorMessage)) {
        process.kill();
        return false;
    }
    if (!qmpError(response).isEmpty()) {
        if (errorMessage) {
            *errorMessage = qmpError(response);
        }
        process.kill();
        return false;
    }
    for (const QJsonValue &value : response.value(QStringLiteral("return")).toArray()) {
        const QJsonObject machine = value.toObject();
        const QString name = machine.value(QStringLiteral("name")).toString();
        const QString alias = machine.value(QStringLiteral("alias")).toString();
        capabilities->machines.append(name);
        if (!alias.isEmpty()) {
            capabilities->machines.append(alias);
        }
        if (machine.value(QStringLiteral("is-default")).toBool()) {
            capabilities->defaultMachine = alias.isEmpty() ? name : alias;
        }
    }

    sendCommand(process, QStringLiteral("query-cpu-definitions"), QStringLiteral("cpus"));
    if (waitForObject(process, QStringLiteral("cpus"), &response, errorMessage)) {
        for (const QJsonValue &value : response.value(QStringLiteral("return")).toArray()) {
            capabilities->cpus.append(value.toObject().value(QStringLiteral("name")).toString());
        }
    }

    sendCommand(process, QStringLiteral("quit"), QStringLiteral("quit"));
    process.waitForFinished(2000);
    capabilities->machines.removeDuplicates();
    naturalSort(&capabilities->machines);
    capabilities->cpus.removeDuplicates();
    naturalSort(&capabilities->cpus);

    if (capabilities->options.isEmpty()) {
        appendOptionsFromHelp(commandOutput(executable, {QStringLiteral("-help")}),
                              &capabilities->options);
    }
    if (capabilities->machines.isEmpty()) {
        capabilities->machines = namesFromHelp(
            commandOutput(executable,
                          {QStringLiteral("-machine"), QStringLiteral("help")}),
            QStringLiteral("Supported machines are:"));
    }
    if (capabilities->defaultMachine.isEmpty() &&
        !capabilities->machines.isEmpty()) {
        capabilities->defaultMachine = capabilities->machines.constFirst();
    }
    if (capabilities->cpus.isEmpty()) {
        capabilities->cpus = namesFromHelp(
            commandOutput(executable,
                          {QStringLiteral("-cpu"), QStringLiteral("help")}),
            QStringLiteral("Available CPUs:"));
    }

    QProcess acceleratorProcess;
    acceleratorProcess.start(executable,
                             {QStringLiteral("-accel"), QStringLiteral("help")});
    if (acceleratorProcess.waitForFinished(10000)) {
        const QString output = QString::fromUtf8(
            acceleratorProcess.readAllStandardOutput()
            + acceleratorProcess.readAllStandardError());
        const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (QString line : lines) {
            line = line.trimmed();
            if (line.isEmpty() || line.endsWith(QLatin1Char(':'))) {
                continue;
            }
            const QString accelerator = line.section(
                QRegularExpression(QStringLiteral("\\s+")), 0, 0);
            if (!accelerator.isEmpty()) {
                capabilities->accelerators.append(accelerator);
            }
        }
        capabilities->accelerators.removeDuplicates();
        capabilities->accelerators.sort(Qt::CaseInsensitive);
    } else {
        acceleratorProcess.kill();
    }
    if (capabilities->options.isEmpty() || capabilities->machines.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "QEMU returned no command-line options or machine definitions");
        }
        return false;
    }
    return true;
}

QStringList QemuDiscovery::firmwareDirectories(const QString &executable,
                                               QString *errorMessage)
{
    QProcess process;
    process.start(executable, {QStringLiteral("-L"), QStringLiteral("help")});
    if (!process.waitForFinished(10000)) {
        process.kill();
        if (errorMessage) {
            *errorMessage = QStringLiteral("QEMU firmware discovery timed out");
        }
        return {};
    }
    if (process.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = QString::fromUtf8(process.readAllStandardError()).trimmed();
        }
        return {};
    }
    QStringList directories = QString::fromUtf8(process.readAllStandardOutput())
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString &directory : directories) {
        directory = directory.trimmed();
    }
    return directories;
}

bool QemuDiscovery::validate(const QString &executable, const QString &qmxFile,
                             QString *output)
{
    QProcess process;
    process.start(executable, {QStringLiteral("-qmx-check"), qmxFile});
    if (!process.waitForFinished(30000)) {
        process.kill();
        if (output) {
            *output = QStringLiteral("QMX validation timed out");
        }
        return false;
    }
    if (output) {
        *output = QString::fromUtf8(process.readAllStandardOutput())
                      + QString::fromUtf8(process.readAllStandardError());
        *output = output->trimmed();
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
