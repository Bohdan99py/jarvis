#pragma once
#include <QString>
#include <QProcess>
#include <QRegularExpression>
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <wrl/client.h>

// Управление системными настройками Windows:
//   - Громкость (WinAPI IAudioEndpointVolume)
//   - Яркость экрана (WMI через PowerShell)
//   - Wi-Fi (netsh)
//   - Режим питания
//   - Ночной режим (Night Light)
class SystemController {
public:
    struct Result {
        bool    success = false;
        QString message;    // для отображения пользователю
    };

    // ── ГРОМКОСТЬ ──────────────────────────────────────────────────────────

    // Установить громкость [0..100]
    static Result setVolume(int percent) {
        percent = qBound(0, percent, 100);
        auto ep = getEndpointVolume();
        if (!ep) return {false, "Не удалось получить доступ к аудио устройству"};
        float vol = percent / 100.0f;
        HRESULT hr = ep->SetMasterVolumeLevelScalar(vol, nullptr);
        if (FAILED(hr)) return {false, "Ошибка установки громкости"};
        return {true, QString("Громкость установлена: %1%").arg(percent)};
    }

    // Получить текущую громкость [0..100], -1 при ошибке
    static int getVolume() {
        auto ep = getEndpointVolume();
        if (!ep) return -1;
        float vol = 0.0f;
        ep->GetMasterVolumeLevelScalar(&vol);
        return static_cast<int>(vol * 100.0f + 0.5f);
    }

    // Изменить громкость на delta (может быть отрицательным)
    static Result changeVolume(int delta) {
        int current = getVolume();
        if (current < 0) return {false, "Не удалось получить текущую громкость"};
        return setVolume(current + delta);
    }

    // Включить/выключить звук
    static Result setMute(bool mute) {
        auto ep = getEndpointVolume();
        if (!ep) return {false, "Не удалось получить доступ к аудио устройству"};
        ep->SetMute(mute ? TRUE : FALSE, nullptr);
        return {true, mute ? "Звук выключен" : "Звук включён"};
    }

    static Result toggleMute() {
        auto ep = getEndpointVolume();
        if (!ep) return {false, "Не удалось получить доступ к аудио устройству"};
        BOOL isMuted = FALSE;
        ep->GetMute(&isMuted);
        return setMute(!isMuted);
    }

    // ── ЯРКОСТЬ ────────────────────────────────────────────────────────────

    // Установить яркость [0..100] через WMI (PowerShell)
    // Работает для большинства ноутбуков с встроенным дисплеем
    static Result setBrightness(int percent) {
        percent = qBound(0, percent, 100);
        QString cmd = QString(
            "(Get-WmiObject -Namespace root/WMI -Class WmiMonitorBrightnessMethods)"
            ".WmiSetBrightness(1, %1)").arg(percent);
        Result r = runPowerShell(cmd);
        if (r.success) r.message = QString("Яркость установлена: %1%").arg(percent);
        return r;
    }

    // Получить текущую яркость [-1 при ошибке]
    static int getBrightness() {
        QString cmd =
            "(Get-WmiObject -Namespace root/WMI -Class WmiMonitorBrightness).CurrentBrightness";
        QProcess p;
        p.start("powershell.exe", {"-NoProfile", "-Command", cmd});
        if (!p.waitForFinished(3000)) return -1;
        bool ok = false;
        int val = p.readAllStandardOutput().trimmed().toInt(&ok);
        return ok ? val : -1;
    }

    // Изменить яркость на delta
    static Result changeBrightness(int delta) {
        int current = getBrightness();
        if (current < 0) {
            // WMI не сработал — попробуем через nircmd (если установлен)
            return changeBrightnessNircmd(delta);
        }
        return setBrightness(current + delta);
    }

    // ── WI-FI / СЕТЬ ──────────────────────────────────────────────────────

    static Result setWifi(bool enable) {
        QString action = enable ? "enable" : "disable";
        return runCmd(QString("netsh interface set interface \"Wi-Fi\" %1").arg(action));
    }

    static Result setAirplaneMode(bool enable) {
        // Переключение через реестр + радио-менеджер
        QString cmd = enable
            ? "Set-ItemProperty -Path 'HKLM:\\SYSTEM\\CurrentControlSet\\Control\\RadioManagement\\SystemRadioState' -Name 'Value' -Value 1"
            : "Set-ItemProperty -Path 'HKLM:\\SYSTEM\\CurrentControlSet\\Control\\RadioManagement\\SystemRadioState' -Name 'Value' -Value 0";
        return runPowerShell(cmd);
    }

    // ── ПИТАНИЕ ───────────────────────────────────────────────────────────

    static Result sleep()    { return runCmd("rundll32.exe powrprof.dll,SetSuspendState 0,1,0"); }
    static Result shutdown() { return runCmd("shutdown /s /t 0"); }
    static Result restart()  { return runCmd("shutdown /r /t 0"); }
    static Result lockScreen() {
        LockWorkStation();
        return {true, "Экран заблокирован"};
    }

    // ── НОЧНОЙ РЕЖИМ ──────────────────────────────────────────────────────

