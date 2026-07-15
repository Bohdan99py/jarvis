// ============================================================
// task_manager_dialog.cpp — Cyberpunk Kanban Task Manager
// ============================================================

#include "task_manager_dialog.h"
#include "task_board_model.h"
#include "notification_manager.h"
#include "lang.h"

#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QCoreApplication>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QDateTime>
#include <QDebug>

static const char* kDialogFormCss = R"(
    QDialog {
        background-color: #0B0C10;
        color: #c5c6c7;
    }
    QLabel { color: #c5c6c7; }
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
    QPushButton#addBtn:hover { background: #66FCF1; }
    QPushButton#dlgBtn {
        background: #1F2833;
        color: #66FCF1;
        border: 1px solid rgba(102,252,241,0.2);
        border-radius: 4px;
        padding: 6px 18px;
        font-size: 12px;
    }
    QPushButton#dlgBtn:hover { background: rgba(102,252,241,0.12); }
    QPushButton#deleteBtn {
        background: rgba(255,82,82,0.15);
        color: #ff5252;
        border: 1px solid rgba(255,82,82,0.3);
        border-radius: 4px;
        padding: 6px 18px;
        font-size: 12px;
    }
    QPushButton#deleteBtn:hover { background: rgba(255,82,82,0.3); }
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

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_board = new QQuickWidget(this);
    m_board->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_board->setClearColor(QColor(0x0B, 0x0C, 0x10));

    // See notification_manager.cpp for why this is needed: windeployqt
    // drops QML plugin deps under bin/qml/, which isn't on the engine's
    // default import search path.
    m_board->engine()->addImportPath(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/qml"));

    m_todoModel       = new TaskListModel(this);
    m_inProgressModel = new TaskListModel(this);
    m_doneModel       = new TaskListModel(this);
    m_todoModel->setEnglish(IS_EN);
    m_inProgressModel->setEnglish(IS_EN);
    m_doneModel->setEnglish(IS_EN);

    QQmlContext* ctx = m_board->rootContext();
    ctx->setContextProperty(QStringLiteral("todoModel"), m_todoModel);
    ctx->setContextProperty(QStringLiteral("inProgressModel"), m_inProgressModel);
    ctx->setContextProperty(QStringLiteral("doneModel"), m_doneModel);
    ctx->setContextProperty(QStringLiteral("boardEnglish"), IS_EN);
    ctx->setContextProperty(QStringLiteral("boardProgress"), 0.0);
    // NB: the literal 0 must be wrapped in QVariant() — passed bare, C++
    // overload resolution prefers QQmlContext::setContextProperty(name,
    // QObject*) over the QVariant overload (0 is a null-pointer-constant),
    // silently registering the property as a null QObject instead of an
    // int. QML then throws "Cannot assign std::nullptr_t to int" the
    // moment anything binds to it strictly-typed.
    ctx->setContextProperty(QStringLiteral("boardDoneCount"), QVariant(0));
    ctx->setContextProperty(QStringLiteral("boardTotalCount"), QVariant(0));
    ctx->setContextProperty(QStringLiteral("taskBoard"), this);

    m_board->setSource(QUrl(QStringLiteral("qrc:/qml/TaskBoard.qml")));

    root->addWidget(m_board);

    rebuild();
}

void TaskManagerDialog::rebuild()
{
    auto& db = DatabaseManager::instance();
    auto allTasks = db.getTasks(m_userId);

    QList<DbTask> todo, inProg, done;
    for (const auto& t : allTasks) {
        if      (t.status == QStringLiteral("Todo"))       todo.append(t);
        else if (t.status == QStringLiteral("InProgress")) inProg.append(t);
        else                                                done.append(t);
    }

    m_todoModel->setTasks(todo);
    m_inProgressModel->setTasks(inProg);
    m_doneModel->setTasks(done);

    const int total = allTasks.size();
    const double progress = total > 0 ? double(done.size()) / total : 0.0;

    QQmlContext* ctx = m_board->rootContext();
    ctx->setContextProperty(QStringLiteral("boardProgress"), progress);
    ctx->setContextProperty(QStringLiteral("boardDoneCount"), done.size());
    ctx->setContextProperty(QStringLiteral("boardTotalCount"), total);
}

// ============================================================
// Invokable from TaskBoard.qml
// ============================================================

void TaskManagerDialog::openTask(qint64 id)
{
    auto t = DatabaseManager::instance().getTask(id);
    if (t) showEditDialog(*t);
}

void TaskManagerDialog::requestAdd()
{
    showAddDialog();
}

void TaskManagerDialog::moveTask(qint64 id, const QString& fromStatus, const QString& toStatus)
{
    if (fromStatus == toStatus) return;

    auto t = DatabaseManager::instance().getTask(id);
    if (!t) return;

    t->status = toStatus;
    DatabaseManager::instance().updateTask(*t);

    // Move the row between the in-memory models directly (instead of a
    // full rebuild()) so TaskBoard.qml's add/remove transitions animate
    // exactly the one card that moved rather than resetting the board.
    auto modelFor = [this](const QString& status) -> TaskListModel* {
        if (status == QStringLiteral("Todo"))       return m_todoModel;
        if (status == QStringLiteral("InProgress")) return m_inProgressModel;
        return m_doneModel;
    };
    TaskListModel* from = modelFor(fromStatus);
    TaskListModel* to   = modelFor(toStatus);

    if (auto moved = from->takeById(id))
        to->insertTask(*moved);

    const int total = m_todoModel->count() + m_inProgressModel->count() + m_doneModel->count();
    const double progress = total > 0 ? double(m_doneModel->count()) / total : 0.0;
    QQmlContext* ctx = m_board->rootContext();
    ctx->setContextProperty(QStringLiteral("boardProgress"), progress);
    ctx->setContextProperty(QStringLiteral("boardDoneCount"), m_doneModel->count());
    ctx->setContextProperty(QStringLiteral("boardTotalCount"), total);

    NotificationManager::instance().showNotification(
        toStatus == QStringLiteral("Done")
            ? (IS_EN ? QStringLiteral("Task completed") : QStringLiteral("Задача выполнена"))
            : (IS_EN ? QStringLiteral("Task moved") : QStringLiteral("Задача перемещена")),
        t->title,
        toStatus == QStringLiteral("Done") ? NotificationManager::Level::Success
                                            : NotificationManager::Level::Info);

    emit taskChanged();
}

// ============================================================
// Add / Edit forms — still plain QDialog; no reason to rebuild a
// perfectly good form in QML just for the sake of it.
// ============================================================

void TaskManagerDialog::showAddDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("New Task"));
    dlg.setFixedWidth(400);
    dlg.setStyleSheet(QString::fromUtf8(kDialogFormCss));

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

    QString createdTitle;

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
        createdTitle = t.title;
        dlg.accept();
    });

    if (dlg.exec() == QDialog::Accepted) {
        rebuild();
        NotificationManager::instance().showNotification(
            IS_EN ? QStringLiteral("Task created") : QStringLiteral("Задача создана"),
            createdTitle, NotificationManager::Level::Success);
        emit taskChanged();
    }
}

void TaskManagerDialog::showEditDialog(const DbTask& task)
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Edit Task"));
    dlg.setFixedWidth(420);
    dlg.setStyleSheet(QString::fromUtf8(kDialogFormCss));

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
        if (deleted) {
            NotificationManager::instance().showNotification(
                IS_EN ? QStringLiteral("Task deleted") : QStringLiteral("Задача удалена"),
                titleEdit->text(), NotificationManager::Level::Warning);
        }
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
