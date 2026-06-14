# J.A.R.V.I.S. — Personal AI Assistant

> Just A Rather Very Intelligent System  
> Windows desktop AI assistant — voice control, code help, screen vision, self-learning.

---

## Quick Start

1. **Launch** JARVIS from Desktop or Start Menu
2. **Type or say** any command — no setup needed for basic use
3. **For AI features** — add an API key in Settings (see below)

---

## Commands — No Internet Required

These work instantly, for free, without any API key:

| What to say | What happens |
|---|---|
| `open Steam` / `открой Steam` | Launches Steam |
| `open Chrome / Discord / CLion / Blender...` | Launches that app |
| `close Steam` / `закрой Steam` | Kills the process |
| `lock screen` / `заблокируй` | Locks Windows |
| `shutdown` / `выключи компьютер` | Shuts down PC |
| `restart` / `перезагрузи` | Restarts PC |
| `what time` / `который час` | Shows current time |
| `what date` / `какая дата` | Shows current date |
| `2+2` / `10*5` | Math instantly |
| `what can you do` / `что ты умеешь` | Full command list |
| `hello` / `привет` | Greeting |

**Volume & brightness** (SystemController):

| Command | Action |
|---|---|
| `volume 70` / `громкость 70` | Set to 70% |
| `volume up` / `громче` | +10% |
| `mute` / `выключи звук` | Mute |
| `brightness up` / `яркость выше` | +10% |
| `night mode on` / `ночной режим` | Enable Night Light |

**File search:**

| Command | What it searches |
|---|---|
| `find file readme` | Files on your PC |
| `find readme in project` | Project source files |
| `find readme in browser history` | Browser history |
| `find readme online` | Opens Google |

---

## Commands — Requires API Key

### AI questions & coding (Claude / Gemini / Ollama)

| Command | What happens |
|---|---|
| Any question | Answered by AI |
| `fix bug in main.cpp` | AI reviews the file |
| `add function X` | AI writes it, JARVIS applies to file |
| `refactor class Y` | AI refactors, JARVIS saves diff |
| `create file Z` | AI creates, JARVIS writes to disk |

### Visual commands (requires Claude API)

| Command | What happens |
|---|---|
| `what do you see` / `что видишь` | Screenshot → AI describes screen |
| `click on OK` / `кликни на ОК` | OCR finds "OK" on screen → clicks |
| `describe screen` / `опиши экран` | Full screen analysis |

### Self-learning (automatic)

After any AI response, JARVIS extracts executable steps and saves them.  
**Next time you give the same command — it runs locally, no API needed.**  
Learned commands are stored in `%AppData%\Jarvis\learned_commands.json`.

---

## Setting Up API Keys

Open **Settings** menu inside JARVIS:

### Claude API (Anthropic) — best quality
1. Go to [console.anthropic.com](https://console.anthropic.com)
2. Create account → API Keys → Create Key
3. In JARVIS: **Settings → Claude API key...**
4. Paste key, press OK
- Cost: ~$1–3/month for typical use. First $5 free.

### Gemini API (Google) — free
1. Go to [aistudio.google.com](https://aistudio.google.com)
2. Sign in → Get API key (completely free)
3. In JARVIS: **Settings → Gemini API key...**
- Cost: **Free** with generous limits.

### Ollama — fully offline AI
1. Download from [ollama.com](https://ollama.com)
2. Install and run: `ollama serve`
3. Pull a model: `ollama pull llama3`
4. In JARVIS: **Settings → Ollama model...**  
   Enable: **Settings → Agent mode** (checks Ollama automatically)
- Cost: **Free**, runs locally, no internet needed.

---

## Project Indexing (for code work)

To let JARVIS understand your codebase:

1. **Project → Index folder...**
2. Select your project root (e.g. `C:\Projects\MyGame`)
3. JARVIS scans all `.cpp`, `.h`, `.py` files and builds an index
4. Now you can say: `fix bug in PlayerController.cpp` — JARVIS finds the file and sends relevant code to AI

---

## Troubleshooting

### "Could not find application: X"
- Use the English or full name: `open Google Chrome` not `open гугл`
- Or the Russian alias: `открой Chrome` works too
- JARVIS knows: Steam, Chrome, Firefox, Brave, Edge, Notepad, Calculator, Explorer, VS Code, CLion, Rider, Telegram, Discord, OBS, VLC, Blender, Krita, DaVinci Resolve, Unreal Engine, Word, Excel, Task Manager, Settings, and more

### "ERROR: Host api.anthropic.com not found"
- No internet connection, or DNS issue
- JARVIS continues working for local commands — only AI answers are unavailable
- Check your connection, then try again

### AI gives wrong language (responds in English when you type Russian)
- JARVIS auto-detects language per message
- If still wrong: type a clear Russian phrase — detection resets automatically

### Antivirus blocks JARVIS
- Add the installation folder to antivirus exclusions
- JARVIS uses `SendInput` for keyboard emulation and `ShellExecuteW` for launching apps — these may trigger heuristic detection

### Ollama not responding
- Make sure Ollama is running: open Terminal → `ollama serve`
- Check the model is installed: `ollama list`
- In JARVIS: Settings → Agent mode (it pings Ollama and shows status)

### Screen vision / "click on X" not working
- Requires Claude API key (Vision uses Claude's image analysis)
- Make sure the target text is clearly visible on screen
- OCR works best with high-contrast text

### "No response" after sending a message
- Check Settings → the active backend has a valid key
- Try switching: Settings → Agent mode OFF → use Claude directly

---

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| `Enter` | Send message |
| `Ctrl+O` | Attach file |
| `Esc` | Close clarification panel / focus input |
| `⌨ button` | Toggle virtual keyboard |
| `🎤 button` | Toggle voice input (Whisper) |

---

## Voice Input (Whisper)

JARVIS uses [whisper.cpp](https://github.com/ggerganov/whisper.cpp) — a fully offline, free speech recognition engine by OpenAI.

**How it works:**
1. Click the **🎤** button in the input bar
2. Say **"Jarvis"** (wake word) — JARVIS activates
3. Speak your command — it gets transcribed and sent automatically
4. Click **🔴** again to stop listening

**Whisper features:**
- 🤫 Works with whisper-level speech (quiet voice detection at -45 dB)
- 🌍 Auto-detects Russian / English — no switching needed
- 💻 Runs 100% locally — no internet, no API key, no cost
- ⚡ Uses your CPU (AVX2 optimized), GPU optional

**Setup** (one time):
```
redist/whisper/ggml-medium.bin   ← ~1.5 GB model file
```
Download from: https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin

| Model | Size | Quality | Speed |
|---|---|---|---|
| ggml-tiny.bin | 75 MB | basic | instant |
| ggml-small.bin | 466 MB | good | ~1s |
| **ggml-medium.bin** | **1.5 GB** | **best for whisper** | **~2-3s** |

---

## File Attachments

- Click **📎** button or drag files into the window
- Supported: `.txt`, `.md`, `.cpp`, `.h`, `.py`, `.pdf`, `.docx`, `.xlsx`, `.pptx`, images
- Files are sent as context with your next message
- JARVIS reads and analyzes file contents automatically

---

## Reporting Bugs

Found a bug or something unexpected?  
**Help → Report a Bug** — type what happened, optionally attach a screenshot, and it goes straight to the developer via Telegram.

Or open an issue on GitHub: [github.com/Bohdan99py/jarvis/issues](https://github.com/Bohdan99py/jarvis/issues)

---

<!-- CHANGELOG_START -->

## Что нового / What's New

> Обновляется автоматически при каждом пуше в `main` через GitHub Actions.

### 🏷️ Последние релизы
- **v3.0.0** — 2026-06-14

### ✨ v3.0 — Крупное обновление

**🗄️ База данных (SQLite)**
- Вся история чатов, команды, память и настройки теперь хранятся в `jarvis.db`
- Таблицы: `chat_history`, `commands`, `memory_kv`, `settings`, `indexed_files`, `behavior_patterns`
- WAL режим, thread-safe соединения, система миграций без потери данных
- Счётчик токенов по модели за месяц (`monthlyTokens`)

**📚 Фоновое обучение**
- `BackgroundLearner` — индексирует `.cpp/.h/.py` файлы игрового проекта в фоне
- Анализирует историю чатов → извлекает паттерны поведения пользователя
- Запускается автоматически через 3 сек после старта, повторяет каждые 30 мин
- Работает в `QThread::LowPriority` — не влияет на UI и голос

**🎤 Голосовой ввод (Whisper.cpp)**
- Кнопка 🎤 в панели ввода
- Распознавание шёпота (VAD порог -45 dB, `no_speech_thold=0.4`)
- Автоопределение языка RU/EN без переключения
- Wake word «Джарвис» / «Jarvis»
- 100% офлайн, бесплатно, на CPU с AVX2 оптимизацией

📋 [Полная история релизов](https://github.com/Bohdan99py/jarvis/releases)

<!-- CHANGELOG_END -->

---

## Tech Stack

C++17 · Qt 6.11 · CMake/Ninja · MSVC  
Claude API (Anthropic) · Gemini API (Google) · Ollama (local LLM)  
**Whisper.cpp** (offline speech recognition) · Windows SAPI (TTS) · SendInput · ShellExecuteW  
Tesseract OCR · Poppler PDF · **SQLite** (chat history, memory, commands)  
**BackgroundLearner** (RAG indexing · behavior patterns)  
Inno Setup · GitHub Actions CI/CD

---

## License

MIT — see [LICENSE](LICENSE)