    static Result setNightLight(bool enable) {
        // Ночной режим Windows через PowerShell + реестр
        QString cmd = enable
            ? "$data = [byte[]] (0x43,0x42,0x01,0x00,0x0A,0x02,0x01,0x00); "
              "Set-ItemProperty -Path 'HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\CloudStore\\Store\\DefaultAccount\\Current\\default$windows.data.bluelightreduction.bluelightreductionstate\\windows.data.bluelightreduction.bluelightreductionstate' "
              "-Name Data -Value $data"
            : "Remove-ItemProperty -Path 'HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\CloudStore\\Store\\DefaultAccount\\Current\\default$windows.data.bluelightreduction.bluelightreductionstate\\windows.data.bluelightreduction.bluelightreductionstate' "
              "-Name Data -ErrorAction SilentlyContinue";
        Result r = runPowerShell(cmd);
        r.message = enable ? "Ночной режим включён" : "Ночной режим выключен";
        return r;
    }

    // ── ПАРСЕР КОМАНД ─────────────────────────────────────────────────────

    // Разобрать текст команды и выполнить нужное действие.
    // Возвращает {true, ...} если команда распознана и выполнена.
    // Возвращает {false, ""} если команда не системная (пусть идёт дальше).
    static Result tryExecuteSystemCommand(const QString &input) {
        QString text = input.toLower().trimmed();

        // ── Громкость ──
        if (text.contains("громкость") || text.contains("звук") ||
            text.contains("volume") || text.contains("sound")) {

            if (text.contains("выключ") || text.contains("mute") || text.contains("тихо")) {
                return setMute(true);
            }
            if (text.contains("включ") || text.contains("unmute")) {
                return setMute(false);
            }
            if (text.contains("громче") || text.contains("увелич") || text.contains("up")) {
                int delta = extractNumber(text, 10);
                return changeVolume(delta);
            }
            if (text.contains("тише") || text.contains("уменьш") || text.contains("down")) {
                int delta = extractNumber(text, 10);
                return changeVolume(-delta);
            }
            // "Громкость 70%"
            int val = extractNumber(text, -1);
            if (val >= 0) return setVolume(val);

            // Показать текущую
            int cur = getVolume();
            return {true, cur >= 0 ? QString("Текущая громкость: %1%").arg(cur) : "Не удалось получить громкость"};
        }

        // ── Яркость ──
        if (text.contains("яркость") || text.contains("brightness") || text.contains("экран ярче") || text.contains("экран темнее")) {
            if (text.contains("увелич") || text.contains("ярче") || text.contains("up")) {
                int delta = extractNumber(text, 10);
                return changeBrightness(delta);
            }
            if (text.contains("уменьш") || text.contains("темнее") || text.contains("down")) {
                int delta = extractNumber(text, 10);
                return changeBrightness(-delta);
            }
            int val = extractNumber(text, -1);
            if (val >= 0) return setBrightness(val);

            int cur = getBrightness();
            return {true, cur >= 0 ? QString("Текущая яркость: %1%").arg(cur) : "Яркость недоступна (внешний монитор?)"};
        }

        // ── Ночной режим ──
        if (text.contains("ночной режим") || text.contains("night light") || text.contains("night mode")) {
            bool enable = !text.contains("выключ") && !text.contains("off") && !text.contains("откл");
            return setNightLight(enable);
        }

        // ── Wi-Fi ──
        if (text.contains("wi-fi") || text.contains("wifi") || text.contains("вай-фай") || text.contains("вайфай")) {
            bool enable = !text.contains("выключ") && !text.contains("off") && !text.contains("откл");
            return setWifi(enable);
        }

        // ── Блокировка ──
        if (text.contains("заблокир") || text.contains("lock")) {
            return lockScreen();
        }

        // ── Сон ──
        if (text.contains("режим сна") || text.contains("sleep mode") || text.contains("спящий")) {
            return sleep();
        }

        // Не системная команда
        return {false, {}};
    }

private:
    // Получить IAudioEndpointVolume (COM)
    static Microsoft::WRL::ComPtr<IAudioEndpointVolume> getEndpointVolume() {
        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                      CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        if (FAILED(hr)) return nullptr;

        Microsoft::WRL::ComPtr<IMMDevice> device;
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr)) return nullptr;

        Microsoft::WRL::ComPtr<IAudioEndpointVolume> volume;
        hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL,
                               nullptr, &volume);
        if (FAILED(hr)) return nullptr;
        return volume;
    }

    static Result runPowerShell(const QString &command) {
        QProcess p;
        p.start("powershell.exe", {"-NoProfile", "-Command", command});
        if (!p.waitForFinished(5000))
            return {false, "PowerShell timeout"};
        int exitCode = p.exitCode();
        if (exitCode != 0) {
            QString err = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
            return {false, err.isEmpty() ? QString("Exit code: %1").arg(exitCode) : err};
        }
        return {true, {}};
    }

    static Result runCmd(const QString &command) {
        QProcess p;
        p.start("cmd.exe", {"/c", command});
        if (!p.waitForFinished(5000))
            return {false, "cmd timeout"};
        return {p.exitCode() == 0, {}};
    }

    static Result changeBrightnessNircmd(int delta) {
        // nircmd.exe changedisplaybrightness <delta>  (если установлен)
        QProcess p;
        p.start("nircmd.exe", {"changedisplaybrightness", QString::number(delta)});
        if (!p.waitForFinished(3000))
            return {false, "nircmd не найден. Установи nircmd для управления яркостью."};
        return {true, QString("Яркость изменена на %1").arg(delta > 0 ? QString("+%1").arg(delta) : QString::number(delta))};
    }

    // Извлечь первое число из строки. Если нет — вернуть defaultVal.
    static int extractNumber(const QString &text, int defaultVal) {
        static const QRegularExpression re("(\\d+)");
        QRegularExpressionMatch m = re.match(text);
        if (m.hasMatch()) return m.captured(1).toInt();
        return defaultVal;
    }
};