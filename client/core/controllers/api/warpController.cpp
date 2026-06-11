#include "warpController.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

#include "amneziaApplication.h"
#include "logger.h"
#include "core/configurators/wireguardConfigurator.h"
#include "core/controllers/selfhosted/importController.h"
#include "core/models/containerConfig.h"
#include "core/models/protocols/awgProtocolConfig.h"
#include "core/utils/containerEnum.h"
#include "core/utils/containers/containerUtils.h"
#include "core/utils/constants/configKeys.h"
#include "core/utils/constants/protocolConstants.h"
#include "core/utils/constants/warpConstants.h"
#include "core/utils/serverConfigUtils.h"

using namespace amnezia;

namespace
{
    Logger logger("WarpController");

    constexpr QLatin1String kRegEndpoint("reg");

    QString withCidr(const QString &address, const QString &cidr)
    {
        if (address.isEmpty() || address.contains('/')) {
            return address;
        }
        return address + "/" + cidr;
    }

    QJsonObject unwrapResult(const QJsonObject &response)
    {
        if (response.value("result").isObject()) {
            return response.value("result").toObject();
        }
        return response;
    }

    void replaceConfigTextValue(QString &configText, const QString &key, const QString &value)
    {
        const QRegularExpression lineRegExp(QStringLiteral("^%1 = .*$").arg(key), QRegularExpression::MultilineOption);
        configText.replace(lineRegExp, QStringLiteral("%1 = %2").arg(key, value));
    }
} // namespace

QString WarpController::WarpSession::addresses() const
{
    QStringList addressList;
    if (!addressV4.isEmpty()) {
        addressList << withCidr(addressV4, "32");
    }
    if (!addressV6.isEmpty()) {
        addressList << withCidr(addressV6, "128");
    }
    return addressList.join(", ");
}

WarpController::WarpController(SecureServersRepository *serversRepository,
                               SecureAppSettingsRepository *appSettingsRepository,
                               ImportController *importController, QObject *parent)
    : QObject(parent),
      m_serversRepository(serversRepository),
      m_appSettingsRepository(appSettingsRepository),
      m_importController(importController)
{
    // hasConfig depends on the stored servers, so any add/remove may change it
    connect(m_serversRepository, &SecureServersRepository::serverAdded, this, &WarpController::hasConfigChanged);
    connect(m_serversRepository, &SecureServersRepository::serverRemoved, this, &WarpController::hasConfigChanged);
}

bool WarpController::hasConfig() const
{
    return !findWarpServerId().isEmpty();
}

bool WarpController::isBusy() const
{
    return m_isBusy;
}

void WarpController::fetchNewConfig()
{
    if (m_isBusy) {
        return;
    }
    setBusy(true);

    registerAccount([this](bool ok, const WarpSession &session) {
        if (!ok) {
            return;
        }
        importNewConfig(session);
    });
}

void WarpController::refreshConfig()
{
    const QString serverId = findWarpServerId();
    if (serverId.isEmpty()) {
        fetchNewConfig();
        return;
    }

    if (m_isBusy) {
        return;
    }
    setBusy(true);

    registerAccount([this, serverId](bool ok, const WarpSession &session) {
        if (!ok) {
            return;
        }
        updateExistingConfig(serverId, session);
    });
}

