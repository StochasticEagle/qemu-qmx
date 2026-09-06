/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "mainwindow.h"

#include <cerrno>
#include <csignal>
#include <limits>

#ifdef Q_OS_UNIX
#include <sys/types.h>
#include <unistd.h>
#endif

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTemporaryFile>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

struct ArchitectureDefinition
{
    const char *target;
    const char *label;
};

static const ArchitectureDefinition architectureDefinitions[] = {
    {"x86_64", "x86-64"},
    {"i386", "x86 (32-bit)"},
    {"aarch64", "Arm (64-bit / AArch64)"},
    {"arm", "Arm (32-bit)"},
    {"riscv64", "RISC-V (64-bit)"},
    {"riscv32", "RISC-V (32-bit)"},
    {"ppc64", "PowerPC (64-bit)"},
    {"ppc", "PowerPC (32-bit)"},
    {"mips64", "MIPS64 (big-endian)"},
    {"mips64el", "MIPS64 (little-endian)"},
    {"mips", "MIPS (32-bit, big-endian)"},
    {"mipsel", "MIPS (32-bit, little-endian)"},
    {"s390x", "IBM Z (s390x)"},
    {"loongarch64", "LoongArch (64-bit)"},
    {"sparc64", "SPARC (64-bit)"},
    {"sparc", "SPARC (32-bit)"},
    {"sh4", "SuperH SH-4 (little-endian)"},
    {"sh4eb", "SuperH SH-4 (big-endian)"},
    {"m68k", "Motorola 68k / ColdFire"},
    {"microblaze", "MicroBlaze (big-endian)"},
    {"alpha", "DEC Alpha"},
    {"hppa", "HP PA-RISC"},
    {"xtensa", "Xtensa (little-endian)"},
    {"xtensaeb", "Xtensa (big-endian)"},
    {"avr", "AVR"},
    {"or1k", "OpenRISC"},
    {"hexagon", "Qualcomm Hexagon"},
    {"tricore", "Infineon TriCore"},
    {"rx", "Renesas RX"},
};

static constexpr int TargetRole = Qt::UserRole;
static constexpr int ExecutableRole = Qt::UserRole + 1;

static void configureSelector(QComboBox *selector)
{
    selector->setEditable(true);
    selector->setInsertPolicy(QComboBox::NoInsert);
    selector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    selector->view()->setTextElideMode(Qt::ElideNone);
}

static void resizeSelectorPopup(QComboBox *selector)
{
    const QFontMetrics metrics(selector->view()->font());
    int width = selector->width();
    for (int index = 0; index < selector->count(); ++index) {
        width = qMax(width, metrics.horizontalAdvance(selector->itemText(index)) + 32);
    }
    if (QScreen *screen = selector->screen()) {
        width = qMin(width, screen->availableGeometry().width() - 40);
    }
    selector->view()->setMinimumWidth(width);
}

static QStringList splitSuboptions(const QString &value)
{
    QStringList parts;
    QString part;
    bool quoted = false;
    bool escaped = false;
    for (const QChar character : value) {
        if (escaped) {
            part.append(character);
            escaped = false;
            continue;
        }
        if (character == QLatin1Char('\\')) {
            part.append(character);
            escaped = true;
            continue;
        }
        if (character == QLatin1Char('"')) {
            quoted = !quoted;
            part.append(character);
            continue;
        }
        if (character == QLatin1Char(',') && !quoted) {
            parts.append(part.trimmed());
            part.clear();
            continue;
        }
        part.append(character);
    }
    if (!part.trimmed().isEmpty()) {
        parts.append(part.trimmed());
    }
    return parts;
}

static QString withoutLeadingCommas(QString value)
{
    value = value.trimmed();
    while (value.startsWith(QLatin1Char(','))) {
        value.remove(0, 1);
        value = value.trimmed();
    }
    return value;
}

static void addCpuCountChoices(QComboBox *selector)
{
    configureSelector(selector);
    selector->lineEdit()->setPlaceholderText(QStringLiteral("Automatic"));
    selector->addItems({QString(), QStringLiteral("1"), QStringLiteral("2"),
                        QStringLiteral("4"), QStringLiteral("8"),
                        QStringLiteral("16"), QStringLiteral("32"),
                        QStringLiteral("64"), QStringLiteral("128")});
    resizeSelectorPopup(selector);
}

