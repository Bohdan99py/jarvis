// ============================================================
// task_manager_dialog.cpp — Cyberpunk Kanban Task Manager
// ============================================================

#include "task_manager_dialog.h"
#include <QCheckBox>
#include <QMessageBox>
#include <QDateTime>
#include <QDebug>

static const char* kDialogCss = R"(
    QDialog {
        background-color: #0B0C10;
        color: #c5c6c7;
    }
    QLabel { color: #c5c6c7; }
    QLabel#columnTitle {
        color: #66FCF1;
        font-size: 15px;
        font-weight: bold;
        font-family: "Segoe UI Semibold", "Segoe UI", sans-serif;
        letter-spacing: 2px;
        padding: 6px 0;
    }
    QScrollArea {
        border: 1px solid rgba(102,252,241,0.12);
        border-radius: 8px;
        background: rgba(15,17,22,0.85);
    }
    QPushButton#addBtn {
        background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
            stop:0 #45A29E, stop:1 #66FCF1);
        color: #0B0C10;
        font-weight: bold;
        font-size: 13px;
        border: none;
        border-radius: 6px;
        padding: 8px 24px;
    }
    QPushButton#addBtn:hover {
        background: #66FCF1;
    }
    QPushButton#cardBtn {
        background: rgba(102,252,241,0.06);
        border: 1px solid rgba(102,252,241,0.12);
        border-radius: 6px;
        color: #c5c6c7;
        text-align: left;
        padding: 10px 12px;
        font-family: "Segoe UI", sans-serif;
        font-size: 12px;
    }
    QPushButton#cardBtn:hover {
        background: rgba(102,252,241,0.14);
        border-color: rgba(102,252,241,0.35);
    }
    QLineEdit, QComboBox, QDateTimeEdit {
        background: #1F2833;
        color: #c5c6c7;
        border: 1px solid rgba(102,252,241,0.2);
        border-radius: 4px;
        padding: 6px 10px;
        font-size: 12px;
    }
    QLineEdit:focus, QComboBox:focus, QDateTimeEdit:focus {
        border-color: #66FCF1;
    }
    QComboBox::drop-down { border: none; }
    QComboBox QAbstractItemView {
        background: #1F2833;
        color: #c5c6c7;
        selection-background-color: rgba(102,252,241,0.2);
        border: 1px solid rgba(102,252,241,0.2);
    }
    QPushButton#dlgBtn {
        background: #1F2833;
        color: #66FCF1;
        border: 1px solid rgba(102,252,241,0.2);
        border-radius: 4px;
        padding: 6px 18px;
        font-size: 12px;
    }
    QPushButton#dlgBtn:hover {
        background: rgba(102,252,241,0.12);
    }
    QPushButton#deleteBtn {
        background: rgba(255,82,82,0.15);
        color: #ff5252;
        border: 1px solid rgba(255,82,82,0.3);
        border-radius: 4px;
        padding: 6px 18px;
        font-size: 12px;
    }
    QPushButton#deleteBtn:hover {
        background: rgba(255,82,82,0.3);
    }
    QCheckBox { color: #c5c6c7; }
    QCheckBox::indicator {
        width: 14px; height: 14px;
        border: 1px solid rgba(102,252,241,0.3);
        border-radius: 3px;
        background: #1F2833;
    }
    QCheckBox::indicator:checked {
        background: #66FCF1;
        border-color: #66FCF1;
    }
)";

// ============================================================

TaskManagerDialog::TaskManagerDialog(qint64 userId, QWidget* parent)
    : QDialog(parent), m_userId(userId)
{
    setWindowTitle(QStringLiteral("JARVIS — Task Manager"));
    setMinimumSize(900, 560);
    setStyleSheet(QString::fromUtf8(kDialogCss));
    rebuild();
}

