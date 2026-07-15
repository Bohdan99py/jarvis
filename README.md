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

## Skills — Modular Knowledge (Lego blocks)

JARVIS is modular: expert knowledge lives in **skills** you can enable,
disable, or add later — so everyone gets a personalized assistant.

| Skill | What it adds | Default |
|---|---|---|
| 🧩 **Programmer (IDE agent)** | Vibecoding: JARVIS writes/edits files in your project, runs commands, opens IDE | On |
| 🧩 **Electronics Engineer** | KiCad, circuit design, embedded firmware expertise | On |
| 🧩 **Philosopher** | Deep conversations, ethics, logic | Off |

- **Manage:** Settings → **🧩 JARVIS Skills...** — toggle on/off, applies instantly (no restart).
- **During install:** the installer lets you pick which skills to install (e.g. skip the Programmer skill if you don't code — JARVIS still works normally as an assistant).
- **Add later:** Settings → Skills → **Import skill...**, or drop a skill folder into `Documents\Jarvis Data\skills\`.
- **Make your own:** a skill is just a folder with `skill.json` (manifest) + `prompt.md` (knowledge) — no programming needed. See `skills/README.md`.

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



## License

MIT — see [LICENSE](LICENSE)