static bool processExists(qint64 processId)
{
#ifdef Q_OS_UNIX
    return ::kill(static_cast<pid_t>(processId), 0) == 0 || errno == EPERM;
#else
    Q_UNUSED(processId);
    return true;
#endif
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    newDocument();
    QTimer::singleShot(0, this, &MainWindow::discoverQemu);
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("QEMU QMX Builder"));
    resize(1200, 760);

    QToolBar *toolbar = addToolBar(QStringLiteral("File and VM"));
    toolbar->addAction(QStringLiteral("New"), this, &MainWindow::newDocument);
    toolbar->addAction(QStringLiteral("Open"), this, &MainWindow::openDocument);
    toolbar->addAction(QStringLiteral("Save"), this, &MainWindow::saveDocument);
    toolbar->addSeparator();
    toolbar->addAction(QStringLiteral("Create Disk"), this,
                       &MainWindow::createDiskImage);
    toolbar->addSeparator();
    toolbar->addAction(QStringLiteral("Validate"), this, &MainWindow::validateDocument);
    toolbar->addAction(QStringLiteral("Launch"), this, &MainWindow::launchDocument);

    QWidget *central = new QWidget(this);
    QVBoxLayout *outer = new QVBoxLayout(central);
    QHBoxLayout *qemuRow = new QHBoxLayout;
    m_qemuExecutable = new QLineEdit(QStringLiteral("qemu-system-x86_64"));
    QHBoxLayout *architectureRow = new QHBoxLayout;
    m_architecture = new QComboBox;
    m_architecture->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_architecture->view()->setTextElideMode(Qt::ElideNone);
    m_architectureStatus = new QLabel;
    architectureRow->addWidget(new QLabel(QStringLiteral("Architecture:")));
    architectureRow->addWidget(m_architecture, 1);
    architectureRow->addWidget(m_architectureStatus);
    outer->addLayout(architectureRow);
    populateArchitectures();
    connect(m_architecture, &QComboBox::currentIndexChanged,
            this, &MainWindow::architectureChanged);
    QPushButton *browseQemu = new QPushButton(QStringLiteral("Browse…"));
    QPushButton *discover = new QPushButton(QStringLiteral("Discover"));
    qemuRow->addWidget(new QLabel(QStringLiteral("QEMU executable:")));
    qemuRow->addWidget(m_qemuExecutable, 1);
    qemuRow->addWidget(browseQemu);
    qemuRow->addWidget(discover);
    outer->addLayout(qemuRow);

    connect(browseQemu, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this,
            QStringLiteral("Select QEMU system emulator"));
        if (!path.isEmpty()) {
            m_qemuExecutable->setText(path);
            populateArchitectures();
            QTimer::singleShot(0, this, &MainWindow::discoverQemu);
        }
    });
    connect(discover, &QPushButton::clicked, this, &MainWindow::discoverQemu);

    QSplitter *splitter = new QSplitter;
    QTabWidget *forms = new QTabWidget;

    QWidget *basicPage = new QWidget;
    QFormLayout *basic = new QFormLayout(basicPage);
    basic->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_name = new QLineEdit;
    m_description = new QLineEdit;
    m_machine = new QComboBox;
    configureSelector(m_machine);
    m_accel = new QComboBox;
    configureSelector(m_accel);
    m_cpu = new QComboBox;
    configureSelector(m_cpu);
    m_cpuProperties = new QLineEdit;
    m_cpuProperties->setPlaceholderText(
        QStringLiteral("Example: hv_relaxed,hv_spinlocks=0x1fff,hv_vapic,hv_time"));
    m_memory = new QComboBox;
    configureSelector(m_memory);
    m_memory->addItems({QStringLiteral("512M"), QStringLiteral("1G"),
                        QStringLiteral("2G"), QStringLiteral("4G"),
                        QStringLiteral("8G"), QStringLiteral("16G"),
                        QStringLiteral("32G"), QStringLiteral("64G")});
    m_smp = new QComboBox;
    configureSelector(m_smp);
    m_smp->addItems({QStringLiteral("1"), QStringLiteral("2"),
                     QStringLiteral("4"), QStringLiteral("8"),
                     QStringLiteral("16"), QStringLiteral("32")});
    m_sockets = new QComboBox;
    addCpuCountChoices(m_sockets);
    m_cores = new QComboBox;
    addCpuCountChoices(m_cores);
    m_threads = new QComboBox;
    addCpuCountChoices(m_threads);
    m_maxCpus = new QComboBox;
    addCpuCountChoices(m_maxCpus);
    m_smpOther = new QLineEdit;
    m_smpOther->setPlaceholderText(
        QStringLiteral("Additional properties, such as dies=2 or clusters=4"));
    resizeSelectorPopup(m_memory);
    resizeSelectorPopup(m_smp);
    basic->addRow(QStringLiteral("Name"), m_name);
    basic->addRow(QStringLiteral("Description"), m_description);
    basic->addRow(QStringLiteral("Machine type"), m_machine);
    basic->addRow(QStringLiteral("Acceleration"), m_accel);
    basic->addRow(QStringLiteral("CPU model"), m_cpu);
    basic->addRow(QStringLiteral("CPU properties"), m_cpuProperties);
    basic->addRow(QStringLiteral("Memory size"), m_memory);
    m_smp->setToolTip(QStringLiteral(
        "Total logical processors initially online; this is not the CPU package count."));
    m_sockets->setToolTip(QStringLiteral(
        "Number of discrete CPU packages exposed to the guest."));
    m_maxCpus->setToolTip(QStringLiteral(
        "Logical processor capacity for hotplug. If empty, QEMU uses the initial count."));
    basic->addRow(QStringLiteral("Initial logical processors"), m_smp);
    basic->addRow(QStringLiteral("CPU packages (sockets)"), m_sockets);
    basic->addRow(QStringLiteral("Cores per CPU"), m_cores);
    basic->addRow(QStringLiteral("Threads per core"), m_threads);
    basic->addRow(QStringLiteral("Maximum logical processors"), m_maxCpus);
    basic->addRow(QStringLiteral("Other processor topology"), m_smpOther);
    QPushButton *applyBasic = new QPushButton(QStringLiteral("Apply Machine Settings"));
    basic->addRow(applyBasic);
    connect(applyBasic, &QPushButton::clicked, this, &MainWindow::applyBasicFields);
    forms->addTab(basicPage, QStringLiteral("Machine"));

    QWidget *assignmentsPage = new QWidget;
    QVBoxLayout *assignmentsLayout = new QVBoxLayout(assignmentsPage);
    m_assignments = new QTableWidget(0, 2);
    m_assignments->setHorizontalHeaderLabels({QStringLiteral("QMX key"),
                                               QStringLiteral("Value")});
    m_assignments->horizontalHeader()->setStretchLastSection(true);
    m_assignments->setSelectionBehavior(QAbstractItemView::SelectRows);
    QHBoxLayout *assignmentButtons = new QHBoxLayout;
    QPushButton *add = new QPushButton(QStringLiteral("Add…"));
    QPushButton *remove = new QPushButton(QStringLiteral("Remove"));
    assignmentButtons->addWidget(add);
    assignmentButtons->addWidget(remove);
    assignmentButtons->addStretch();
    assignmentsLayout->addWidget(m_assignments);
    assignmentsLayout->addLayout(assignmentButtons);
    connect(add, &QPushButton::clicked, this, &MainWindow::addAssignment);
    connect(remove, &QPushButton::clicked, this, &MainWindow::removeAssignment);
    connect(m_assignments, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem *item) {
        if (m_updating || item->column() != 1) {
            return;
        }
        QTableWidgetItem *keyItem = m_assignments->item(item->row(), 0);
        if (!keyItem) {
            return;
        }
        m_document.setValue(keyItem->text(), item->text().trimmed());
        updateSourceFromDocument();
    });
    forms->addTab(assignmentsPage, QStringLiteral("Assignments"));

    QWidget *catalogPage = new QWidget;
    QVBoxLayout *catalogLayout = new QVBoxLayout(catalogPage);
    catalogLayout->addWidget(new QLabel(QStringLiteral(
        "Options discovered from the selected QEMU executable:")));
    m_catalogSearch = new QLineEdit;
    m_catalogSearch->setPlaceholderText(QStringLiteral("Filter QEMU options"));
    m_catalog = new QListWidget;
    QPushButton *addCatalog = new QPushButton(QStringLiteral("Add selected option…"));
    catalogLayout->addWidget(m_catalogSearch);
    catalogLayout->addWidget(m_catalog);
    catalogLayout->addWidget(addCatalog);
    connect(addCatalog, &QPushButton::clicked, this, &MainWindow::addCatalogOption);
    connect(m_catalog, &QListWidget::itemDoubleClicked, this,
            [this] { addCatalogOption(); });
    connect(m_catalogSearch, &QLineEdit::textChanged, this,
            [this](const QString &text) {
        for (int row = 0; row < m_catalog->count(); ++row) {
            QListWidgetItem *item = m_catalog->item(row);
            item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
        }
    });
    forms->addTab(catalogPage, QStringLiteral("QEMU Options"));

    QWidget *right = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(new QLabel(QStringLiteral("QMX source")));
    m_source = new QPlainTextEdit;
    m_source->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont fixed = QFont(QStringLiteral("monospace"));
    fixed.setStyleHint(QFont::Monospace);
    m_source->setFont(fixed);
    rightLayout->addWidget(m_source, 3);
    rightLayout->addWidget(new QLabel(QStringLiteral("Validation and diagnostics")));
    m_diagnostics = new QPlainTextEdit;
    m_diagnostics->setReadOnly(true);
    m_diagnostics->setMaximumBlockCount(1000);
    rightLayout->addWidget(m_diagnostics, 1);

    splitter->addWidget(forms);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    outer->addWidget(splitter, 1);
    setCentralWidget(central);

    connect(m_source, &QPlainTextEdit::textChanged, this, &MainWindow::sourceChanged);

    const auto markBasicFieldsDirty = [this] {
        if (!m_updating) {
            m_basicFieldsDirty = true;
        }
    };
    for (QLineEdit *field : {m_name, m_description, m_cpuProperties, m_smpOther}) {
        connect(field, &QLineEdit::textChanged, this, markBasicFieldsDirty);
    }
    for (QComboBox *field : {m_machine, m_accel, m_cpu, m_memory, m_smp,
                             m_sockets, m_cores, m_threads, m_maxCpus}) {
        connect(field, &QComboBox::currentTextChanged,
                this, markBasicFieldsDirty);
    }
}