void TaskManagerDialog::rebuild()
{
    // Delete old layout
    if (layout()) {
        QLayoutItem* item;
        while ((item = layout()->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
        delete layout();
    }

    auto& db = DatabaseManager::instance();
    auto allTasks = db.getTasks(m_userId);

    QList<DbTask> todo, inProg, done;
    for (const auto& t : allTasks) {
        if      (t.status == QStringLiteral("Todo"))       todo.append(t);
        else if (t.status == QStringLiteral("InProgress")) inProg.append(t);
        else                                                done.append(t);
    }

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);

    // Header
    auto* header = new QHBoxLayout();
    auto* titleLbl = new QLabel(QStringLiteral("TASK MANAGER"), this);
    titleLbl->setStyleSheet(QStringLiteral(
        "color:#66FCF1; font-size:18px; font-weight:bold; letter-spacing:3px;"));
    header->addWidget(titleLbl);
    header->addStretch();

    auto* addBtn = new QPushButton(QStringLiteral("+ NEW TASK"), this);
    addBtn->setObjectName(QStringLiteral("addBtn"));
    connect(addBtn, &QPushButton::clicked, this, &TaskManagerDialog::showAddDialog);
    header->addWidget(addBtn);
    root->addLayout(header);

    // Separator
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral(
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 transparent, stop:0.2 #45A29E, stop:0.5 #66FCF1,"
        "stop:0.8 #45A29E, stop:1 transparent); max-height:2px;"));
    root->addWidget(sep);

    // Columns
    auto* cols = new QHBoxLayout();
    cols->setSpacing(12);
    cols->addWidget(buildColumn(QStringLiteral("TO DO"),        QStringLiteral("Todo"),       todo));
    cols->addWidget(buildColumn(QStringLiteral("IN PROGRESS"),  QStringLiteral("InProgress"), inProg));
    cols->addWidget(buildColumn(QStringLiteral("DONE"),         QStringLiteral("Done"),       done));
    root->addLayout(cols, 1);

    // Close button
    auto* closeRow = new QHBoxLayout();
    closeRow->addStretch();
    auto* closeBtn = new QPushButton(QStringLiteral("CLOSE"), this);
    closeBtn->setObjectName(QStringLiteral("dlgBtn"));
    closeBtn->setFixedWidth(100);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    closeRow->addWidget(closeBtn);
    closeRow->addStretch();
    root->addLayout(closeRow);
}