void WarpController::registerAccount(const std::function<void(bool ok, const WarpSession &session)> &onDone)
{
    const auto keys = WireguardConfigurator::genClientKeys();
    if (keys.clientPrivKey.isEmpty() || keys.clientPubKey.isEmpty()) {
        fail(tr("Failed to generate WireGuard keys"));
        onDone(false, {});
        return;
    }

    QJsonObject regBody;
    regBody["install_id"] = "";
    regBody["tos"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    regBody["key"] = keys.clientPubKey;
    regBody["fcm_token"] = "";
    regBody["type"] = "ios";
    regBody["locale"] = "en_US";

    sendRequestAsync("POST", kRegEndpoint, regBody, QString(),
                     [this, keys, onDone](bool ok, const QJsonObject &response, const QString &errorMessage) {
        if (!ok) {
            fail(tr("Cloudflare WARP registration failed: %1").arg(errorMessage));
            onDone(false, {});
            return;
        }

        const QJsonObject account = unwrapResult(response);
        const QString accountId = account.value("id").toString();
        const QString token = account.value("token").toString();
        if (accountId.isEmpty() || token.isEmpty()) {
            fail(tr("Cloudflare WARP registration failed: unexpected response format"));
            onDone(false, {});
            return;
        }

        QJsonObject patchBody;
        patchBody["warp_enabled"] = true;

        sendRequestAsync("PATCH", QStringLiteral("%1/%2").arg(kRegEndpoint, accountId), patchBody, token,
                         [this, keys, onDone](bool ok, const QJsonObject &response, const QString &errorMessage) {
            if (!ok) {
                fail(tr("Failed to enable WARP for the registered account: %1").arg(errorMessage));
                onDone(false, {});
                return;
            }

            const QJsonObject config = unwrapResult(response).value("config").toObject();
            const QJsonArray peers = config.value("peers").toArray();
            const QJsonObject addresses = config.value("interface").toObject().value("addresses").toObject();

            WarpSession session;
            session.clientPrivateKey = keys.clientPrivKey;
            session.clientPublicKey = keys.clientPubKey;
            session.peerPublicKey = peers.isEmpty() ? QString() : peers.at(0).toObject().value("public_key").toString();
            session.addressV4 = addresses.value("v4").toString();
            session.addressV6 = addresses.value("v6").toString();

            if (session.peerPublicKey.isEmpty() || (session.addressV4.isEmpty() && session.addressV6.isEmpty())) {
                fail(tr("Cloudflare WARP returned an incomplete configuration"));
                onDone(false, {});
                return;
            }

            onDone(true, session);
        });
    });
}

void WarpController::sendRequestAsync(const QByteArray &verb, const QString &endpoint, const QJsonObject &body,
                                      const QString &bearerToken,
                                      const std::function<void(bool ok, const QJsonObject &response, const QString &errorMessage)> &onDone)
{
    QNetworkRequest request;
    request.setTransferTimeout(protocols::warp::requestTimeoutMsecs);
    request.setUrl(QUrl(QStringLiteral("%1/%2").arg(protocols::warp::apiBaseUrl, endpoint)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("User-Agent", protocols::warp::userAgent);
    if (!bearerToken.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + bearerToken.toUtf8());
    }

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = amnApp->networkManager()->sendCustomRequest(request, verb, payload);

    connect(reply, &QNetworkReply::finished, this, [reply, endpoint, onDone]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            logger.error() << "Request to" << endpoint << "failed:" << reply->errorString();
            onDone(false, {}, reply->errorString());
            return;
        }

        const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
        if (response.isEmpty()) {
            logger.error() << "Request to" << endpoint << "returned an invalid JSON response";
            onDone(false, {}, tr("invalid response from the server"));
            return;
        }

        onDone(true, response, QString());
    });
}

void WarpController::importNewConfig(const WarpSession &session)
{
    const QString previousServerId = findWarpServerId();

    auto importResult = m_importController->extractConfigFromData(buildConfigText(session));
    if (importResult.errorCode != ErrorCode::NoError || importResult.config.isEmpty()) {
        fail(tr("Failed to process the generated WARP configuration"));
        return;
    }

    importResult.config[configKey::description] = QStringLiteral("WARP");
    m_importController->importConfig(importResult.config);

    if (!previousServerId.isEmpty()) {
        m_serversRepository->removeServer(previousServerId);
    }

    logger.info() << "New WARP config imported";
    setBusy(false);
    emit configReady();
}

void WarpController::updateExistingConfig(const QString &serverId, const WarpSession &session)
{
    auto serverConfig = m_serversRepository->nativeConfig(serverId);
    if (!serverConfig.has_value()) {
        fail(tr("Failed to read the saved WARP configuration"));
        return;
    }

    const DockerContainer container = serverConfig->defaultContainer;
    ContainerConfig containerConfig = serverConfig->containerConfig(container);
    AwgProtocolConfig *awgConfig = containerConfig.getAwgProtocolConfig();
    if (!awgConfig || !awgConfig->hasClientConfig()) {
        fail(tr("Failed to read the saved WARP configuration"));
        return;
    }

    // Replace only the Cloudflare session fields, keeping all user overrides intact
    AwgClientConfig clientConfig = awgConfig->clientConfig.value();
    clientConfig.clientPrivateKey = session.clientPrivateKey;
    clientConfig.clientPublicKey = session.clientPublicKey;
    clientConfig.serverPublicKey = session.peerPublicKey;
    clientConfig.clientIp = session.addresses();

    replaceConfigTextValue(clientConfig.nativeConfig, protocols::wireguard::PrivateKey, session.clientPrivateKey);
    replaceConfigTextValue(clientConfig.nativeConfig, protocols::wireguard::Address, session.addresses());
    replaceConfigTextValue(clientConfig.nativeConfig, protocols::wireguard::PublicKey, session.peerPublicKey);

    awgConfig->setClientConfig(clientConfig);
    serverConfig->updateContainerConfig(container, containerConfig);
    m_serversRepository->editServer(serverId, serverConfig->toJson(), serverConfigUtils::ConfigType::Native);

    logger.info() << "WARP config refreshed";
    setBusy(false);
    emit configUpdated();
}

QString WarpController::getConfigJson() const
{
    const QString serverId = findWarpServerId();
    if (serverId.isEmpty()) {
        return QString();
    }

    const auto serverConfig = m_serversRepository->nativeConfig(serverId);
    if (!serverConfig.has_value()) {
        return QString();
    }

    const ContainerConfig containerConfig = serverConfig->containerConfig(serverConfig->defaultContainer);
    const auto *awgConfig = containerConfig.getAwgProtocolConfig();
    if (!awgConfig || !awgConfig->hasClientConfig()) {
        return QString();
    }

    return QString::fromUtf8(QJsonDocument(awgConfig->clientConfig->toJson()).toJson());
}

