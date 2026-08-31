// -------------------------------------------------------
// example_plugin.cpp — Рабочий пример плагина JARVIS (API v2)
//
// Показывает ровно то, ради чего API расширяли: плагин добавляет
// ИНСТРУМЕНТ, а не только ловит фразу. После загрузки hash_text
// виден модели, голосу, Ctrl+K, сценариям и триггерам наравне с
// инструментами ядра — и проходит те же разрешения.
//
// Собирается отдельной DLL и кладётся в bin/plugins. Ядро его не
// знает: связь только через plugin_interface.h и plugin_host.h.
// -------------------------------------------------------

#include "plugin_interface.h"
#include "plugin_host.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

class ExamplePlugin : public QObject, public JarvisPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID JarvisPlugin_iid FILE "example_plugin.json")
    Q_INTERFACES(JarvisPlugin)

public:
    QString name()        const override { return QStringLiteral("example_plugin"); }
    QString displayName() const override { return QStringLiteral("Пример: хеши"); }
    QString version()     const override { return QStringLiteral("1.0.0"); }
    int     apiVersion()  const override { return JARVIS_PLUGIN_API_VERSION; }

    bool initialize(PluginHost* host) override
    {
        if (!host)
            return false;
        m_host = host;

        // --- Схема аргументов: обычная JSON Schema, без типов ядра ---
        QJsonObject props;
        props[QStringLiteral("text")] = QJsonObject{
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("Text to hash") }
        };
        props[QStringLiteral("file")] = QJsonObject{
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("File to hash instead of text") }
        };
        props[QStringLiteral("algorithm")] = QJsonObject{
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("enum"), QJsonArray{ QStringLiteral("md5"),
                                                  QStringLiteral("sha1"),
                                                  QStringLiteral("sha256") } },
            { QStringLiteral("description"), QStringLiteral("Default sha256") }
        };

        QJsonObject schema;
        schema[QStringLiteral("type")]       = QStringLiteral("object");
        schema[QStringLiteral("properties")] = props;

        // Только чтение — RiskSafe, значит выполняется без подтверждения.
        const bool ok = host->registerTool(
            QStringLiteral("hash_text"),
            QStringLiteral("Compute an MD5, SHA-1 or SHA-256 hash of a string or a file. "
                           "Use it to compare a downloaded file against a published "
                           "checksum, or to check whether two files are identical."),
            schema,
            PluginHost::RiskSafe,
            [](const QJsonObject& args, bool& okOut) -> QString {
                return ExamplePlugin::hash(args, okOut);
            });

        if (!ok) {
            // Имя занято — это не повод молчать: инструмента не будет.
            host->postEvent(QStringLiteral("plugin"), 2,
                            QStringLiteral("example_plugin: не удалось зарегистрировать hash_text"));
            return false;
        }

        // Плагин может и по-старому — фразой. Одно другому не мешает.
        host->registerCommand({ QStringLiteral("хеш файла"), QStringLiteral("hash file") },
                              [](const QString& input) -> QString {
            QJsonObject args;
            args[QStringLiteral("file")] = input.section(QChar(' '), 2).trimmed();
            bool ok = false;
            return ExamplePlugin::hash(args, ok);
        },
                              QStringLiteral("Хеш файла: хеш файла <путь>"));

        host->postEvent(QStringLiteral("plugin"), 1,
                        QStringLiteral("Пример плагина загружен"),
                        QStringLiteral("добавлен инструмент hash_text"));
        return true;
    }

    void shutdown() override { m_host = nullptr; }

private:
    static QString hash(const QJsonObject& args, bool& ok)
    {
        ok = false;

        const QString algo = args.value(QStringLiteral("algorithm"))
                                 .toString(QStringLiteral("sha256")).toLower();
        QCryptographicHash::Algorithm method = QCryptographicHash::Sha256;
        if (algo == QLatin1String("md5"))  method = QCryptographicHash::Md5;
        if (algo == QLatin1String("sha1")) method = QCryptographicHash::Sha1;

        const QString file = args.value(QStringLiteral("file")).toString().trimmed();
        if (!file.isEmpty()) {
            QFile f(file);
            if (!f.open(QIODevice::ReadOnly))
                return QStringLiteral("Cannot read %1: %2").arg(file, f.errorString());

            QCryptographicHash hasher(method);
            if (!hasher.addData(&f))
                return QStringLiteral("Failed to read %1").arg(file);

            ok = true;
            return QStringLiteral("%1(%2) = %3")
                .arg(algo, QFileInfo(file).fileName(),
                     QString::fromLatin1(hasher.result().toHex()));
        }

        const QString text = args.value(QStringLiteral("text")).toString();
        if (text.isEmpty())
            return QStringLiteral("Pass either 'text' or 'file'.");

        ok = true;
        return QStringLiteral("%1 = %2")
            .arg(algo, QString::fromLatin1(
                     QCryptographicHash::hash(text.toUtf8(), method).toHex()));
    }

    PluginHost* m_host = nullptr;
};

#include "example_plugin.moc"
