# ⬡ J.A.R.V.I.S. — Personal AI Assistant

> Just A Rather Very Intelligent System

Персональный ИИ-ассистент для Windows с интеграцией Claude API и модульной архитектурой.

---

## Быстрый старт

### Требования для сборки

- **Windows 10/11** (x64)
- **Qt 6.7+** — [qt.io/download](https://www.qt.io/download-qt-installer)
- **CMake 3.20+** — [cmake.org](https://cmake.org/download/)
- **Visual Studio 2022** (Community бесплатна) — [visualstudio.com](https://visualstudio.microsoft.com/)
- **Git** — [git-scm.com](https://git-scm.com/)

### Сборка за 3 команды

```bash
git clone https://github.com/YOUR_USER/jarvis.git
cd jarvis
build.bat Release
```

Если Qt стоит не по дефолтному пути:
```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.8.0\msvc2022_64"
cmake --build build --config Release --parallel
```

Результат: `build\bin\JarvisApp.exe`

---

## Как создать GitHub репозиторий

### Шаг 1: Создай репозиторий

1. Открой [github.com/new](https://github.com/new)
2. Имя: `jarvis`
3. Приватный или публичный — на твой выбор
4. **НЕ** ставь галки на README/gitignore (у нас уже есть)
5. Нажми "Create repository"

### Шаг 2: Загрузи код

```bash
cd jarvis
git init
git add .
git commit -m "Initial commit: JARVIS v2.0.0"
git branch -M main
git remote add origin https://github.com/ТВОЙ_ЮЗЕРНЕЙМ/jarvis.git
git push -u origin main
```

### Шаг 3: Замени плейсхолдеры

В этих файлах замени `YOUR_GITHUB_USER` на свой юзернейм:
- `core/jarvis.cpp` (строка с `setRepository`)
- `installer.iss` (строка `MyAppURL`)

```bash
# Быстрая замена (PowerShell):
(Get-Content core\jarvis.cpp) -replace 'YOUR_GITHUB_USER','МойЮзернейм' | Set-Content core\jarvis.cpp
(Get-Content installer.iss) -replace 'YOUR_GITHUB_USER','МойЮзернейм' | Set-Content installer.iss
git add . && git commit -m "Set GitHub username" && git push
```

### Шаг 4: Создай первый релиз

```bash
git tag v2.0.0
git push origin v2.0.0
```

GitHub Actions автоматически:
1. Соберёт проект
2. Запакует ZIP-архив
3. Выложит отдельные DLL для обновления
4. Создаст установщик (если есть иконки)
5. Опубликует всё в GitHub Releases

---

## Обновление без пересборки

### Архитектура

```
JARVIS/
├── JarvisApp.exe       ← Лаунчер (почти никогда не меняется)
├── JarvisCore.dll      ← Ядро (обновляется заменой файла)
├── plugins/
│   ├── plugins.json    ← Конфиг плагинов
│   └── *.dll           ← Плагины (каждый обновляется отдельно)
├── updates/            ← Сюда кладёшь новые DLL
└── backup/             ← Автобэкап перед обновлением
```

### Как обновить

**Из приложения:**
```
> обновление
```
JARVIS проверит GitHub Releases и скажет есть ли новая версия.

**Вручную:**
1. Скачай `JarvisCore.dll` из [Releases](https://github.com/YOUR_USER/jarvis/releases)
2. Положи в папку `updates/` рядом с exe
3. Запусти `scripts\update.bat`

**Откат:**
```bash
scripts\update.bat rollback
```

### Что когда обновлять

| Изменил | Обновить | Как |
|---------|----------|-----|
| Команды, логику, память | `JarvisCore.dll` | Заменить DLL |
| Плагин | `plugins/ИмяПлагина.dll` | Заменить DLL |
| UI, окно | `JarvisApp.exe` + `JarvisCore.dll` | Заменить оба |
| Всё | Полная переустановка | Скачать ZIP/установщик |

---

## Создание установщика

### Вариант 1: Автоматически через GitHub Actions

При `git tag v2.1.0 && git push origin v2.1.0` — установщик создаётся автоматически и публикуется в Releases.

### Вариант 2: Локально

1. Установи [Inno Setup 6](https://jrsoftware.org/isdl.php) (бесплатно)
2. Подготовь пакет:
   ```bash
   scripts\prepare_release.bat "C:\Qt\6.8.0\msvc2022_64"
   ```
3. Собери установщик:
   ```bash
   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer.iss
   ```
4. Результат: `build\installer\JARVIS-Setup-2.0.0.exe`

### Вариант 3: Простой ZIP

```bash
scripts\prepare_release.bat
cd build\release_package
7z a ..\JARVIS-v2.0.0-win64.zip *
```

---

## Вшивание API-ключа

Чтобы новые пользователи не вводили `apikey` вручную:

### Шаг 1: Сгенерируй зашифрованный ключ

```bash
scripts\generate_key.bat sk-ant-api03-ТВОЙ_КЛЮЧ_ЗДЕСЬ
```

Скрипт выведет массив байтов.

### Шаг 2: Вставь в код

Открой `core/embedded_key.h` и замени:

```cpp
// БЫЛО:
static constexpr uint8_t ENCRYPTED_KEY_DATA[] = { 0x00 };
static constexpr int ENCRYPTED_KEY_SIZE = 0;

// СТАЛО (пример):
static constexpr uint8_t ENCRYPTED_KEY_DATA[] = {
    0x39, 0x2A, 0x21, 0x1D, 0x00, 0x36, 0xAF, 0xC0,
    0xD3, 0x86, 0xAE, 0x93, 0x76, 0x52, 0x74, 0x08,
    // ... остальные байты из generate_key.bat
};
static constexpr int ENCRYPTED_KEY_SIZE = 48;
```

### Шаг 3: Пересобери

```bash
build.bat Release
```

### Приоритет ключей

1. Если пользователь ввёл `apikey <ключ>` — используется его ключ
2. Если не вводил — используется вшитый ключ
3. Если ключ не вшит — просит ввести

### Безопасность

**XOR-обфускация** — ключ не виден как plaintext в бинарнике (нельзя найти через `strings`). Но это НЕ полноценное шифрование — опытный реверсер может извлечь.

Для продакшена с чужими пользователями лучше **прокси-сервер**: JARVIS → твой сервер → Anthropic API. Так ключ вообще не попадает к пользователю.

---

## Команды

| Команда | Описание |
|---------|----------|
| `помощь` | Список всех команд |
| `привет` | Приветствие |
| `время` / `дата` | Текущее время/дата |
| `запусти <app>` | Запуск приложения |
| `найди <запрос>` | Поиск в Google |
| `youtube <запрос>` | Поиск на YouTube |
| `напечатай <текст>` | Набрать текст в активном окне |
| `комбо ctrl+c` | Комбинация клавиш |
| `apikey <ключ>` | Установить свой Claude API-ключ |
| `запомни ключ=значение` | Сохранить факт в память |
| `вспомни <ключ>` | Вспомнить факт |
| `статистика` | Статистика использования |
| `обновление` | Проверить обновления |
| `плагины` | Список загруженных плагинов |
| `перезагрузи <плагин>` | Hot-reload плагина |
| Любой текст | → Claude API |

---

## Структура проекта

```
jarvis/
├── CMakeLists.txt          # Корневой CMake
├── build.bat               # Скрипт сборки
├── installer.iss           # Inno Setup (установщик)
├── app/                    # EXE — тонкий лаунчер
│   ├── main.cpp
│   └── mainwindow.*
├── core/                   # DLL — ядро (обновляется отдельно)
│   ├── jarvis.*            # Главный класс + PluginHost
│   ├── claude_api.*        # Anthropic API клиент
│   ├── embedded_key.h      # Вшитый API-ключ
│   ├── session_memory.*    # Контекстная память
│   ├── action_predictor.*  # Предугадывание действий
│   ├── plugin_*.*          # Плагинная система
│   ├── updater.*           # Автообновление из GitHub
│   └── ...
├── plugins/                # DLL-плагины
│   └── plugins.json
├── scripts/
│   ├── generate_key.bat    # Генератор зашифрованного ключа
│   ├── prepare_release.bat # Подготовка пакета
│   └── update.bat          # Обновление без пересборки
├── assets/                 # Иконки для установщика
└── .github/workflows/
    └── build.yml           # CI/CD: автосборка + релизы
```

---

## Лицензия

MIT — см. [LICENSE](LICENSE)
