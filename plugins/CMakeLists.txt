## -------------------------------------------------------
## plugins/CMakeLists.txt — Плагины J.A.R.V.I.S.
##
## Каждый плагин — отдельная DLL. Добавляйте новые
## плагины как add_subdirectory().
## -------------------------------------------------------

# Рабочий пример: регистрирует инструмент hash_text через API v2.
# Он же — проверка того, что граница ядро/плагин действительно
# работает: если пример перестал собираться, значит из интерфейса
# уехало что-то, чего плагины видеть не должны.
add_subdirectory(example_plugin)

# Будущие плагины:
# add_subdirectory(whisper_stt)        # Распознавание речи через Whisper
# add_subdirectory(gemini_api)         # Gemini API для чата
# add_subdirectory(ue5_remote)         # UE5 Remote Control
# add_subdirectory(screen_capture)     # Захват экрана
# add_subdirectory(telegram_bot)       # Telegram уведомления
