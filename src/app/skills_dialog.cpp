// -------------------------------------------------------
// skills_dialog.cpp — Управление модульными скиллами JARVIS
// -------------------------------------------------------

#include "skills_dialog.h"

#include "skill_manager.h"
#include "lang.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

namespace {

// Роли Qt::UserRole используем для типа/id элемента дерева.
enum ItemRole {
    RoleKind = Qt::UserRole,      // 0 = группа, 1 = лист (скилл)
    RoleId   = Qt::UserRole + 1,  // id скилла (только для листа)
};
enum ItemKind { KindGroup = 0, KindSkill = 1 };

// Показываем «нет категории» в конце списка, чтобы группы шли первыми.
QString rootLabel(bool en) {
    return en ? QStringLiteral("Other") : QStringLiteral("Прочее");
}

} // namespace

SkillsDialog::SkillsDialog(SkillManager* skills, QWidget* parent)
    : QDialog(parent)
    , m_skills(skills)
{
    const bool en = IS_EN;

    setWindowTitle(en ? QStringLiteral("JARVIS — Skills")
                      : QStringLiteral("JARVIS — Скиллы"));
    setMinimumSize(620, 520);
    setStyleSheet(QStringLiteral(
        "QDialog { background: #0a0a1a; color: #ecf0f1; }"
        "QLabel { color: #ecf0f1; }"
        "QTreeWidget { background: #0f1626; color: #ecf0f1; "
        "border: 1px solid #1a5070; border-radius: 4px; }"
        "QTreeWidget::item { padding: 4px; }"
        "QTreeWidget::item:selected { background: #1a3a5c; }"
        "QTreeWidget::branch:has-children:!has-siblings:closed,"
        "QTreeWidget::branch:closed:has-children:has-siblings { "
        "border-image: none; }"
        "QPushButton { background: #0f2438; color: #00d4ff; "
        "border: 1px solid #1a5070; border-radius: 4px; padding: 5px 18px; }"
        "QPushButton:hover { background: #1a3a5c; }"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 12);

    auto* header = new QLabel(en
        ? QStringLiteral("🧩 Skills are knowledge modules — like Lego blocks. "
                         "Toggle whole groups (e.g. Development → Unreal Engine) "
                         "or individual skills. Changes apply instantly, "
                         "no restart required.")
        : QStringLiteral("🧩 Скиллы — это модули знаний, как лего-блоки. "
                         "Включай/выключай целые группы (например, Разработка "
                         "→ Unreal Engine) или отдельные скиллы. Изменения "
                         "применяются сразу, без перезапуска."), this);
    header->setWordWrap(true);
    layout->addWidget(header);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setUniformRowHeights(false);
    m_tree->setAnimated(true);
    layout->addWidget(m_tree, 1);

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
        reloadTree();
    });
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_tree, &QTreeWidget::itemChanged, this, &SkillsDialog::onItemChanged);
    connect(m_tree, &QTreeWidget::itemSelectionChanged,
            this, &SkillsDialog::onSelectionChanged);

    reloadTree();
}

// ============================================================
// Построение дерева
// ============================================================

