#include "awarpBackendConfig.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "logger.h"

namespace
{
    Logger logger("AwarpBackendConfig");

    constexpr QLatin1String kResourcePath(":/awarp/warp_bootstrap.json");

    struct Data
    {
        bool loaded = false;
        bool valid = false;

        bool bootstrapEnabled = false;
        QString bootstrapUrl;
        QString bootstrapSecretHeader;
        QString bootstrapSecret;
        int bootstrapTimeoutMs = 7000;
        QVector<AwarpBackendConfig::Relay> relays;
        QString updateUrl;
    };

    Data &cache()
    {
        static Data data;
        return data;
    }

    void load()
    {
        Data &data = cache();
        if (data.loaded) {
            return;
        }
        data.loaded = true; // attempt only once, even on failure

        QFile file(kResourcePath);
        if (!file.open(QIODevice::ReadOnly)) {
            logger.error() << "Backend config resource not found:" << kResourcePath;
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        file.close();
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            logger.error() << "Backend config is not valid JSON:" << parseError.errorString();
            return;
        }

        const QJsonObject root = doc.object();
        const QJsonObject bootstrap = root.value("bootstrap").toObject();

        data.bootstrapEnabled = bootstrap.value("enabled").toBool(false);
        data.bootstrapUrl = bootstrap.value("url").toString();
        data.bootstrapSecretHeader = bootstrap.value("apiSecretHeader").toString(QStringLiteral("X-API-Secret"));
        data.bootstrapSecret = bootstrap.value("apiSecret").toString();
        data.bootstrapTimeoutMs = bootstrap.value("timeoutMs").toInt(7000);
        data.updateUrl = root.value("updateUrl").toString();

        const QJsonArray relaysArray = root.value("relays").toArray();
        for (const QJsonValue &value : relaysArray) {
            const QJsonObject relayObj = value.toObject();
            AwarpBackendConfig::Relay relay;
            relay.host = relayObj.value("host").toString();
            relay.port = relayObj.value("port").toInt();
            if (!relay.host.isEmpty() && relay.port > 0) {
                data.relays << relay;
            }
        }

        data.valid = true;

        // NB: the API secret is intentionally NEVER printed — log only whether
        // it is present, never its value.
        logger.info() << "backend config loaded, bootstrap enabled=" << data.bootstrapEnabled
                      << "url=" << (data.bootstrapUrl.isEmpty() ? QStringLiteral("<empty>") : data.bootstrapUrl)
                      << "secret=" << (data.bootstrapSecret.isEmpty() ? QStringLiteral("<none>") : QStringLiteral("<set>"))
                      << "relays=" << data.relays.size();
    }
} // namespace

namespace AwarpBackendConfig
{
    bool bootstrapEnabled()
    {
        load();
        return cache().bootstrapEnabled;
    }

    QString bootstrapUrl()
    {
        load();
        return cache().bootstrapUrl;
    }

    QString bootstrapSecretHeader()
    {
        load();
        return cache().bootstrapSecretHeader;
    }

    QString bootstrapSecret()
    {
        load();
        return cache().bootstrapSecret;
    }

    int bootstrapTimeoutMs()
    {
        load();
        return cache().bootstrapTimeoutMs;
    }

    QVector<Relay> relays()
    {
        load();
        return cache().relays;
    }

    QString updateUrl()
    {
        load();
        return cache().updateUrl;
    }
} // namespace AwarpBackendConfig