QWidget* TaskManagerDialog::buildColumn(const QString& title, const QString& status,
                                         const QList<DbTask>& tasks)
{
    Q_UNUSED(status)
    auto* frame = new QWidget(this);
    auto* lay = new QVBoxLayout(frame);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    auto* lbl = new QLabel(title, frame);
    lbl->setObjectName(QStringLiteral("columnTitle"));
    lbl->setAlignment(Qt::AlignCenter);
    lay->addWidget(lbl);

    auto* countLbl = new QLabel(
        QStringLiteral("<span style='color:#45A29E;font-size:11px;'>%1 task%2</span>")
            .arg(tasks.size()).arg(tasks.size() == 1 ? "" : "s"), frame);
    countLbl->setTextFormat(Qt::RichText);
    countLbl->setAlignment(Qt::AlignCenter);
    lay->addWidget(countLbl);

    auto* scrollArea = new QScrollArea(frame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* scrollContent = new QWidget();
    auto* scrollLay = new QVBoxLayout(scrollContent);
    scrollLay->setContentsMargins(6, 6, 6, 6);
    scrollLay->setSpacing(6);

    for (const auto& t : tasks)
        scrollLay->addWidget(buildCard(t));
    scrollLay->addStretch();

    scrollArea->setWidget(scrollContent);
    lay->addWidget(scrollArea, 1);

    return frame;
}

QWidget* TaskManagerDialog::buildCard(const DbTask& task)
{
    auto* btn = new QPushButton(this);
    btn->setObjectName(QStringLiteral("cardBtn"));
    btn->setCursor(Qt::PointingHandCursor);

    // Priority color
    QString priColor = QStringLiteral("#45A29E");
    if (task.priority == QStringLiteral("High"))   priColor = QStringLiteral("#ff5252");
    if (task.priority == QStringLiteral("Medium")) priColor = QStringLiteral("#FFD740");

    // Deadline text
    QString deadText;
    if (task.deadline.isValid()) {
        qint64 secsTo = QDateTime::currentDateTime().secsTo(task.deadline);
        if (secsTo < 0)
            deadText = QStringLiteral(" | OVERDUE");
        else if (secsTo < 3600)
            deadText = QStringLiteral(" | %1m left").arg(secsTo / 60);
        else if (secsTo < 86400)
            deadText = QStringLiteral(" | %1h left").arg(secsTo / 3600);
        else
            deadText = QStringLiteral(" | ") + task.deadline.toString(QStringLiteral("MMM dd"));
    }

    QString text = QStringLiteral("%1\n[%2] [%3]%4")
        .arg(task.title, task.category, task.priority, deadText);
    btn->setText(text);

    // Overdue styling
    if (task.deadline.isValid() && QDateTime::currentDateTime() > task.deadline
        && task.status != QStringLiteral("Done")) {
        btn->setStyleSheet(
            QStringLiteral("QPushButton#cardBtn { border-color: rgba(255,82,82,0.5); "
                           "background: rgba(255,82,82,0.08); }"));
    }

    qint64 taskId = task.id;
    connect(btn, &QPushButton::clicked, this, [this, taskId]() {
        auto t = DatabaseManager::instance().getTask(taskId);
        if (t) showEditDialog(*t);
    });

    return btn;
}

void TaskManagerDialog::showAddDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("New Task"));
    dlg.setFixedWidth(400);
    dlg.setStyleSheet(styleSheet());

    auto* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(16, 12, 16, 12);
    lay->setSpacing(8);

    lay->addWidget(new QLabel(QStringLiteral(
        "<span style='color:#66FCF1;font-size:14px;font-weight:bold;'>NEW TASK</span>")));

    auto* titleEdit = new QLineEdit(&dlg);
    titleEdit->setPlaceholderText(QStringLiteral("Task title..."));
    lay->addWidget(titleEdit);

    auto* catCombo = new QComboBox(&dlg);
    catCombo->addItems({QStringLiteral("General"), QStringLiteral("UE5"),
                        QStringLiteral("KiCad"), QStringLiteral("Blender")});
    auto* catRow = new QHBoxLayout();
    catRow->addWidget(new QLabel(QStringLiteral("Category:"), &dlg));
    catRow->addWidget(catCombo, 1);
    lay->addLayout(catRow);

    auto* priCombo = new QComboBox(&dlg);
    priCombo->addItems({QStringLiteral("Medium"), QStringLiteral("High"),
                        QStringLiteral("Low")});
    auto* priRow = new QHBoxLayout();
    priRow->addWidget(new QLabel(QStringLiteral("Priority:"), &dlg));
    priRow->addWidget(priCombo, 1);
    lay->addLayout(priRow);

    auto* deadEdit = new QDateTimeEdit(&dlg);
    deadEdit->setCalendarPopup(true);
    deadEdit->setDateTime(QDateTime::currentDateTime().addDays(1));
    deadEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    auto* deadCheck = new QCheckBox(QStringLiteral("Set deadline"), &dlg);
    deadEdit->setEnabled(false);
    connect(deadCheck, &QCheckBox::toggled, deadEdit, &QWidget::setEnabled);
    auto* deadRow = new QHBoxLayout();
    deadRow->addWidget(deadCheck);
    deadRow->addWidget(deadEdit, 1);
    lay->addLayout(deadRow);

    auto* btnRow = new QHBoxLayout();
    auto* saveBtn = new QPushButton(QStringLiteral("CREATE"), &dlg);
    saveBtn->setObjectName(QStringLiteral("addBtn"));
    auto* cancelBtn = new QPushButton(QStringLiteral("CANCEL"), &dlg);
    cancelBtn->setObjectName(QStringLiteral("dlgBtn"));
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(saveBtn);
    lay->addLayout(btnRow);

    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, &dlg, [&]() {
        if (titleEdit->text().trimmed().isEmpty()) return;
        DbTask t;
        t.userId   = m_userId;
        t.title    = titleEdit->text().trimmed();
        t.category = catCombo->currentText();
        t.status   = QStringLiteral("Todo");
        t.priority = priCombo->currentText();
        if (deadCheck->isChecked())
            t.deadline = deadEdit->dateTime();
        DatabaseManager::instance().addTask(t);
        dlg.accept();
    });

    if (dlg.exec() == QDialog::Accepted) {
        rebuild();
        emit taskChanged();
    }
}

