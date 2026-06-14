#!/usr/bin/env python3
# ============================================================
# scripts/update_changelog.py
# Автоматически обновляет секцию "## Что нового" в README.md
# на основе git log + тегов релизов.
#
# Запуск: python scripts/update_changelog.py
# CI: вызывается GitHub Actions при каждом пуше в main
# ============================================================

import subprocess
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

# ── Настройки ────────────────────────────────────────────────

README_PATH   = Path("README.md")
MAX_ENTRIES   = 20          # максимум записей в changelog
MAX_MSG_LEN   = 100         # обрезаем длинные сообщения
SECTION_START = "<!-- CHANGELOG_START -->"
SECTION_END   = "<!-- CHANGELOG_END -->"

# Категории по ключевым словам в сообщении коммита
CATEGORIES = {
    "🧠 ИИ / AI":      ["brain", "claude", "gemini", "groq", "ai", "llm", "whisper",
                        "voice", "speech", "голос", "шёпот", "распознавание"],
    "🗄️ База данных":  ["database", "db", "sql", "sqlite", "миграция", "migration",
                        "storage", "memory", "память", "history", "история"],
    "📚 Обучение":     ["learner", "learning", "pattern", "index", "rag",
                        "обучение", "индекс", "паттерн"],
    "🔌 Плагины":      ["plugin", "плагин", "module", "модуль"],
    "🖥️ Интерфейс":   ["ui", "qml", "window", "tray", "icon", "theme",
                        "интерфейс", "окно", "трей"],
    "🔧 Инфраструктура": ["cmake", "ci", "actions", "build", "deploy", "release",
                          "сборка", "деплой", "обновление", "updater"],
    "🐛 Исправления":  ["fix", "bug", "error", "crash", "исправл", "ошибк", "фикс"],
    "✨ Новое":        [],   # fallback
}

def run(cmd: list[str], **kwargs) -> str:
    result = subprocess.run(cmd, capture_output=True, text=True, **kwargs)
    return result.stdout.strip()

def get_latest_tag() -> str:
    tag = run(["git", "describe", "--tags", "--abbrev=0"], cwd=".")
    return tag or "v0.0.0"

def get_commits_since(tag: str) -> list[dict]:
    """Получаем коммиты с тега до HEAD."""
    fmt = "%H|||%s|||%ai|||%an"
    since = f"{tag}..HEAD" if tag != "v0.0.0" else "HEAD~50..HEAD"
    log = run(["git", "log", since, f"--pretty=format:{fmt}", "--no-merges"])
    commits = []
    for line in log.splitlines():
        parts = line.split("|||")
        if len(parts) != 4:
            continue
        sha, msg, date_str, author = parts
        msg = msg.strip()
        if not msg or msg.startswith("Merge"):
            continue
        # Обрезаем длинные сообщения
        if len(msg) > MAX_MSG_LEN:
            msg = msg[:MAX_MSG_LEN] + "…"
        commits.append({
            "sha":    sha[:7],
            "msg":    msg,
            "date":   date_str[:10],
            "author": author,
        })
    return commits[:MAX_ENTRIES]

def categorize(msg: str) -> str:
    lower = msg.lower()
    for category, keywords in CATEGORIES.items():
        if any(kw in lower for kw in keywords):
            return category
    return "✨ Новое"

def get_all_tags() -> list[dict]:
    """Получаем последние релизы с датами."""
    out = run(["git", "tag", "--sort=-version:refname", "--format=%(refname:short)|||%(creatordate:short)"])
    tags = []
    for line in out.splitlines()[:5]:  # последние 5 тегов
        parts = line.split("|||")
        if len(parts) == 2:
            tags.append({"tag": parts[0], "date": parts[1]})
    return tags

def build_changelog_section(commits: list[dict], latest_tag: str,
                              tags: list[dict]) -> str:
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    lines = [
        SECTION_START,
        "",
        "## Что нового / What's New",
        "",
        f"> Автоматически обновляется при каждом пуше · Last updated: `{now}`",
        "",
    ]

    # Блок последних релизов
    if tags:
        lines.append("### 🏷️ Последние релизы")
        for t in tags:
            lines.append(f"- **{t['tag']}** — {t['date']}")
        lines.append("")

    # Текущие изменения с последнего тега
    if commits:
        lines.append(f"### 🔄 Изменения с `{latest_tag}`")
        lines.append("")

        # Группируем по категориям
        grouped: dict[str, list[dict]] = {}
        for c in commits:
            cat = categorize(c["msg"])
            grouped.setdefault(cat, []).append(c)

        for category in CATEGORIES:
            if category not in grouped:
                continue
            lines.append(f"**{category}**")
            for c in grouped[category]:
                lines.append(f"- `{c['sha']}` {c['msg']} _{c['date']}_")
            lines.append("")
    else:
        lines.append(f"*Нет изменений с {latest_tag}*")
        lines.append("")

    # Ссылка на полный changelog
    lines.append("📋 [Полная история релизов](https://github.com/Bohdan99py/jarvis/releases)")
    lines.append("")
    lines.append(SECTION_END)
    return "\n".join(lines)

def update_readme(section: str) -> bool:
    """Вставляем/заменяем секцию в README.md."""
    if not README_PATH.exists():
        print(f"ERROR: {README_PATH} not found")
        return False

    content = README_PATH.read_text(encoding="utf-8")

    if SECTION_START in content and SECTION_END in content:
        # Заменяем существующую секцию
        pattern = re.compile(
            re.escape(SECTION_START) + r".*?" + re.escape(SECTION_END),
            re.DOTALL
        )
        new_content = pattern.sub(section, content)
    else:
        # Вставляем перед ## Стек или ## License или в конец
        for marker in ["## Стек", "## Stack", "## Лицензия", "## License"]:
            if marker in content:
                new_content = content.replace(marker, section + "\n\n" + marker, 1)
                break
        else:
            new_content = content + "\n\n" + section

    README_PATH.write_text(new_content, encoding="utf-8")
    return True

def main():
    print("🔄 Updating README changelog...")

    latest_tag = get_latest_tag()
    print(f"   Latest tag: {latest_tag}")

    commits = get_commits_since(latest_tag)
    print(f"   Commits since tag: {len(commits)}")

    tags = get_all_tags()
    print(f"   Tags found: {[t['tag'] for t in tags]}")

    section = build_changelog_section(commits, latest_tag, tags)
    success  = update_readme(section)

    if success:
        print(f"✅ README.md updated successfully")
        print(f"   Section preview:")
        for line in section.splitlines()[:10]:
            print(f"   {line}")
    else:
        print("❌ Failed to update README.md")
        sys.exit(1)

if __name__ == "__main__":
    main()