QString MainWindow::findSystemEmulator(const QString &target) const
{
    QString executableName = QStringLiteral("qemu-system-") + target;
#ifdef Q_OS_WIN
    executableName += QStringLiteral(".exe");
#endif

    const QString currentText = m_qemuExecutable->text().trimmed();
    QString currentPath = QStandardPaths::findExecutable(currentText);
    if (currentPath.isEmpty() && QFileInfo(currentText).isAbsolute() &&
        QFileInfo(currentText).isExecutable()) {
        currentPath = QFileInfo(currentText).absoluteFilePath();
    }
    if (!currentPath.isEmpty() &&
        QFileInfo(currentPath).fileName() == executableName) {
        return currentPath;
    }

    QStringList directories;
    if (!currentPath.isEmpty()) {
        directories.append(QFileInfo(currentPath).absolutePath());
    }
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    directories.append(applicationDirectory.absolutePath());
    directories.append(QDir::cleanPath(applicationDirectory.absoluteFilePath(
        QStringLiteral("../.."))));
    directories.removeDuplicates();

    for (const QString &directory : directories) {
        const QFileInfo candidate(QDir(directory).filePath(executableName));
        if (candidate.isFile() && candidate.isExecutable()) {
            return candidate.absoluteFilePath();
        }
    }
    return QStandardPaths::findExecutable(executableName);
}

