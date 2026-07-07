// -------------------------------------------------------
// profile_setup_dialog.cpp — Мастер профиля пользователя
// -------------------------------------------------------

#include "profile_setup_dialog.h"

#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPushButton>

QList<ProfileSetupDialog::RoleEntry> ProfileSetupDialog::knownRoles(bool english)
{
    return {
        {QStringLiteral("Developer"),
         english ? QStringLiteral("Developer") : QStringLiteral("Разработчик")},
        {QStringLiteral("QA_Tester"),
         english ? QStringLiteral("QA Tester") : QStringLiteral("QA Тестировщик")},
        {QStringLiteral("Digital_Artist"),
         english ? QStringLiteral("Digital Artist / Illustrator")
                 : QStringLiteral("Цифровой художник / Иллюстратор")},
        {QStringLiteral("Casual_Friend"),
         english ? QStringLiteral("Casual / Friend") : QStringLiteral("Дружеский / Casual")},
        {QStringLiteral("Student_Academic"),
         english ? QStringLiteral("Student / Academic") : QStringLiteral("Студент / Академия")},
    };
}

ProfileSetupDialog::ProfileSetupDialog(Mode mode, bool english, QWidget* parent)
    : QDialog(parent)
    , m_mode(mode)
    , m_english(english)
{
    setWindowTitle(mode == Mode::FirstRun
        ? (english ? QStringLiteral("Welcome to J.A.R.V.I.S.")
                   : QStringLiteral("Добро пожаловать в J.A.R.V.I.S."))
        : (english ? QStringLiteral("Edit Profile")
                   : QStringLiteral("Редактирование профиля")));
    setModal(true);
    setMinimumWidth(380);

    auto* layout = new QVBoxLayout(this);

    if (mode == Mode::FirstRun) {
        auto* intro = new QLabel(english
            ? QStringLiteral("Let's get acquainted. JARVIS uses your name and role\n"
                             "to personalize responses on this computer.")
            : QStringLiteral("Давай знакомиться. JARVIS использует имя и роль,\n"
                             "чтобы персонализировать ответы на этом компьютере."),
            this);
        intro->setWordWrap(true);
        layout->addWidget(intro);
    }

    auto* form = new QFormLayout();

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(english ? QStringLiteral("Your name")
                                           : QStringLiteral("Ваше имя"));
    form->addRow(english ? QStringLiteral("Name:") : QStringLiteral("Имя:"), m_nameEdit);

    m_roleBox = new QComboBox(this);
    for (const auto& r : knownRoles(english))
        m_roleBox->addItem(r.label, r.value);
    form->addRow(english ? QStringLiteral("Role:") : QStringLiteral("Роль:"), m_roleBox);

    m_langBox = new QComboBox(this);
    m_langBox->addItem(english ? QStringLiteral("Auto-detect") : QStringLiteral("Автоопределение"),
                       QStringLiteral("auto"));
    m_langBox->addItem(QStringLiteral("Русский"), QStringLiteral("ru"));
    m_langBox->addItem(QStringLiteral("English"), QStringLiteral("en"));
    form->addRow(english ? QStringLiteral("Language:") : QStringLiteral("Язык:"), m_langBox);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | (mode == Mode::Edit ? QDialogButtonBox::Cancel
                                                   : QDialogButtonBox::NoButton),
        this);
    buttons->button(QDialogButtonBox::Ok)->setText(mode == Mode::FirstRun
        ? (english ? QStringLiteral("Start") : QStringLiteral("Начать"))
        : (english ? QStringLiteral("Save")  : QStringLiteral("Сохранить")));
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (m_nameEdit->text().trimmed().isEmpty()) {
            m_nameEdit->setFocus();
            return; // имя обязательно
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    setStyleSheet(QStringLiteral(
        "QDialog { background: #0a0a1a; }"
        "QLabel { color: #ecf0f1; font-family: Consolas; font-size: 12px; }"
        "QLineEdit, QComboBox { background: #0f2438; color: #ecf0f1; "
        "border: 1px solid #1a5070; border-radius: 4px; padding: 4px 8px; }"
        "QPushButton { background: #0f2438; color: #00d4ff; border: 1px solid #1a5070; "
        "border-radius: 4px; padding: 5px 18px; }"));
}

void ProfileSetupDialog::setProfile(const DbUserProfile& profile)
{
    m_profile = profile;
    m_nameEdit->setText(profile.name);

    // currentRole — реальное значение (влияет на тон/меш); scenario —
    // старое декоративное поле. Предпочитаем currentRole, но старые
    // профили без него falls back на scenario, чтобы не терять выбор.
    const QString roleValue = !profile.currentRole.isEmpty()
        ? profile.currentRole : profile.scenario;
    int roleIdx = m_roleBox->findData(roleValue);
    if (roleIdx < 0) roleIdx = m_roleBox->findText(roleValue);
    if (roleIdx >= 0) m_roleBox->setCurrentIndex(roleIdx);

    const int langIdx = m_langBox->findData(profile.language);
    if (langIdx >= 0) m_langBox->setCurrentIndex(langIdx);
}

DbUserProfile ProfileSetupDialog::profile() const
{
    DbUserProfile p = m_profile;
    p.name     = m_nameEdit->text().trimmed();
    // Один выбор роли пишется в оба поля — устраняет расхождение,
    // из-за которого раньше существовал отдельный пункт "Switch Role".
    const QString roleValue = m_roleBox->currentData().toString();
    p.currentRole = roleValue;
    p.scenario    = roleValue;
    p.language = m_langBox->currentData().toString();
    if (p.preferences.isEmpty()) p.preferences = QStringLiteral("{}");
    return p;
}