bool WarpController::saveConfig(const QString &configJson)
{
    const QString serverId = findWarpServerId();
    if (serverId.isEmpty()) {
        return false;
    }

    const QJsonObject clientConfigJson = QJsonDocument::fromJson(configJson.toUtf8()).object();
    if (clientConfigJson.isEmpty()) {
        emit errorOccurred(tr("Invalid WARP configuration format"));
        return false;
    }

    auto serverConfig = m_serversRepository->nativeConfig(serverId);
    if (!serverConfig.has_value()) {
        return false;
    }

    const DockerContainer container = serverConfig->defaultContainer;
    ContainerConfig containerConfig = serverConfig->containerConfig(container);
    AwgProtocolConfig *awgConfig = containerConfig.getAwgProtocolConfig();
    if (!awgConfig) {
        return false;
    }

    const AwgClientConfig clientConfig = AwgClientConfig::fromJson(clientConfigJson);
    awgConfig->setClientConfig(clientConfig);
    if (clientConfig.port > 0) {
        awgConfig->serverConfig.port = QString::number(clientConfig.port);
    }

    if (!clientConfig.hostName.isEmpty()) {
        serverConfig->hostName = clientConfig.hostName;
    }
    serverConfig->updateContainerConfig(container, containerConfig);
    m_serversRepository->editServer(serverId, serverConfig->toJson(), serverConfigUtils::ConfigType::Native);

    emit configUpdated();
    return true;
}

QString WarpController::buildConfigText(const WarpSession &session) const
{
    QStringList lines;
    lines << QStringLiteral("[Interface]");
    lines << QStringLiteral("PrivateKey = %1").arg(session.clientPrivateKey);
    lines << QStringLiteral("Address = %1").arg(session.addresses());
    lines << QStringLiteral("DNS = %1").arg(protocols::warp::defaultDns);
    lines << QStringLiteral("MTU = %1").arg(protocols::warp::defaultMtu);
    lines << QStringLiteral("Jc = %1").arg(protocols::warp::defaultJunkPacketCount);
    lines << QStringLiteral("Jmin = %1").arg(protocols::warp::defaultJunkPacketMinSize);
    lines << QStringLiteral("Jmax = %1").arg(protocols::warp::defaultJunkPacketMaxSize);
    lines << QStringLiteral("S1 = %1").arg(protocols::warp::defaultInitPacketJunkSize);
    lines << QStringLiteral("S2 = %1").arg(protocols::warp::defaultResponsePacketJunkSize);
    lines << QStringLiteral("H1 = %1").arg(protocols::warp::defaultInitPacketMagicHeader);
    lines << QStringLiteral("H2 = %1").arg(protocols::warp::defaultResponsePacketMagicHeader);
    lines << QStringLiteral("H3 = %1").arg(protocols::warp::defaultUnderloadPacketMagicHeader);
    lines << QStringLiteral("H4 = %1").arg(protocols::warp::defaultTransportPacketMagicHeader);
    lines << QStringLiteral("I1 = %1").arg(protocols::warp::defaultSpecialJunk1);
    lines << QString();
    lines << QStringLiteral("[Peer]");
    lines << QStringLiteral("PublicKey = %1").arg(session.peerPublicKey);
    lines << QStringLiteral("AllowedIPs = %1").arg(protocols::warp::defaultAllowedIps);
    lines << QStringLiteral("Endpoint = %1:%2").arg(protocols::warp::endpointHost, protocols::warp::endpointPort);
    return lines.join("\n");
}

QString WarpController::findWarpServerId() const
{
    auto isWarpServer = [this](const QString &serverId) {
        if (m_serversRepository->serverKind(serverId) != serverConfigUtils::ConfigType::Native) {
            return false;
        }
        const auto serverConfig = m_serversRepository->nativeConfig(serverId);
        if (!serverConfig.has_value() || !ContainerUtils::isAwgContainer(serverConfig->defaultContainer)) {
            return false;
        }
        const ContainerConfig containerConfig = serverConfig->containerConfig(serverConfig->defaultContainer);
        const auto *awgConfig = containerConfig.getAwgProtocolConfig();
        return awgConfig && awgConfig->serverConfig.isThirdPartyConfig && awgConfig->hasClientConfig();
    };

    const QString defaultServerId = m_serversRepository->defaultServerId();
    if (!defaultServerId.isEmpty() && isWarpServer(defaultServerId)) {
        return defaultServerId;
    }

    const QVector<QString> serverIds = m_serversRepository->orderedServerIds();
    for (const QString &serverId : serverIds) {
        if (isWarpServer(serverId)) {
            return serverId;
        }
    }
    return QString();
}

void WarpController::setBusy(bool busy)
{
    if (m_isBusy == busy) {
        return;
    }
    m_isBusy = busy;
    emit busyChanged(busy);
}

void WarpController::fail(const QString &errorMessage)
{
    setBusy(false);
    emit errorOccurred(errorMessage);
}