void MainWindow::populateArchitectures()
{
    QString preferredTarget = QSettings().value(
        QStringLiteral("architecture"), QStringLiteral("x86_64")).toString();
    const QString currentName = QFileInfo(m_qemuExecutable->text()).fileName();
    if (currentName.startsWith(QStringLiteral("qemu-system-"))) {
        preferredTarget = currentName.mid(QStringLiteral("qemu-system-").size());
        if (preferredTarget.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
            preferredTarget.chop(4);
        }
    }

    m_architecture->blockSignals(true);
    m_architecture->clear();
    int preferredIndex = -1;
    int firstAvailableIndex = -1;
    for (const ArchitectureDefinition &definition : architectureDefinitions) {
        const QString target = QString::fromLatin1(definition.target);
        const QString executable = findSystemEmulator(target);
        const bool available = !executable.isEmpty();
        const QString status = available ? QStringLiteral("installed")
                                         : QStringLiteral("not installed");
        m_architecture->addItem(
            QStringLiteral("%1 (%2) — %3")
                .arg(QString::fromUtf8(definition.label), target, status),
            target);
        const int index = m_architecture->count() - 1;
        m_architecture->setItemData(index, executable, ExecutableRole);
        m_architecture->setItemData(
            index,
            available ? QStringLiteral("Available: %1").arg(executable)
                      : QStringLiteral("Install %1 to use this architecture")
                            .arg(QStringLiteral("qemu-system-") + target),
            Qt::ToolTipRole);
        if (QStandardItemModel *model =
                qobject_cast<QStandardItemModel *>(m_architecture->model())) {
            if (QStandardItem *item = model->item(index)) {
                item->setEnabled(available);
            }
        }
        if (available && firstAvailableIndex < 0) {
            firstAvailableIndex = index;
        }
        if (available && target == preferredTarget) {
            preferredIndex = index;
        }
    }
    const int selectedIndex = preferredIndex >= 0 ? preferredIndex
                                                   : firstAvailableIndex;
    if (selectedIndex >= 0) {
        m_architecture->setCurrentIndex(selectedIndex);
        const QString executable =
            m_architecture->itemData(selectedIndex, ExecutableRole).toString();
        m_qemuExecutable->setText(executable);
        m_architectureStatus->setText(QStringLiteral("Ready"));
        QSettings().setValue(QStringLiteral("architecture"),
                             m_architecture->currentData(TargetRole));
    } else {
        m_architectureStatus->setText(QStringLiteral("No system emulator installed"));
    }
    m_architecture->blockSignals(false);
    resizeSelectorPopup(m_architecture);
}

void MainWindow::selectArchitectureForExecutable(const QString &executable)
{
    QString target = QFileInfo(executable).fileName();
    if (!target.startsWith(QStringLiteral("qemu-system-"))) {
        return;
    }
    target.remove(0, QStringLiteral("qemu-system-").size());
    if (target.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        target.chop(4);
    }
    for (int index = 0; index < m_architecture->count(); ++index) {
        if (m_architecture->itemData(index, TargetRole).toString() == target) {
            QString resolvedExecutable = QStandardPaths::findExecutable(executable);
            if (resolvedExecutable.isEmpty()) {
                resolvedExecutable = QFileInfo(executable).absoluteFilePath();
            }
            m_architecture->blockSignals(true);
            m_architecture->setCurrentIndex(index);
            m_architecture->setItemData(index, resolvedExecutable,
                                        ExecutableRole);
            m_architecture->blockSignals(false);
            m_architectureStatus->setText(QStringLiteral("Ready"));
            QSettings().setValue(QStringLiteral("architecture"), target);
            return;
        }
    }
}

void MainWindow::architectureChanged(int index)
{
    if (index < 0) {
        return;
    }
    const QString executable =
        m_architecture->itemData(index, ExecutableRole).toString();
    if (executable.isEmpty()) {
        return;
    }
    m_qemuExecutable->setText(executable);
    m_architectureStatus->setText(QStringLiteral("Discovering…"));
    QSettings().setValue(QStringLiteral("architecture"),
                         m_architecture->itemData(index, TargetRole));
    m_architectureChangePending = true;
    m_machine->clear();
    m_cpu->clear();
    m_cpuProperties->clear();
    m_catalog->clear();
    m_catalog->addItem(QStringLiteral("Discovering QEMU capabilities…"));
    m_document.remove(QStringLiteral("cpu"));
    m_document.remove(QStringLiteral("accel"));
    updateSourceFromDocument();
    QTimer::singleShot(0, this, &MainWindow::discoverQemu);
}

void MainWindow::newDocument()
{
    const QString machine = m_defaultMachine.isEmpty()
        ? QStringLiteral("q35") : m_defaultMachine;
    m_document.parse(QStringLiteral(
        "qmx = 1\n"
        "name = \"New virtual machine\"\n"
        "machine = %1\n"
        "memory = 4G\n").arg(machine));
    setCurrentFile({});
    loadFromDocument();
}

void MainWindow::openDocument()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open QMX configuration"), {},
        QStringLiteral("QMX configurations (*.qmx);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    openFile(path);
}

bool MainWindow::openFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QStringLiteral("Open failed"), file.errorString());
        return false;
    }
    QStringList errors;
    m_document.parse(QString::fromUtf8(file.readAll()), &errors);
    setCurrentFile(path);
    loadFromDocument();
    m_diagnostics->setPlainText(errors.join(QLatin1Char('\n')));
    return true;
}

bool MainWindow::saveDocument()
{
    if (!applyBasicFields()) {
        return false;
    }
    if (m_currentFile.isEmpty()) {
        return saveDocumentAs();
    }
    return writeFile(m_currentFile);
}

bool MainWindow::saveDocumentAs()
{
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save QMX configuration"), m_currentFile,
        QStringLiteral("QMX configurations (*.qmx)"));
    if (path.isEmpty()) {
        return false;
    }
    if (!path.endsWith(QStringLiteral(".qmx"), Qt::CaseInsensitive)) {
        path.append(QStringLiteral(".qmx"));
    }
    if (!writeFile(path)) {
        return false;
    }
    setCurrentFile(path);
    return true;
}

bool MainWindow::writeFile(const QString &path)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QStringLiteral("Save failed"), file.errorString());
        return false;
    }
    file.write(m_source->toPlainText().toUtf8());
    if (!file.commit()) {
        QMessageBox::critical(this, QStringLiteral("Save failed"), file.errorString());
        return false;
    }
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(path), 3000);
    return true;
}