void SkillsDialog::reloadTree()
{
    const bool en = IS_EN;
    m_reloading = true;
    m_tree->clear();

    const auto& skills = m_skills->skills();

    // Индекс: category -> subcategory -> QTreeWidgetItem*
    QHash<QString, QTreeWidgetItem*> topLevel;
    QHash<QString, QTreeWidgetItem*> subLevel; // key = category + "/" + subcategory

    auto ensureTop = [&](const QString& id, const QString& label) {
        if (auto* it = topLevel.value(id)) return it;
        auto* it = new QTreeWidgetItem(m_tree);
        it->setText(0, QStringLiteral("📁 ") + label);
        it->setData(0, RoleKind, KindGroup);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable
                                 | Qt::ItemIsAutoTristate);
        it->setCheckState(0, Qt::Unchecked);
        it->setExpanded(true);
        topLevel.insert(id, it);
        return it;
    };
    auto ensureSub = [&](QTreeWidgetItem* parent,
                         const QString& parentId,
                         const QString& id, const QString& label) {
        const QString key = parentId + QStringLiteral("/") + id;
        if (auto* it = subLevel.value(key)) return it;
        auto* it = new QTreeWidgetItem(parent);
        it->setText(0, QStringLiteral("🎮 ") + label);
        it->setData(0, RoleKind, KindGroup);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable
                                 | Qt::ItemIsAutoTristate);
        it->setCheckState(0, Qt::Unchecked);
        it->setExpanded(true);
        subLevel.insert(key, it);
        return it;
    };

    for (const SkillInfo& s : skills) {
        QTreeWidgetItem* parent = nullptr;

        if (!s.category.isEmpty()) {
            const QString topLabel = !s.categoryLabel(en).isEmpty()
                                     ? s.categoryLabel(en) : s.category;
            QTreeWidgetItem* top = ensureTop(s.category, topLabel);

            if (!s.subcategory.isEmpty()) {
                const QString subLabel = !s.subcategoryLabel(en).isEmpty()
                                         ? s.subcategoryLabel(en) : s.subcategory;
                parent = ensureSub(top, s.category, s.subcategory, subLabel);
            } else {
                parent = top;
            }
        }

        auto* leaf = parent ? new QTreeWidgetItem(parent)
                            : new QTreeWidgetItem(m_tree);
        leaf->setText(0, QStringLiteral("🧩 ") + s.displayName(en)
                        + QStringLiteral("   v") + s.version);
        leaf->setData(0, RoleKind, KindSkill);
        leaf->setData(0, RoleId,   s.id);
        leaf->setFlags(leaf->flags() | Qt::ItemIsUserCheckable);
        leaf->setCheckState(0, s.enabled ? Qt::Checked : Qt::Unchecked);
    }

    // Инициализируем tri-state группы по фактическим состояниям детей.
    // Тут же добавляем счётчик «включено/всего» в подпись.
    auto annotateGroup = [](QTreeWidgetItem* g) {
        int total = 0, on = 0;
        for (int i = 0; i < g->childCount(); ++i) {
            auto* c = g->child(i);
            if (c->data(0, RoleKind).toInt() == KindSkill) {
                ++total;
                if (c->checkState(0) == Qt::Checked) ++on;
            } else {
                // подгруппа — считаем её листья
                for (int j = 0; j < c->childCount(); ++j) {
                    ++total;
                    if (c->child(j)->checkState(0) == Qt::Checked) ++on;
                }
            }
        }
        if (total > 0) {
            const QString base = g->text(0);
            g->setText(0, base + QStringLiteral("   (%1/%2)").arg(on).arg(total));
        }
    };
    for (auto* top : topLevel) annotateGroup(top);

    if (skills.isEmpty()) {
        auto* empty = new QTreeWidgetItem(m_tree);
        empty->setText(0, en
            ? QStringLiteral("No skills installed. Import one or drop a folder "
                             "into the skills directory.")
            : QStringLiteral("Скиллы не установлены. Импортируй скилл или "
                             "положи папку в директорию скиллов."));
        empty->setFlags(Qt::NoItemFlags);
    }

    m_reloading = false;
    if (auto* first = m_tree->topLevelItem(0)) m_tree->setCurrentItem(first);
    onSelectionChanged();
}

// ============================================================
// Клики по чекбоксам
// ============================================================

void SkillsDialog::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_reloading || m_syncing || !item || column != 0) return;

    const int kind = item->data(0, RoleKind).toInt();

    if (kind == KindSkill) {
        // Обычный листовой скилл — просто сохраняем состояние.
        const QString id = item->data(0, RoleId).toString();
        if (!id.isEmpty())
            m_skills->setEnabled(id, item->checkState(0) == Qt::Checked);
        return;
    }

    // Групповой чекбокс — каскадно включаем/выключаем всех потомков.
    // Partially-checked (Qt::PartiallyChecked) сюда прилетает только от
    // itemChanged на детях; вручную его не выставляем.
    if (item->checkState(0) == Qt::PartiallyChecked) return;

    m_syncing = true;
    const Qt::CheckState target = item->checkState(0);
    std::function<void(QTreeWidgetItem*)> cascade = [&](QTreeWidgetItem* it) {
        for (int i = 0; i < it->childCount(); ++i) {
            auto* c = it->child(i);
            if (c->data(0, RoleKind).toInt() == KindSkill) {
                if (c->checkState(0) != target) {
                    c->setCheckState(0, target);
                    const QString id = c->data(0, RoleId).toString();
                    if (!id.isEmpty())
                        m_skills->setEnabled(id, target == Qt::Checked);
                }
            } else {
                c->setCheckState(0, target);
                cascade(c);
            }
        }
    };
    cascade(item);
    m_syncing = false;

    // Обновим счётчик "n/m" у затронутого поддерева — проще перестроить.
    reloadTree();
}

// ============================================================
// Описание справа
// ============================================================

void SkillsDialog::onSelectionChanged()
{
    const bool en = IS_EN;
    const auto* item = m_tree->currentItem();
    if (!item) { m_description->clear(); return; }

    if (item->data(0, RoleKind).toInt() == KindGroup) {
        m_description->setText(en
            ? QStringLiteral("Group: toggle to enable/disable all skills inside.")
            : QStringLiteral("Группа: галочка включает или выключает все скиллы внутри."));
        return;
    }

    const QString id = item->data(0, RoleId).toString();
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

// Не используем — оставлен для совместимости с header, где он объявлен.
void SkillsDialog::refreshGroupCheckState(QTreeWidgetItem* /*group*/) {}

// ============================================================
// Импорт
// ============================================================

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
    reloadTree();
}