void TaskManagerDialog::showEditDialog(const DbTask& task)
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Edit Task"));
    dlg.setFixedWidth(420);
    dlg.setStyleSheet(styleSheet());

    auto* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(16, 12, 16, 12);
    lay->setSpacing(8);

    lay->addWidget(new QLabel(QStringLiteral(
        "<span style='color:#66FCF1;font-size:14px;font-weight:bold;'>EDIT TASK</span>")));

    auto* titleEdit = new QLineEdit(task.title, &dlg);
    lay->addWidget(titleEdit);

    auto* catCombo = new QComboBox(&dlg);
    catCombo->addItems({QStringLiteral("General"), QStringLiteral("UE5"),
                        QStringLiteral("KiCad"), QStringLiteral("Blender")});
    catCombo->setCurrentText(task.category);
    auto* catRow = new QHBoxLayout();
    catRow->addWidget(new QLabel(QStringLiteral("Category:"), &dlg));
    catRow->addWidget(catCombo, 1);
    lay->addLayout(catRow);

    auto* statusCombo = new QComboBox(&dlg);
    statusCombo->addItems({QStringLiteral("Todo"), QStringLiteral("InProgress"),
                           QStringLiteral("Done")});
    statusCombo->setCurrentText(task.status);
    auto* stRow = new QHBoxLayout();
    stRow->addWidget(new QLabel(QStringLiteral("Status:"), &dlg));
    stRow->addWidget(statusCombo, 1);
    lay->addLayout(stRow);

    auto* priCombo = new QComboBox(&dlg);
    priCombo->addItems({QStringLiteral("Medium"), QStringLiteral("High"),
                        QStringLiteral("Low")});
    priCombo->setCurrentText(task.priority);
    auto* priRow = new QHBoxLayout();
    priRow->addWidget(new QLabel(QStringLiteral("Priority:"), &dlg));
    priRow->addWidget(priCombo, 1);
    lay->addLayout(priRow);

    auto* deadEdit = new QDateTimeEdit(&dlg);
    deadEdit->setCalendarPopup(true);
    deadEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    auto* deadCheck = new QCheckBox(QStringLiteral("Deadline"), &dlg);
    if (task.deadline.isValid()) {
        deadCheck->setChecked(true);
        deadEdit->setDateTime(task.deadline);
    } else {
        deadCheck->setChecked(false);
        deadEdit->setDateTime(QDateTime::currentDateTime().addDays(1));
        deadEdit->setEnabled(false);
    }
    connect(deadCheck, &QCheckBox::toggled, deadEdit, &QWidget::setEnabled);
    auto* deadRow = new QHBoxLayout();
    deadRow->addWidget(deadCheck);
    deadRow->addWidget(deadEdit, 1);
    lay->addLayout(deadRow);

    auto* btnRow = new QHBoxLayout();
    auto* delBtn = new QPushButton(QStringLiteral("DELETE"), &dlg);
    delBtn->setObjectName(QStringLiteral("deleteBtn"));
    auto* saveBtn = new QPushButton(QStringLiteral("SAVE"), &dlg);
    saveBtn->setObjectName(QStringLiteral("addBtn"));
    auto* cancelBtn = new QPushButton(QStringLiteral("CANCEL"), &dlg);
    cancelBtn->setObjectName(QStringLiteral("dlgBtn"));
    btnRow->addWidget(delBtn);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(saveBtn);
    lay->addLayout(btnRow);

    bool deleted = false;
    qint64 taskId = task.id;

    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(delBtn, &QPushButton::clicked, &dlg, [&]() {
        auto r = QMessageBox::question(&dlg, QStringLiteral("Delete Task"),
            QStringLiteral("Delete \"%1\"?").arg(titleEdit->text()),
            QMessageBox::Yes | QMessageBox::No);
        if (r == QMessageBox::Yes) {
            DatabaseManager::instance().deleteTask(taskId);
            deleted = true;
            dlg.accept();
        }
    });
    connect(saveBtn, &QPushButton::clicked, &dlg, [&]() {
        if (titleEdit->text().trimmed().isEmpty()) return;
        DbTask updated;
        updated.id       = taskId;
        updated.title    = titleEdit->text().trimmed();
        updated.category = catCombo->currentText();
        updated.status   = statusCombo->currentText();
        updated.priority = priCombo->currentText();
        if (deadCheck->isChecked())
            updated.deadline = deadEdit->dateTime();
        DatabaseManager::instance().updateTask(updated);
        dlg.accept();
    });

    if (dlg.exec() == QDialog::Accepted) {
        rebuild();
        emit taskChanged();
    }
}