void MainWindow::discoverQemu()
{
    selectArchitectureForExecutable(m_qemuExecutable->text());
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QemuCapabilities capabilities;
    QString error;
    QemuDiscovery discovery;
    const bool ok = discovery.discover(m_qemuExecutable->text(), &capabilities, &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        m_catalog->clear();
        QListWidgetItem *item = new QListWidgetItem(
            QStringLiteral("Capability discovery failed: %1").arg(error), m_catalog);
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        m_diagnostics->setPlainText(
            QStringLiteral("QEMU capability discovery failed: %1").arg(error));
        statusBar()->showMessage(QStringLiteral("QEMU discovery failed"), 5000);
        return;
    }

    QString selectedMachine = m_architectureChangePending
        ? QString() : m_document.value(QStringLiteral("machine"));
    const QString selectedAccelerator = m_architectureChangePending
        ? QStringLiteral("Default") : m_accel->currentText();
    const QStringList configuredCpu = splitSuboptions(
        m_document.value(QStringLiteral("cpu")));
    const QString selectedCpu = m_architectureChangePending || configuredCpu.isEmpty()
        ? QString() : configuredCpu.constFirst();
    m_availableMachines = capabilities.machines;
    m_defaultMachine = capabilities.defaultMachine;
    if (!m_availableMachines.contains(selectedMachine)) {
        selectedMachine = m_defaultMachine;
    }
    m_machine->clear();
    m_machine->addItems(capabilities.machines);
    m_accel->clear();
    m_accel->addItem(QStringLiteral("Default"));
    m_accel->addItems(capabilities.accelerators);
    m_cpu->clear();
    m_cpu->addItems(capabilities.cpus);
    m_catalog->clear();
    for (const QemuOption &option : capabilities.options) {
        if (option.name.trimmed().isEmpty()) {
            continue;
        }
        QString description = option.name;
        QStringList parameters;
        for (const QemuOptionParameter &parameter : option.parameters) {
            parameters.append(QStringLiteral("%1:%2").arg(parameter.name, parameter.type));
        }
        if (!parameters.isEmpty()) {
            description += QStringLiteral(" — ") + parameters.join(QStringLiteral(", "));
        }
        QListWidgetItem *item = new QListWidgetItem(description, m_catalog);
        item->setData(Qt::UserRole, option.name);
        item->setData(Qt::UserRole + 1, option.argumentless);
    }
    m_catalog->sortItems(Qt::AscendingOrder);
    m_machine->setCurrentText(selectedMachine);
    m_accel->setCurrentText(selectedAccelerator.isEmpty()
                                ? QStringLiteral("Default") : selectedAccelerator);
    m_cpu->setCurrentText(selectedCpu);
    resizeSelectorPopup(m_machine);
    resizeSelectorPopup(m_accel);
    resizeSelectorPopup(m_cpu);
    m_architectureStatus->setText(QStringLiteral("Ready"));
    if (m_architectureChangePending) {
        m_document.setValue(QStringLiteral("machine"), selectedMachine);
        m_document.remove(QStringLiteral("cpu"));
        m_architectureChangePending = false;
        updateSourceFromDocument();
    }
    m_basicFieldsDirty = false;
    statusBar()->showMessage(
        QStringLiteral("Discovered %1 options, %2 machines, %3 accelerators, and %4 CPU definitions")
            .arg(capabilities.options.size())
            .arg(capabilities.machines.size())
            .arg(capabilities.accelerators.size())
            .arg(capabilities.cpus.size()), 5000);
}

QString MainWindow::validationFile(QString *temporaryPath)
{
    if (!m_currentFile.isEmpty() && saveDocument()) {
        return m_currentFile;
    }
    if (!m_currentFile.isEmpty() || !applyBasicFields()) {
        return {};
    }

    QTemporaryFile temporary(QDir::tempPath()
                                + QStringLiteral("/qmx-builder-XXXXXX.qmx"));
    temporary.setAutoRemove(false);
    if (!temporary.open()) {
        return {};
    }
    temporary.write(m_source->toPlainText().toUtf8());
    temporary.close();
    *temporaryPath = temporary.fileName();
    return *temporaryPath;
}

void MainWindow::validateDocument()
{
    QString temporary;
    const QString path = validationFile(&temporary);
    if (path.isEmpty()) {
        return;
    }
    QString output;
    const bool ok = QemuDiscovery::validate(m_qemuExecutable->text(), path, &output);
    if (!temporary.isEmpty()) {
        QFile::remove(temporary);
    }
    m_diagnostics->setPlainText(output);
    statusBar()->showMessage(ok ? QStringLiteral("QMX configuration is valid")
                                : QStringLiteral("QMX validation failed"), 5000);
}

void MainWindow::launchDocument()
{
    if (!saveDocument()) {
        return;
    }

    const QFileInfo qmxInfo(m_currentFile);
    const QString qmxPath = qmxInfo.absoluteFilePath();
    QString validationOutput;
    if (!QemuDiscovery::validate(m_qemuExecutable->text(), qmxPath,
                                 &validationOutput)) {
        m_diagnostics->setPlainText(validationOutput);
        QMessageBox::critical(
            this, QStringLiteral("Launch blocked by validation"),
            validationOutput.isEmpty()
                ? QStringLiteral("QEMU rejected the QMX configuration.")
                : validationOutput);
        return;
    }

    QTemporaryFile launchLog(QDir::tempPath()
                             + QStringLiteral("/qmx-builder-XXXXXX.log"));
    launchLog.setAutoRemove(false);
    if (!launchLog.open()) {
        QMessageBox::critical(this, QStringLiteral("Launch failed"),
                              QStringLiteral("Could not create a QEMU startup log."));
        return;
    }
    const QString logPath = launchLog.fileName();
    launchLog.close();

    QProcess process;
    process.setProgram(m_qemuExecutable->text());
    process.setArguments({qmxPath});
    process.setWorkingDirectory(qmxInfo.absolutePath());
    process.setStandardErrorFile(logPath, QIODevice::Truncate);
    qint64 processId = 0;
    if (!process.startDetached(&processId)) {
        QFile::remove(logPath);
        QMessageBox::critical(
            this, QStringLiteral("Launch failed"),
            QStringLiteral("Could not start the selected QEMU executable."));
        return;
    }
    statusBar()->showMessage(
        QStringLiteral("Starting VM as process %1…").arg(processId));
    QTimer::singleShot(6500, this, [this, processId, logPath] {
        QFile log(logPath);
        QString output;
        if (log.open(QIODevice::ReadOnly | QIODevice::Text)) {
            output = QString::fromUtf8(log.readAll()).trimmed();
        }
        QFile::remove(logPath);
        if (!processExists(processId)) {
            if (output.isEmpty()) {
                output = QStringLiteral(
                    "QEMU exited during startup without reporting an error.");
            }
            m_diagnostics->setPlainText(output);
            QMessageBox::critical(this, QStringLiteral("QEMU failed to start"),
                                  output);
            statusBar()->showMessage(QStringLiteral("VM launch failed"), 5000);
            return;
        }
        m_diagnostics->clear();
        statusBar()->showMessage(
            QStringLiteral("VM is running as process %1").arg(processId), 5000);
    });
}

