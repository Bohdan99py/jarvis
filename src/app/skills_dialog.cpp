// -------------------------------------------------------
// skills_dialog.cpp — Управление модульными скиллами JARVIS
// -------------------------------------------------------

#include "skills_dialog.h"

#include "skill_manager.h"
#include "lang.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

SkillsDialog::SkillsDialog(SkillManager* skills, QWidget* parent)
    : QDialog(parent)
    , m_skills(skills)
{
    const bool en = IS_EN;

    setWindowTitle(en ? QStringLiteral("JARVIS — Skills")
                      : QStringLiteral("JARVIS — Скиллы"));
    setMinimumSize(560, 460);
    setStyleSheet(QStringLiteral(
        "QDialog { background: #0a0a1a; color: #ecf0f1; }"
        "QLabel { color: #ecf0f1; }"
        "QListWidget { background: #0f1626; color: #ecf0f1; "
        "border: 1px solid #1a5070; border-radius: 4px; }"
        "QListWidget::item { padding: 6px; }"
        "QListWidget::item:selected { background: #1a3a5c; }"
        "QPushButton { background: #0f2438; color: #00d4ff; "
        "border: 1px solid #1a5070; border-radius: 4px; padding: 5px 18px; }"
        "QPushButton:hover { background: #1a3a5c; }"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 12);

    auto* header = new QLabel(en
        ? QStringLiteral("🧩 Skills are knowledge modules — like Lego blocks. "
                         "Enable only what you need; changes apply instantly, "
                         "no restart required.")
        : QStringLiteral("🧩 Скиллы — это модули знаний, как лего-блоки. "
                         "Включай только нужное; изменения применяются сразу, "
                         "без перезапуска."), this);
    header->setWordWrap(true);
    layout->addWidget(header);

    m_list = new QListWidget(this);
    layout->addWidget(m_list, 1);

    m_description = new QLabel(this);
    m_description->setWordWrap(true);
    m_description->setStyleSheet(QStringLiteral("color: #8fa3b0; padding: 4px;"));
    m_description->setMinimumHeight(60);
    layout->addWidget(m_description);

    auto* buttons = new QHBoxLayout();
    auto* btnImport = new QPushButton(en ? QStringLiteral("📥 Import skill...")
                                         : QStringLiteral("📥 Импортировать скилл..."), this);
    auto* btnFolder = new QPushButton(en ? QStringLiteral("📂 Open skills folder")
                                         : QStringLiteral("📂 Папка скиллов"), this);
    auto* btnRescan = new QPushButton(en ? QStringLiteral("🔄 Rescan")
                                         : QStringLiteral("🔄 Обновить"), this);
    auto* btnClose  = new QPushButton(en ? QStringLiteral("Close")
                                         : QStringLiteral("Закрыть"), this);
    buttons->addWidget(btnImport);
    buttons->addWidget(btnFolder);
    buttons->addWidget(btnRescan);
    buttons->addStretch(1);
    buttons->addWidget(btnClose);
    layout->addLayout(buttons);

    connect(btnImport, &QPushButton::clicked, this, &SkillsDialog::onImportClicked);
    connect(btnFolder, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(SkillManager::userSkillsDir()));
    });
    connect(btnRescan, &QPushButton::clicked, this, [this]() {
        m_skills->scan();
        reloadList();
    });
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_list, &QListWidget::itemChanged, this, &SkillsDialog::onItemChanged);
    connect(m_list, &QListWidget::itemSelectionChanged,
            this, &SkillsDialog::onSelectionChanged);

    reloadList();
}

void SkillsDialog::reloadList()
{
    const bool en = IS_EN;
    m_reloading = true;
    m_list->clear();

    const auto& skills = m_skills->skills();
    for (const SkillInfo& s : skills) {
        auto* item = new QListWidgetItem(
            QStringLiteral("🧩 ") + s.displayName(en)
            + QStringLiteral("   v") + s.version, m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(s.enabled ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, s.id);
    }

    if (skills.isEmpty()) {
        auto* empty = new QListWidgetItem(en
            ? QStringLiteral("No skills installed. Import one or drop a folder "
                             "into the skills directory.")
            : QStringLiteral("Скиллы не установлены. Импортируй скилл или "
                             "положи папку в директорию скиллов."), m_list);
        empty->setFlags(Qt::NoItemFlags);
    }

    m_reloading = false;
    if (m_list->count() > 0) m_list->setCurrentRow(0);
    onSelectionChanged();
}

void SkillsDialog::onItemChanged(QListWidgetItem* item)
{
    if (m_reloading || !item) return;
    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty()) return;
    m_skills->setEnabled(id, item->checkState() == Qt::Checked);
}

void SkillsDialog::onSelectionChanged()
{
    const bool en = IS_EN;
    const auto* item = m_list->currentItem();
    if (!item) { m_description->clear(); return; }

    const QString id = item->data(Qt::UserRole).toString();
    for (const SkillInfo& s : m_skills->skills()) {
        if (s.id != id) continue;
        QString text = s.description(en);
        const QString feats = s.features.join(QStringLiteral(", "));
        if (!feats.isEmpty()) {
            text += (en ? QStringLiteral("\nCore features: ")
                        : QStringLiteral("\nНативные фичи: ")) + feats;
        }
        m_description->setText(text);
        return;
    }
    m_description->clear();
}

void SkillsDialog::onImportClicked()
{
    const bool en = IS_EN;
    const QString dir = QFileDialog::getExistingDirectory(this,
        en ? QStringLiteral("Select a skill folder (with skill.json)")
           : QStringLiteral("Выбери папку скилла (со skill.json)"));
    if (dir.isEmpty()) return;

    const QString error = m_skills->importSkill(dir, en);
    if (!error.isEmpty()) {
        QMessageBox::warning(this,
            en ? QStringLiteral("Import failed") : QStringLiteral("Ошибка импорта"),
            error);
        return;
    }
    reloadList();
}