// ============================================================
//  TaskNotifications
// ============================================================

QString TaskNotifications::checkDeadlines(qint64 userId, bool english)
{
    auto overdue = DatabaseManager::instance().getOverdueTasks(userId, 24);
    if (overdue.isEmpty()) return QString();

    QString msg;
    for (const auto& t : overdue) {
        qint64 secsTo = QDateTime::currentDateTime().secsTo(t.deadline);
        QString timeStr;
        if (secsTo < 0) {
            int hoursAgo = static_cast<int>((-secsTo) / 3600);
            timeStr = english
                ? QStringLiteral("OVERDUE by %1h").arg(qMax(hoursAgo, 1))
                : QStringLiteral("ПРОСРОЧЕНО на %1ч").arg(qMax(hoursAgo, 1));
        } else if (secsTo < 3600) {
            timeStr = english
                ? QStringLiteral("due in %1 minutes").arg(secsTo / 60)
                : QStringLiteral("через %1 мин").arg(secsTo / 60);
        } else {
            timeStr = english
                ? QStringLiteral("due in %1 hours").arg(secsTo / 3600)
                : QStringLiteral("через %1 ч").arg(secsTo / 3600);
        }

        msg += (english
            ? QStringLiteral("[TASK WARNING]: Sir, '%1' (%2) — %3!\n")
            : QStringLiteral("[ЗАДАЧА]: Сэр, '%1' (%2) — %3!\n"))
            .arg(t.title, t.category, timeStr);
    }
    return msg.trimmed();
}

QString TaskNotifications::buildTaskContext(qint64 userId)
{
    auto& db = DatabaseManager::instance();
    auto tasks = db.getTasks(userId);
    if (tasks.isEmpty()) return QString();

    int todo = 0, inProg = 0, done = 0;
    for (const auto& t : tasks) {
        if      (t.status == QStringLiteral("Todo"))       ++todo;
        else if (t.status == QStringLiteral("InProgress")) ++inProg;
        else                                                ++done;
    }

    QString ctx = QStringLiteral("=== TASK BOARD ===\n"
        "User's active tasks: %1 todo, %2 in progress, %3 done.\n")
        .arg(todo).arg(inProg).arg(done);

    auto overdue = db.getOverdueTasks(userId, 48);
    if (!overdue.isEmpty()) {
        ctx += QStringLiteral("URGENT deadlines:\n");
        for (const auto& t : overdue) {
            qint64 secsTo = QDateTime::currentDateTime().secsTo(t.deadline);
            QString urgency = secsTo < 0
                ? QStringLiteral("OVERDUE") : QStringLiteral("%1h left").arg(secsTo/3600);
            ctx += QStringLiteral("- [%1] \"%2\" (%3) — %4\n")
                .arg(t.priority, t.title, t.category, urgency);
        }
    }

    ctx += QStringLiteral(
        "You can help manage tasks. Voice/text commands:\n"
        "- 'add task <title>' / 'new task <title>'\n"
        "- 'move task <id> to done' / 'mark task <id> done'\n"
        "- 'change deadline for <title> to tomorrow'\n"
        "Proactively remind user about approaching deadlines.\n\n");
    return ctx;
}