void MainWindow::createDiskImage()
{
    const QFileInfo qemuInfo(m_qemuExecutable->text());
    QString qemuImg;

    if (qemuInfo.isAbsolute()) {
        const QString sibling = qemuInfo.dir().filePath(QStringLiteral("qemu-img"));
        if (QFileInfo::exists(sibling)) {
            qemuImg = sibling;
        }
    }
    if (qemuImg.isEmpty()) {
        qemuImg = QStandardPaths::findExecutable(QStringLiteral("qemu-img"));
    }
    if (qemuImg.isEmpty()) {
        QMessageBox::critical(
            this, QStringLiteral("qemu-img not found"),
            QStringLiteral("Install qemu-img or select a QEMU executable whose "
                           "installation also contains qemu-img."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Create Disk Image"));
    QFormLayout *form = new QFormLayout(&dialog);
    QLineEdit *pathEdit = new QLineEdit;
    QPushButton *browse = new QPushButton(QStringLiteral("Browse…"));
    QHBoxLayout *pathRow = new QHBoxLayout;
    pathRow->addWidget(pathEdit, 1);
    pathRow->addWidget(browse);

    QComboBox *format = new QComboBox;
    format->setEditable(true);
    format->addItems({QStringLiteral("qcow2"), QStringLiteral("raw"),
                      QStringLiteral("vmdk"), QStringLiteral("vdi")});
    QLineEdit *size = new QLineEdit(QStringLiteral("64G"));
    QLineEdit *driveId = new QLineEdit(QStringLiteral("disk0"));
    QComboBox *interface = new QComboBox;
    interface->addItems({QStringLiteral("none"), QStringLiteral("virtio"),
                         QStringLiteral("ide"), QStringLiteral("scsi")});
    QCheckBox *addToDocument = new QCheckBox(
        QStringLiteral("Add the new image as a QMX drive"));
    addToDocument->setChecked(true);

    form->addRow(QStringLiteral("Image file"), pathRow);
    form->addRow(QStringLiteral("Format"), format);
    form->addRow(QStringLiteral("Virtual size"), size);
    form->addRow(QStringLiteral("Drive ID"), driveId);
    form->addRow(QStringLiteral("Interface"), interface);
    form->addRow(addToDocument);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(browse, &QPushButton::clicked, &dialog, [this, pathEdit, format] {
        const QString base = m_currentFile.isEmpty()
            ? QDir::homePath() : QFileInfo(m_currentFile).absolutePath();
        const QString selected = QFileDialog::getSaveFileName(
            this, QStringLiteral("Create Disk Image"), base,
            QStringLiteral("Disk images (*.%1);;All files (*)")
                .arg(format->currentText()));
        if (!selected.isEmpty()) {
            pathEdit->setText(selected);
        }
    });

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString path = QDir::cleanPath(pathEdit->text().trimmed());
    const QString imageFormat = format->currentText().trimmed();
    const QString imageSize = size->text().trimmed();
    const QString id = driveId->text().trimmed();
    static const QRegularExpression idPattern(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_-]*$"));
    if (path.isEmpty() || imageFormat.isEmpty() || imageSize.isEmpty() ||
        (addToDocument->isChecked() && !idPattern.match(id).hasMatch())) {
        QMessageBox::critical(this, QStringLiteral("Invalid disk definition"),
                              QStringLiteral("Enter a file, format, size, and a valid "
                                             "QMX drive ID."));
        return;
    }

    QProcess process;
    process.start(qemuImg, {QStringLiteral("create"), QStringLiteral("-f"),
                            imageFormat, path, imageSize});
    if (!process.waitForStarted(5000) || !process.waitForFinished(300000) ||
        process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString output = QString::fromUtf8(process.readAllStandardError()).trimmed();
        QMessageBox::critical(this, QStringLiteral("Disk creation failed"),
                              output.isEmpty() ? process.errorString() : output);
        return;
    }

    if (addToDocument->isChecked()) {
        QString qmxPath = QFileInfo(path).absoluteFilePath();
        if (!m_currentFile.isEmpty()) {
            const QDir qmxDir(QFileInfo(m_currentFile).absolutePath());
            const QString relative = qmxDir.relativeFilePath(qmxPath);
            if (relative != QStringLiteral("..") &&
                !relative.startsWith(QStringLiteral("../"))) {
                qmxPath = QStringLiteral("./") + relative;
            }
        }
        const QString value = QStringLiteral("file=%1,format=%2,if=%3")
            .arg(quoted(qmxPath), imageFormat, interface->currentText());
        m_document.setValue(QStringLiteral("drive.%1").arg(id), value);
        updateSourceFromDocument();
    }

    statusBar()->showMessage(QStringLiteral("Created %1").arg(path), 5000);
}

void MainWindow::sourceChanged()
{
    if (m_updating) {
        return;
    }
    QStringList errors;
    m_document.parse(m_source->toPlainText(), &errors);
    m_basicFieldsDirty = false;
    syncBasicFieldsFromDocument();
    m_diagnostics->setPlainText(errors.join(QLatin1Char('\n')));
    updateAssignments();
}

QString MainWindow::quoted(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

bool MainWindow::applyBasicFields()
{
    QString cpu = m_cpu->currentText().trimmed();
    const QString cpuProperties = withoutLeadingCommas(m_cpuProperties->text());
    if (!cpu.isEmpty() && !cpuProperties.isEmpty()) {
        cpu += QLatin1Char(',');
        cpu += cpuProperties;
    }

    QStringList smpParts;
    if (!m_smp->currentText().trimmed().isEmpty()) {
        smpParts.append(m_smp->currentText().trimmed());
    }
    const QList<QPair<QString, QComboBox *>> topology = {
        {QStringLiteral("sockets"), m_sockets},
        {QStringLiteral("cores"), m_cores},
        {QStringLiteral("threads"), m_threads},
        {QStringLiteral("maxcpus"), m_maxCpus},
    };
    for (const auto &[name, selector] : topology) {
        const QString value = selector->currentText().trimmed();
        if (!value.isEmpty()) {
            smpParts.append(QStringLiteral("%1=%2").arg(name, value));
        }
    }
    const QString otherTopology = withoutLeadingCommas(m_smpOther->text());
    if (!otherTopology.isEmpty()) {
        smpParts.append(otherTopology);
    }

    qulonglong explicitTopologyProduct = 1;
    bool topologySpecified = false;
    bool topologyValuesValid = true;
    const auto includeTopologyValue = [&](const QString &text) {
        if (text.isEmpty() || !topologyValuesValid) {
            return;
        }
        bool valid = false;
        const qulonglong value = text.toULongLong(&valid);
        if (!valid || value == 0 ||
            explicitTopologyProduct > std::numeric_limits<qulonglong>::max() / value) {
            topologyValuesValid = false;
            return;
        }
        topologySpecified = true;
        explicitTopologyProduct *= value;
    };
    includeTopologyValue(m_sockets->currentText().trimmed());
    includeTopologyValue(m_cores->currentText().trimmed());
    includeTopologyValue(m_threads->currentText().trimmed());
    for (const QString &part : splitSuboptions(otherTopology)) {
        const qsizetype equals = part.indexOf(QLatin1Char('='));
        if (equals < 0) {
            continue;
        }
        const QString name = part.left(equals).trimmed();
        if (name == QStringLiteral("drawers") ||
            name == QStringLiteral("books") ||
            name == QStringLiteral("dies") ||
            name == QStringLiteral("clusters") ||
            name == QStringLiteral("modules")) {
            includeTopologyValue(part.mid(equals + 1).trimmed());
        }
    }

    bool initialValid = false;
    bool maximumValid = false;
    const QString initialText = m_smp->currentText().trimmed();
    const QString maximumText = m_maxCpus->currentText().trimmed();
    const qulonglong initialCpus = initialText.toULongLong(&initialValid);
    const qulonglong maximumCpus = maximumText.isEmpty()
        ? initialCpus : maximumText.toULongLong(&maximumValid);
    if (maximumText.isEmpty()) {
        maximumValid = initialValid;
    }
    if (!topologyValuesValid ||
        (!initialText.isEmpty() && (!initialValid || initialCpus == 0)) ||
        (!maximumText.isEmpty() && (!maximumValid || maximumCpus == 0))) {
        QMessageBox::critical(
            this, QStringLiteral("Invalid processor topology"),
            QStringLiteral("Processor counts and topology values must be positive integers."));
        return false;
    }
    if (initialValid && maximumValid && initialCpus > maximumCpus) {
        QMessageBox::critical(
            this, QStringLiteral("Invalid processor topology"),
            QStringLiteral("Initial logical processors cannot exceed Maximum logical processors."));
        return false;
    }
    const bool completeBasicTopology =
        !m_sockets->currentText().trimmed().isEmpty() &&
        !m_cores->currentText().trimmed().isEmpty() &&
        !m_threads->currentText().trimmed().isEmpty();
    const bool topologyMismatch = topologySpecified && maximumValid &&
        (completeBasicTopology
             ? maximumCpus != explicitTopologyProduct
             : maximumCpus % explicitTopologyProduct != 0);
    if (topologyMismatch) {
        const qulonglong recommended = completeBasicTopology
            ? explicitTopologyProduct
            : ((maximumCpus / explicitTopologyProduct) + 1) *
                  explicitTopologyProduct;
        const QString relationship = completeBasicTopology
            ? QStringLiteral("equal %1").arg(explicitTopologyProduct)
            : QStringLiteral("be a multiple of %1").arg(explicitTopologyProduct);
        const QString maximumExplanation = maximumText.isEmpty()
            ? QStringLiteral("Maximum logical processors is empty, so QEMU uses "
                             "the Initial logical processors value of %1.")
                  .arg(maximumCpus)
            : QStringLiteral("Maximum logical processors is currently %1.")
                  .arg(maximumCpus);
        QMessageBox::critical(
            this, QStringLiteral("Invalid processor topology"),
            QStringLiteral(
                "The configured topology requires Maximum logical processors to %1. "
                "%2 For this topology, set Initial logical processors to %3, or "
                "explicitly configure a compatible Maximum logical "
                "processors value.")
                .arg(relationship)
                .arg(maximumExplanation)
                .arg(recommended));
        return false;
    }

    const QList<QPair<QString, QString>> fields = {
        {QStringLiteral("name"), quoted(m_name->text())},
        {QStringLiteral("description"), quoted(m_description->text())},
        {QStringLiteral("machine"), m_machine->currentText().trimmed()},
        {QStringLiteral("accel"), m_accel->currentText() == QStringLiteral("Default")
                                      ? QString()
                                      : m_accel->currentText().trimmed()},
        {QStringLiteral("cpu"), cpu},
        {QStringLiteral("memory"), m_memory->currentText().trimmed()},
        {QStringLiteral("smp"), smpParts.join(QLatin1Char(','))},
    };
    for (const auto &[key, value] : fields) {
        if (value.isEmpty() || ((key == QStringLiteral("name") ||
                                 key == QStringLiteral("description")) &&
                                value == QStringLiteral("\"\""))) {
            m_document.remove(key);
        } else {
            m_document.setValue(key, value);
        }
    }
    updateSourceFromDocument();
    m_basicFieldsDirty = false;
    return true;
}

void MainWindow::addAssignment()
{
    bool ok = false;
    const QString key = QInputDialog::getText(this, QStringLiteral("Add assignment"),
                                               QStringLiteral("QMX key:"),
                                               QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || key.isEmpty()) {
        return;
    }
    const QString value = QInputDialog::getText(this, QStringLiteral("Add assignment"),
                                                 QStringLiteral("Value:"),
                                                 QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || value.isEmpty()) {
        return;
    }
    m_document.setValue(key, value);
    updateSourceFromDocument();
}

void MainWindow::removeAssignment()
{
    const int row = m_assignments->currentRow();
    if (row < 0) {
        return;
    }
    m_document.remove(m_assignments->item(row, 0)->text());
    updateSourceFromDocument();
}

void MainWindow::addCatalogOption()
{
    QListWidgetItem *item = m_catalog->currentItem();
    if (!item) {
        return;
    }
    const QString option = item->data(Qt::UserRole).toString();
    const bool argumentless = item->data(Qt::UserRole + 1).toBool();
    bool ok = false;
    QString value = QStringLiteral("on");
    if (!argumentless) {
        value = QInputDialog::getText(
            this, QStringLiteral("Add QEMU option"),
            QStringLiteral("Value for %1:").arg(option), QLineEdit::Normal,
            {}, &ok).trimmed();
        if (!ok || value.isEmpty()) {
            return;
        }
    }
    QString key = option == QStringLiteral("m") ? QStringLiteral("memory") : option;
    if (m_document.contains(key)) {
        const QString occurrence = QInputDialog::getText(
            this, QStringLiteral("Occurrence name"),
            QStringLiteral("This option already exists. Enter an occurrence name:"),
            QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || occurrence.isEmpty()) {
            return;
        }
        key += QLatin1Char('.') + occurrence;
    }
    m_document.setValue(key, value);
    updateSourceFromDocument();
}

void MainWindow::syncBasicFieldsFromDocument()
{
    m_updating = true;
    m_name->setText(m_document.value(QStringLiteral("name")).remove(QLatin1Char('"')));
    m_description->setText(
        m_document.value(QStringLiteral("description")).remove(QLatin1Char('"')));
    m_machine->setCurrentText(m_document.value(QStringLiteral("machine")));
    const QString accelerator = m_document.value(QStringLiteral("accel"));
    m_accel->setCurrentText(accelerator.isEmpty() ? QStringLiteral("Default")
                                                   : accelerator);
    QStringList cpuParts = splitSuboptions(m_document.value(QStringLiteral("cpu")));
    m_cpu->setCurrentText(cpuParts.isEmpty() ? QString() : cpuParts.takeFirst());
    m_cpuProperties->setText(cpuParts.join(QLatin1Char(',')));
    m_memory->setCurrentText(m_document.value(QStringLiteral("memory")));

    m_smp->setCurrentText(QString());
    m_sockets->setCurrentText(QString());
    m_cores->setCurrentText(QString());
    m_threads->setCurrentText(QString());
    m_maxCpus->setCurrentText(QString());
    QStringList otherTopology;
    for (const QString &part : splitSuboptions(
             m_document.value(QStringLiteral("smp")))) {
        const qsizetype equals = part.indexOf(QLatin1Char('='));
        const QString name = equals < 0 ? QString() : part.left(equals).trimmed();
        const QString value = equals < 0 ? part.trimmed()
                                         : part.mid(equals + 1).trimmed();
        if ((name.isEmpty() || name == QStringLiteral("cpus")) &&
            m_smp->currentText().isEmpty()) {
            m_smp->setCurrentText(value);
        } else if (name == QStringLiteral("sockets")) {
            m_sockets->setCurrentText(value);
        } else if (name == QStringLiteral("cores")) {
            m_cores->setCurrentText(value);
        } else if (name == QStringLiteral("threads")) {
            m_threads->setCurrentText(value);
        } else if (name == QStringLiteral("maxcpus")) {
            m_maxCpus->setCurrentText(value);
        } else {
            otherTopology.append(part);
        }
    }
    m_smpOther->setText(otherTopology.join(QLatin1Char(',')));
    m_updating = false;
    m_basicFieldsDirty = false;
}

void MainWindow::loadFromDocument()
{
    syncBasicFieldsFromDocument();
    updateSourceFromDocument();
}

void MainWindow::updateSourceFromDocument()
{
    m_updating = true;
    m_source->setPlainText(m_document.serialize());
    m_updating = false;
    updateAssignments();
}

void MainWindow::updateAssignments()
{
    m_updating = true;
    m_assignments->setRowCount(0);
    for (const QmxDocument::Line &line : m_document.lines()) {
        if (line.type != QmxDocument::LineType::Assignment) {
            continue;
        }
        const int row = m_assignments->rowCount();
        m_assignments->insertRow(row);
        QTableWidgetItem *keyItem = new QTableWidgetItem(line.key);
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
        m_assignments->setItem(row, 0, keyItem);
        m_assignments->setItem(row, 1, new QTableWidgetItem(line.value));
    }
    m_updating = false;
}

void MainWindow::setCurrentFile(const QString &path)
{
    m_currentFile = path;
    setWindowFilePath(path);
    setWindowTitle(path.isEmpty() ? QStringLiteral("QEMU QMX Builder — Untitled")
                                  : QStringLiteral("QEMU QMX Builder — %1")
                                        .arg(QFileInfo(path).fileName()));
}
