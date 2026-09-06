/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "qemudiscovery.h"
#include "qmxdocument.h"

#include <QMainWindow>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTableWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    bool openFile(const QString &path);

private slots:
    void newDocument();
    void openDocument();
    bool saveDocument();
    bool saveDocumentAs();
    void architectureChanged(int index);
    void discoverQemu();
    void validateDocument();
    void launchDocument();
    void createDiskImage();
    void sourceChanged();
    bool applyBasicFields();
    void addAssignment();
    void removeAssignment();
    void addCatalogOption();

private:
    void buildUi();
    void loadFromDocument();
    void syncBasicFieldsFromDocument();
    void updateSourceFromDocument();
    void updateAssignments();
    void populateArchitectures();
    void selectArchitectureForExecutable(const QString &executable);
    QString findSystemEmulator(const QString &target) const;
    void setCurrentFile(const QString &path);
    bool writeFile(const QString &path);
    QString validationFile(QString *temporaryPath);
    static QString quoted(const QString &value);

    QmxDocument m_document;
    QString m_currentFile;
    bool m_updating = false;
    bool m_basicFieldsDirty = false;
    bool m_architectureChangePending = false;
    QString m_defaultMachine;
    QStringList m_availableMachines;

    QComboBox *m_architecture = nullptr;
    QLabel *m_architectureStatus = nullptr;
    QLineEdit *m_qemuExecutable = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_description = nullptr;
    QComboBox *m_machine = nullptr;
    QComboBox *m_accel = nullptr;
    QComboBox *m_cpu = nullptr;
    QLineEdit *m_cpuProperties = nullptr;
    QComboBox *m_memory = nullptr;
    QComboBox *m_smp = nullptr;
    QComboBox *m_sockets = nullptr;
    QComboBox *m_cores = nullptr;
    QComboBox *m_threads = nullptr;
    QComboBox *m_maxCpus = nullptr;
    QLineEdit *m_smpOther = nullptr;
    QTableWidget *m_assignments = nullptr;
    QLineEdit *m_catalogSearch = nullptr;
    QListWidget *m_catalog = nullptr;
    QPlainTextEdit *m_source = nullptr;
    QPlainTextEdit *m_diagnostics = nullptr;
};
