#include "warpController.h"

#include <QAbstractSocket>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
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
#include "core/utils/config/awarpBackendConfig.h" // AWARP
#include "core/utils/serverConfigUtils.h"
#include "warpScanner.h"

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

    QStringList splitCommaList(const QString &value)
    {
        QStringList result;
        const QStringList parts = value.split(',', Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const QString trimmed = part.trimmed();
            if (!trimmed.isEmpty()) {
                result << trimmed;
            }
        }
        return result;
    }

    // Keys of the QVariantMap exchanged with the QML config settings page
    namespace fieldKey
    {
        constexpr QLatin1String junkPacketCount("junkPacketCount");
        constexpr QLatin1String junkPacketMinSize("junkPacketMinSize");
        constexpr QLatin1String junkPacketMaxSize("junkPacketMaxSize");
        constexpr QLatin1String initPacketJunkSize("initPacketJunkSize");
        constexpr QLatin1String responsePacketJunkSize("responsePacketJunkSize");
        constexpr QLatin1String initPacketMagicHeader("initPacketMagicHeader");
        constexpr QLatin1String responsePacketMagicHeader("responsePacketMagicHeader");
        constexpr QLatin1String underloadPacketMagicHeader("underloadPacketMagicHeader");
        constexpr QLatin1String transportPacketMagicHeader("transportPacketMagicHeader");
        constexpr QLatin1String specialJunk1("specialJunk1");
        constexpr QLatin1String mtu("mtu");
        constexpr QLatin1String dns("dns");
        constexpr QLatin1String allowedIps("allowedIps");
        constexpr QLatin1String endpointHost("endpointHost");
        constexpr QLatin1String endpointPort("endpointPort");
        constexpr QLatin1String endpointAuto("endpointAuto");
        // read-only session fields
        constexpr QLatin1String clientIpV4("clientIpV4");
        constexpr QLatin1String clientIpV6("clientIpV6");
        constexpr QLatin1String peerPublicKey("peerPublicKey");
    } // namespace fieldKey
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
      m_importController(importController),
      m_scanner(new WarpScanner(this))
{
    // hasConfig depends on the stored servers, so any add/remove may change it
    connect(m_serversRepository, &SecureServersRepository::serverAdded, this, &WarpController::hasConfigChanged);
    connect(m_serversRepository, &SecureServersRepository::serverRemoved, this, &WarpController::hasConfigChanged);

    connect(m_scanner, &WarpScanner::scanFinished, this, [this](const QString &bestIp, int latencyMs) {
        logger.info() << "Endpoint scan picked" << bestIp << "(" << latencyMs << "ms)";
        m_scannedEndpointIp = bestIp;
        setScanning(false);
        // Apply only in auto mode; manual endpoints are never overwritten
        if (m_appSettingsRepository->isWarpEndpointAuto()) {
            const QString serverId = findWarpServerId();
            if (!serverId.isEmpty()) {
                applyScannedEndpoint(serverId, bestIp, protocols::warp::warpPortDefault);
            }
        }
    });
    connect(m_scanner, &WarpScanner::scanFailed, this, [this](const QString &errorMessage) {
        logger.warning() << "Endpoint scan failed:" << errorMessage;
        setScanning(false);
    });

    m_latencyTimer = new QTimer(this);
    m_latencyTimer->setInterval(8 * 1000); // light re-probe while connected
    connect(m_latencyTimer, &QTimer::timeout, this, [this]() { probeLatencyOnce(); });
}

bool WarpController::hasConfig() const
{
    return !findWarpServerId().isEmpty();
}

bool WarpController::isBusy() const
{
    return m_isBusy;
}

bool WarpController::isScanning() const
{
    return m_isScanning;
}

bool WarpController::endpointAuto() const
{
    return m_appSettingsRepository->isWarpEndpointAuto();
}

int WarpController::latencyMs() const
{
    return m_latencyMs;
}

void WarpController::fetchNewConfig()
{
    if (m_isBusy) {
        return;
    }
    setBusy(true);

    // Scan for the best endpoint in parallel with key registration.
    // The session is applied as soon as it arrives; the scanned endpoint is
    // applied when the scan finishes (only in auto mode).
    startEndpointScan();

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

    // Scan for the best endpoint in parallel with key registration (see fetchNewConfig)
    startEndpointScan();

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
        fail(tr("Не удалось сгенерировать ключи WireGuard"));
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
            // AWARP: direct Cloudflare registration failed — try the bootstrap fallback.
            logger.warning() << "WarpController: прямой запрос к Cloudflare не удался, пробую резервный сервис";
            registerAccountViaBootstrap([this, onDone](bool bootstrapOk, const WarpSession &session, const QString &bootstrapError) {
                if (bootstrapOk) {
                    onDone(true, session);
                    return;
                }
                fail(tr("Не удалось получить конфиг ни напрямую, ни через резервный сервис: %1").arg(bootstrapError));
                onDone(false, {});
            });
            return;
        }

        const QJsonObject account = unwrapResult(response);
        const QString accountId = account.value("id").toString();
        const QString token = account.value("token").toString();
        if (accountId.isEmpty() || token.isEmpty()) {
            fail(tr("Не удалось зарегистрироваться в Cloudflare WARP: неожиданный формат ответа"));
            onDone(false, {});
            return;
        }

        QJsonObject patchBody;
        patchBody["warp_enabled"] = true;

        sendRequestAsync("PATCH", QStringLiteral("%1/%2").arg(kRegEndpoint, accountId), patchBody, token,
                         [this, keys, onDone](bool ok, const QJsonObject &response, const QString &errorMessage) {
            if (!ok) {
                // AWARP: enabling WARP failed — try the bootstrap fallback.
                logger.warning() << "WarpController: прямой запрос к Cloudflare не удался, пробую резервный сервис";
                registerAccountViaBootstrap([this, onDone](bool bootstrapOk, const WarpSession &session, const QString &bootstrapError) {
                    if (bootstrapOk) {
                        onDone(true, session);
                        return;
                    }
                    fail(tr("Не удалось получить конфиг ни напрямую, ни через резервный сервис: %1").arg(bootstrapError));
                    onDone(false, {});
                });
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
                fail(tr("Cloudflare WARP вернул неполный конфиг"));
                onDone(false, {});
                return;
            }

            onDone(true, session);
        });
    });
}

void WarpController::registerAccountViaBootstrap(
        const std::function<void(bool ok, const WarpSession &session, const QString &errorMessage)> &onDone)
{
    if (!AwarpBackendConfig::bootstrapEnabled()) {
        onDone(false, {}, tr("резервный сервис отключён"));
        return;
    }

    const QString url = AwarpBackendConfig::bootstrapUrl();
    if (url.isEmpty()) {
        onDone(false, {}, tr("адрес резервного сервиса не задан"));
        return;
    }

    QNetworkRequest request;
    request.setTransferTimeout(AwarpBackendConfig::bootstrapTimeoutMs());
    request.setUrl(QUrl(url));
    request.setRawHeader(AwarpBackendConfig::bootstrapSecretHeader().toUtf8(),
                         AwarpBackendConfig::bootstrapSecret().toUtf8());

    sendWithIPv4(request, QStringLiteral("bootstrap"), [this, onDone](const QNetworkRequest &request) {
        QNetworkReply *reply = amnApp->networkManager()->get(request);

        connect(reply, &QNetworkReply::finished, this, [reply, onDone]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            // NB: never log the request (it carries the API secret in a header).
            logger.error() << "Bootstrap request failed:" << reply->errorString();
            onDone(false, {}, reply->errorString());
            return;
        }

        const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
        if (response.isEmpty() || !response.value("ok").toBool(false)) {
            const QString error = response.value("error").toString(tr("некорректный ответ резервного сервиса"));
            logger.error() << "Bootstrap response not ok:" << error;
            onDone(false, {}, error);
            return;
        }

        WarpSession session;
        session.clientPrivateKey = response.value("private_key").toString();
        session.clientPublicKey = QString(); // bootstrap does not return it
        session.peerPublicKey = response.value("peer_public_key").toString();
        // Addresses already carry a CIDR suffix; addresses() detects the '/'
        // and will not double-append via withCidr().
        session.addressV4 = response.value("client_ipv4").toString();
        session.addressV6 = response.value("client_ipv6").toString();

        if (session.clientPrivateKey.isEmpty() || session.peerPublicKey.isEmpty()
            || (session.addressV4.isEmpty() && session.addressV6.isEmpty())) {
            logger.error() << "Bootstrap returned an incomplete config";
            onDone(false, {}, tr("резервный сервис вернул неполный конфиг"));
            return;
        }

        logger.info() << "WarpController: конфиг получен через резервный сервис";
        onDone(true, session, QString());
        });
    });
}

void WarpController::sendWithIPv4(QNetworkRequest request, const QString &logTag,
                                  const std::function<void(const QNetworkRequest &request)> &send)
{
    const QUrl url = request.url();
    const QString host = url.host();

    // If the URL host is already a literal IP address, there is nothing to resolve.
    if (host.isEmpty() || !QHostAddress(host).isNull()) {
        send(request);
        return;
    }

    QHostInfo::lookupHost(host, this, [this, request, host, logTag, send](const QHostInfo &info) mutable {
        QHostAddress ipv4;
        if (info.error() == QHostInfo::NoError) {
            for (const QHostAddress &address : info.addresses()) {
                if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                    ipv4 = address;
                    break;
                }
            }
        }

        if (ipv4.isNull()) {
            // No A record (IPv6-only or lookup failed): send on the hostname as before.
            logger.warning() << "WarpController:" << logTag
                             << "— нет IPv4 (A) записи для" << host << ", отправляю по hostname";
            send(request);
            return;
        }

        // Connect by IPv4 while keeping the hostname for the HTTP Host header and
        // the TLS SNI / certificate verification.
        QUrl ipUrl = request.url();
        ipUrl.setHost(ipv4.toString());
        request.setUrl(ipUrl);
        // Set the HTTP Host header explicitly (this Qt build has no HostHeader
        // KnownHeaders enum) so the server routes by the original hostname even
        // though we connect by IP.
        request.setRawHeader("Host", host.toUtf8());
        // Keep TLS SNI and certificate verification bound to the hostname.
        request.setPeerVerifyName(host);

        logger.info() << "WarpController:" << logTag << "via IPv4" << ipv4.toString()
                      << "(host" << host << ")";
        send(request);
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

    sendWithIPv4(request, QStringLiteral("reg"), [this, verb, payload, endpoint, onDone](const QNetworkRequest &request) {
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
                onDone(false, {}, tr("некорректный ответ сервера"));
                return;
            }

            onDone(true, response, QString());
        });
    });
}

void WarpController::importNewConfig(const WarpSession &session)
{
    const QString previousServerId = findWarpServerId();

    auto importResult = m_importController->extractConfigFromData(
            buildConfigText(session.clientPrivateKey, session.addresses(), session.peerPublicKey, defaultParams()));
    if (importResult.errorCode != ErrorCode::NoError || importResult.config.isEmpty()) {
        fail(tr("Не удалось обработать полученный WARP-конфиг"));
        return;
    }

    importResult.config[configKey::description] = QStringLiteral("WARP");
    m_importController->importConfig(importResult.config);

    if (!previousServerId.isEmpty()) {
        m_serversRepository->removeServer(previousServerId);
    }

    // If the scan already finished (before the session arrived), apply its
    // endpoint now that the server exists — only in auto mode.
    if (!m_scannedEndpointIp.isEmpty() && m_appSettingsRepository->isWarpEndpointAuto()) {
        applyScannedEndpoint(findWarpServerId(), m_scannedEndpointIp, protocols::warp::warpPortDefault);
    }

    logger.info() << "New WARP config imported";
    setBusy(false);
    emit configReady();
}

void WarpController::updateExistingConfig(const QString &serverId, const WarpSession &session)
{
    auto serverConfig = m_serversRepository->nativeConfig(serverId);
    if (!serverConfig.has_value()) {
        fail(tr("Не удалось прочитать сохранённый WARP-конфиг"));
        return;
    }

    const DockerContainer container = serverConfig->defaultContainer;
    ContainerConfig containerConfig = serverConfig->containerConfig(container);
    AwgProtocolConfig *awgConfig = containerConfig.getAwgProtocolConfig();
    if (!awgConfig || !awgConfig->hasClientConfig()) {
        fail(tr("Не удалось прочитать сохранённый WARP-конфиг"));
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

    // If the scan already finished, apply its endpoint now (auto mode only)
    if (!m_scannedEndpointIp.isEmpty() && m_appSettingsRepository->isWarpEndpointAuto()) {
        applyScannedEndpoint(serverId, m_scannedEndpointIp, protocols::warp::warpPortDefault);
    }

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

QVariantMap WarpController::getConfigFields() const
{
    const QString serverId = findWarpServerId();
    if (serverId.isEmpty()) {
        return {};
    }

    const auto serverConfig = m_serversRepository->nativeConfig(serverId);
    if (!serverConfig.has_value()) {
        return {};
    }

    const ContainerConfig containerConfig = serverConfig->containerConfig(serverConfig->defaultContainer);
    const auto *awgConfig = containerConfig.getAwgProtocolConfig();
    if (!awgConfig || !awgConfig->hasClientConfig()) {
        return {};
    }

    const AwgClientConfig &clientConfig = awgConfig->clientConfig.value();

    QVariantMap fields;
    fields[fieldKey::junkPacketCount] = clientConfig.junkPacketCount;
    fields[fieldKey::junkPacketMinSize] = clientConfig.junkPacketMinSize;
    fields[fieldKey::junkPacketMaxSize] = clientConfig.junkPacketMaxSize;
    fields[fieldKey::initPacketJunkSize] = clientConfig.initPacketJunkSize;
    fields[fieldKey::responsePacketJunkSize] = clientConfig.responsePacketJunkSize;
    fields[fieldKey::initPacketMagicHeader] = clientConfig.initPacketMagicHeader;
    fields[fieldKey::responsePacketMagicHeader] = clientConfig.responsePacketMagicHeader;
    fields[fieldKey::underloadPacketMagicHeader] = clientConfig.underloadPacketMagicHeader;
    fields[fieldKey::transportPacketMagicHeader] = clientConfig.transportPacketMagicHeader;
    fields[fieldKey::specialJunk1] = clientConfig.specialJunk1;
    fields[fieldKey::mtu] = clientConfig.mtu;

    // DNS is stored as a line of the raw .conf text; the first two entries are duplicated
    // at the server level (dns1/dns2) and are used at connection time
    QString dns;
    const QRegularExpression dnsLineRegExp(QStringLiteral("^DNS = (.*)$"), QRegularExpression::MultilineOption);
    const QRegularExpressionMatch dnsMatch = dnsLineRegExp.match(clientConfig.nativeConfig);
    if (dnsMatch.hasMatch()) {
        dns = dnsMatch.captured(1).trimmed();
    }
    if (dns.isEmpty()) {
        QStringList dnsParts;
        if (!serverConfig->dns1.isEmpty()) {
            dnsParts << serverConfig->dns1;
        }
        if (!serverConfig->dns2.isEmpty()) {
            dnsParts << serverConfig->dns2;
        }
        dns = dnsParts.join(", ");
    }
    fields[fieldKey::dns] = dns;

    fields[fieldKey::allowedIps] = clientConfig.allowedIps.join(", ");
    fields[fieldKey::endpointHost] = clientConfig.hostName;
    fields[fieldKey::endpointPort] =
            clientConfig.port > 0 ? QString::number(clientConfig.port) : QString(protocols::warp::endpointPort);

    // Read-only Cloudflare session fields
    QString clientIpV4;
    QString clientIpV6;
    const QStringList addresses = splitCommaList(clientConfig.clientIp);
    for (const QString &address : addresses) {
        if (address.contains(':')) {
            clientIpV6 = address;
        } else {
            clientIpV4 = address;
        }
    }
    fields[fieldKey::clientIpV4] = clientIpV4;
    fields[fieldKey::clientIpV6] = clientIpV6;
    fields[fieldKey::peerPublicKey] = clientConfig.serverPublicKey;

    fields[fieldKey::endpointAuto] = m_appSettingsRepository->isWarpEndpointAuto();

    return fields;
}

QVariantMap WarpController::getDefaultConfigFields() const
{
    const WarpParams params = defaultParams();

    QVariantMap fields;
    fields[fieldKey::junkPacketCount] = params.junkPacketCount;
    fields[fieldKey::junkPacketMinSize] = params.junkPacketMinSize;
    fields[fieldKey::junkPacketMaxSize] = params.junkPacketMaxSize;
    fields[fieldKey::initPacketJunkSize] = params.initPacketJunkSize;
    fields[fieldKey::responsePacketJunkSize] = params.responsePacketJunkSize;
    fields[fieldKey::initPacketMagicHeader] = params.initPacketMagicHeader;
    fields[fieldKey::responsePacketMagicHeader] = params.responsePacketMagicHeader;
    fields[fieldKey::underloadPacketMagicHeader] = params.underloadPacketMagicHeader;
    fields[fieldKey::transportPacketMagicHeader] = params.transportPacketMagicHeader;
    fields[fieldKey::specialJunk1] = params.specialJunk1;
    fields[fieldKey::mtu] = params.mtu;
    fields[fieldKey::dns] = params.dns;
    fields[fieldKey::allowedIps] = params.allowedIps;
    fields[fieldKey::endpointHost] = params.endpointHost;
    fields[fieldKey::endpointPort] = params.endpointPort;
    fields[fieldKey::endpointAuto] = true;
    return fields;
}

bool WarpController::saveConfig(const QVariantMap &fields)
{
    const QString serverId = findWarpServerId();
    if (serverId.isEmpty()) {
        emit errorOccurred(tr("Сохранённый WARP-конфиг не найден"));
        return false;
    }

    auto serverConfig = m_serversRepository->nativeConfig(serverId);
    if (!serverConfig.has_value()) {
        emit errorOccurred(tr("Не удалось прочитать сохранённый WARP-конфиг"));
        return false;
    }

    const DockerContainer container = serverConfig->defaultContainer;
    ContainerConfig containerConfig = serverConfig->containerConfig(container);
    AwgProtocolConfig *awgConfig = containerConfig.getAwgProtocolConfig();
    if (!awgConfig || !awgConfig->hasClientConfig()) {
        emit errorOccurred(tr("Не удалось прочитать сохранённый WARP-конфиг"));
        return false;
    }

    WarpParams params = defaultParams();
    auto takeField = [&fields](const QLatin1String &key, QString &target) {
        if (fields.contains(key)) {
            target = fields.value(key).toString().trimmed();
        }
    };
    takeField(fieldKey::junkPacketCount, params.junkPacketCount);
    takeField(fieldKey::junkPacketMinSize, params.junkPacketMinSize);
    takeField(fieldKey::junkPacketMaxSize, params.junkPacketMaxSize);
    takeField(fieldKey::initPacketJunkSize, params.initPacketJunkSize);
    takeField(fieldKey::responsePacketJunkSize, params.responsePacketJunkSize);
    takeField(fieldKey::initPacketMagicHeader, params.initPacketMagicHeader);
    takeField(fieldKey::responsePacketMagicHeader, params.responsePacketMagicHeader);
    takeField(fieldKey::underloadPacketMagicHeader, params.underloadPacketMagicHeader);
    takeField(fieldKey::transportPacketMagicHeader, params.transportPacketMagicHeader);
    takeField(fieldKey::specialJunk1, params.specialJunk1);
    takeField(fieldKey::mtu, params.mtu);
    takeField(fieldKey::dns, params.dns);
    takeField(fieldKey::allowedIps, params.allowedIps);
    takeField(fieldKey::endpointHost, params.endpointHost);
    takeField(fieldKey::endpointPort, params.endpointPort);

    const int endpointPort = params.endpointPort.toInt();
    if (params.endpointHost.isEmpty() || endpointPort <= 0 || endpointPort > 65535) {
        emit errorOccurred(tr("Некорректный Endpoint"));
        return false;
    }
    if (params.mtu.toInt() < 576) {
        emit errorOccurred(tr("Некорректное значение MTU"));
        return false;
    }
    const QStringList allowedIpsList = splitCommaList(params.allowedIps);
    if (allowedIpsList.isEmpty()) {
        emit errorOccurred(tr("AllowedIPs не может быть пустым"));
        return false;
    }
    params.allowedIps = allowedIpsList.join(", ");
    params.dns = splitCommaList(params.dns).join(", ");

    // Replace only the user parameters, keeping the Cloudflare session fields intact
    AwgClientConfig clientConfig = awgConfig->clientConfig.value();
    clientConfig.junkPacketCount = params.junkPacketCount;
    clientConfig.junkPacketMinSize = params.junkPacketMinSize;
    clientConfig.junkPacketMaxSize = params.junkPacketMaxSize;
    clientConfig.initPacketJunkSize = params.initPacketJunkSize;
    clientConfig.responsePacketJunkSize = params.responsePacketJunkSize;
    clientConfig.initPacketMagicHeader = params.initPacketMagicHeader;
    clientConfig.responsePacketMagicHeader = params.responsePacketMagicHeader;
    clientConfig.underloadPacketMagicHeader = params.underloadPacketMagicHeader;
    clientConfig.transportPacketMagicHeader = params.transportPacketMagicHeader;
    clientConfig.specialJunk1 = params.specialJunk1;
    clientConfig.mtu = params.mtu;
    clientConfig.hostName = params.endpointHost;
    clientConfig.port = endpointPort;
    clientConfig.allowedIps = allowedIpsList;
    clientConfig.nativeConfig =
            buildConfigText(clientConfig.clientPrivateKey, clientConfig.clientIp, clientConfig.serverPublicKey, params);

    awgConfig->setClientConfig(clientConfig);
    awgConfig->serverConfig.port = params.endpointPort;

    serverConfig->hostName = params.endpointHost;

    // Server-level dns1/dns2 are used at connection time and accept IPv4 only
    // (as in ImportController::extractWireGuardConfig)
    QStringList dnsV4List;
    const QStringList dnsList = splitCommaList(params.dns);
    for (const QString &dnsEntry : dnsList) {
        if (!dnsEntry.contains(':')) {
            dnsV4List << dnsEntry;
        }
    }
    serverConfig->dns1 = dnsV4List.value(0);
    serverConfig->dns2 = dnsV4List.value(1);

    serverConfig->updateContainerConfig(container, containerConfig);
    m_serversRepository->editServer(serverId, serverConfig->toJson(), serverConfigUtils::ConfigType::Native);

    // Persist the endpoint mode: in auto mode the scanner may overwrite the
    // endpoint on the next refresh; in manual mode it never touches it.
    if (fields.contains(fieldKey::endpointAuto)) {
        const bool wantAuto = fields.value(fieldKey::endpointAuto).toBool();
        if (wantAuto != m_appSettingsRepository->isWarpEndpointAuto()) {
            m_appSettingsRepository->setWarpEndpointAuto(wantAuto);
            emit endpointAutoChanged(wantAuto);
        }
    }

    logger.info() << "WARP config settings saved";
    emit configSaved();
    return true;
}

WarpController::WarpParams WarpController::defaultParams()
{
    WarpParams params;
    params.junkPacketCount = protocols::warp::defaultJunkPacketCount;
    params.junkPacketMinSize = protocols::warp::defaultJunkPacketMinSize;
    params.junkPacketMaxSize = protocols::warp::defaultJunkPacketMaxSize;
    params.initPacketJunkSize = protocols::warp::defaultInitPacketJunkSize;
    params.responsePacketJunkSize = protocols::warp::defaultResponsePacketJunkSize;
    params.initPacketMagicHeader = protocols::warp::defaultInitPacketMagicHeader;
    params.responsePacketMagicHeader = protocols::warp::defaultResponsePacketMagicHeader;
    params.underloadPacketMagicHeader = protocols::warp::defaultUnderloadPacketMagicHeader;
    params.transportPacketMagicHeader = protocols::warp::defaultTransportPacketMagicHeader;
    params.specialJunk1 = protocols::warp::defaultSpecialJunk1;
    params.mtu = protocols::warp::defaultMtu;
    params.dns = protocols::warp::defaultDns;
    params.allowedIps = protocols::warp::defaultAllowedIps;
    params.endpointHost = protocols::warp::endpointHost;
    params.endpointPort = protocols::warp::endpointPort;
    return params;
}

QString WarpController::buildConfigText(const QString &privateKey, const QString &address, const QString &peerPublicKey,
                                        const WarpParams &params)
{
    QStringList lines;
    lines << QStringLiteral("[Interface]");
    lines << QStringLiteral("PrivateKey = %1").arg(privateKey);
    lines << QStringLiteral("Address = %1").arg(address);
    if (!params.dns.isEmpty()) {
        lines << QStringLiteral("DNS = %1").arg(params.dns);
    }
    lines << QStringLiteral("MTU = %1").arg(params.mtu);
    lines << QStringLiteral("Jc = %1").arg(params.junkPacketCount);
    lines << QStringLiteral("Jmin = %1").arg(params.junkPacketMinSize);
    lines << QStringLiteral("Jmax = %1").arg(params.junkPacketMaxSize);
    lines << QStringLiteral("S1 = %1").arg(params.initPacketJunkSize);
    lines << QStringLiteral("S2 = %1").arg(params.responsePacketJunkSize);
    lines << QStringLiteral("H1 = %1").arg(params.initPacketMagicHeader);
    lines << QStringLiteral("H2 = %1").arg(params.responsePacketMagicHeader);
    lines << QStringLiteral("H3 = %1").arg(params.underloadPacketMagicHeader);
    lines << QStringLiteral("H4 = %1").arg(params.transportPacketMagicHeader);
    if (!params.specialJunk1.isEmpty()) {
        lines << QStringLiteral("I1 = %1").arg(params.specialJunk1);
    }
    lines << QString();
    lines << QStringLiteral("[Peer]");
    lines << QStringLiteral("PublicKey = %1").arg(peerPublicKey);
    lines << QStringLiteral("AllowedIPs = %1").arg(params.allowedIps);
    lines << QStringLiteral("Endpoint = %1:%2").arg(params.endpointHost, params.endpointPort);
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

void WarpController::startEndpointScan()
{
    m_scannedEndpointIp.clear();
    setScanning(true);
    m_scanner->scanBest();
}

void WarpController::applyScannedEndpoint(const QString &serverId, const QString &ip, int port)
{
    if (ip.isEmpty() || serverId.isEmpty()) {
        return;
    }

    auto serverConfig = m_serversRepository->nativeConfig(serverId);
    if (!serverConfig.has_value()) {
        return;
    }

    const DockerContainer container = serverConfig->defaultContainer;
    ContainerConfig containerConfig = serverConfig->containerConfig(container);
    AwgProtocolConfig *awgConfig = containerConfig.getAwgProtocolConfig();
    if (!awgConfig || !awgConfig->hasClientConfig()) {
        return;
    }

    // Update the endpoint in every place it is stored (mirrors saveConfig)
    AwgClientConfig clientConfig = awgConfig->clientConfig.value();
    replaceConfigTextValue(clientConfig.nativeConfig, protocols::wireguard::Endpoint,
                           QStringLiteral("%1:%2").arg(ip).arg(port));
    clientConfig.hostName = ip;
    clientConfig.port = port;

    awgConfig->setClientConfig(clientConfig);
    awgConfig->serverConfig.port = QString::number(port);
    serverConfig->hostName = ip;

    serverConfig->updateContainerConfig(container, containerConfig);
    m_serversRepository->editServer(serverId, serverConfig->toJson(), serverConfigUtils::ConfigType::Native);

    logger.info() << "WARP endpoint set to" << ip << ":" << port << "by scanner";
}

bool WarpController::applyRelay(const QString &host, int port)
{
    if (host.isEmpty() || port <= 0 || port > 65535) {
        emit errorOccurred(tr("Некорректный релей"));
        return false;
    }

    const QString serverId = findWarpServerId();
    if (serverId.isEmpty()) {
        emit errorOccurred(tr("Сохранённый WARP-конфиг не найден"));
        return false;
    }

    auto serverConfig = m_serversRepository->nativeConfig(serverId);
    if (!serverConfig.has_value()) {
        emit errorOccurred(tr("Не удалось прочитать сохранённый WARP-конфиг"));
        return false;
    }

    const DockerContainer container = serverConfig->defaultContainer;
    ContainerConfig containerConfig = serverConfig->containerConfig(container);
    AwgProtocolConfig *awgConfig = containerConfig.getAwgProtocolConfig();
    if (!awgConfig || !awgConfig->hasClientConfig()) {
        emit errorOccurred(tr("Не удалось прочитать сохранённый WARP-конфиг"));
        return false;
    }

    // Update the endpoint in every place it is stored (mirrors applyScannedEndpoint)
    AwgClientConfig clientConfig = awgConfig->clientConfig.value();
    replaceConfigTextValue(clientConfig.nativeConfig, protocols::wireguard::Endpoint,
                           QStringLiteral("%1:%2").arg(host).arg(port));
    clientConfig.hostName = host;
    clientConfig.port = port;

    awgConfig->setClientConfig(clientConfig);
    awgConfig->serverConfig.port = QString::number(port);
    serverConfig->hostName = host;

    serverConfig->updateContainerConfig(container, containerConfig);
    m_serversRepository->editServer(serverId, serverConfig->toJson(), serverConfigUtils::ConfigType::Native);

    // Switch to manual endpoint mode so the scanner never overwrites the relay.
    if (m_appSettingsRepository->isWarpEndpointAuto()) {
        m_appSettingsRepository->setWarpEndpointAuto(false);
        emit endpointAutoChanged(false);
    }

    logger.info() << "WARP endpoint set to relay" << host << ":" << port;
    emit configSaved();
    return true;
}

void WarpController::setEndpointAuto(bool autoMode)
{
    if (m_appSettingsRepository->isWarpEndpointAuto() == autoMode) {
        return;
    }
    m_appSettingsRepository->setWarpEndpointAuto(autoMode);
    emit endpointAutoChanged(autoMode);
    // In auto mode the next refresh/scan re-picks the best endpoint.
}

QString WarpController::currentEndpoint() const
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
    const auto &cc = awgConfig->clientConfig.value();
    if (cc.hostName.isEmpty()) {
        return QString();
    }
    return QStringLiteral("%1:%2").arg(cc.hostName).arg(cc.port);
}

QStringList WarpController::detectActiveVpns() const
{
    // Patterns of interface names that typically belong to a VPN tunnel.
    static const QStringList kVpnPrefixes = {
        QStringLiteral("amn"),     // AmneziaVPN / AmneziaWG
        QStringLiteral("awarp"),   // our own future tunnel (defensive)
        QStringLiteral("tun"),     // OpenVPN and many others
        QStringLiteral("wg"),      // WireGuard
        QStringLiteral("utun"),    // macOS tunnels
        QStringLiteral("ppp"),     // PPTP/L2TP
        QStringLiteral("nordlynx"),
        QStringLiteral("proton"),
        QStringLiteral("wt"),      // various WireGuard-based clients
    };

    QStringList result;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        const auto flags = iface.flags();
        // Only consider interfaces that are actually up and routable.
        if (!flags.testFlag(QNetworkInterface::IsUp)
            || !flags.testFlag(QNetworkInterface::IsRunning)) {
            continue;
        }
        if (flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }

        const QString name = iface.name();
        if (name.isEmpty()) {
            continue;
        }

        bool isVpn = false;
        for (const QString &prefix : kVpnPrefixes) {
            if (name.startsWith(prefix, Qt::CaseInsensitive)) {
                isVpn = true;
                break;
            }
        }
        // Point-to-point non-loopback interfaces are almost always tunnels.
        if (!isVpn && flags.testFlag(QNetworkInterface::IsPointToPoint)) {
            isVpn = true;
        }
        if (!isVpn) {
            continue;
        }

        // Build a human-readable label.
        QString label;
        if (name.startsWith(QStringLiteral("amn"), Qt::CaseInsensitive)) {
            label = QStringLiteral("AmneziaVPN (%1)").arg(name);
        } else {
            label = name;
        }
        if (!result.contains(label)) {
            result << label;
        }
    }
    return result;
}

void WarpController::startLatencyMonitor()
{
    probeLatencyOnce();
    if (!m_latencyTimer->isActive()) {
        m_latencyTimer->start();
    }
}

void WarpController::stopLatencyMonitor()
{
    m_latencyTimer->stop();
    setLatency(-1);
}

QString WarpController::activeEndpointHost() const
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
    return awgConfig->clientConfig->hostName;
}

void WarpController::probeLatencyOnce()
{
    const QString host = activeEndpointHost();
    if (host.isEmpty()) {
        setLatency(-1);
        return;
    }

    auto *socket = new QTcpSocket(this);
    auto *timer = new QElapsedTimer;
    auto *done = new bool(false);
    timer->start();

    auto finish = [this, socket, timer, done](int latency) {
        if (*done) {
            return;
        }
        *done = true;
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
        delete timer;
        delete done;
        setLatency(latency);
    };

    connect(socket, &QTcpSocket::connected, this, [finish, timer]() {
        finish(int(timer->elapsed()));
    });
    connect(socket, &QAbstractSocket::errorOccurred, this, [finish](QAbstractSocket::SocketError) {
        finish(-1);
    });

    socket->connectToHost(host, protocols::warp::scanProbePort);

    QTimer::singleShot(protocols::warp::scanProbeTimeoutMs, socket, [socket, finish]() {
        if (socket->state() != QAbstractSocket::ConnectedState) {
            finish(-1);
        }
    });
}

void WarpController::setLatency(int latencyMs)
{
    if (m_latencyMs == latencyMs) {
        return;
    }
    m_latencyMs = latencyMs;
    emit latencyChanged(latencyMs);
}

void WarpController::setBusy(bool busy)
{
    if (m_isBusy == busy) {
        return;
    }
    m_isBusy = busy;
    emit busyChanged(busy);
}

void WarpController::setScanning(bool scanning)
{
    if (m_isScanning == scanning) {
        return;
    }
    m_isScanning = scanning;
    emit scanningChanged(scanning);
}

void WarpController::fail(const QString &errorMessage)
{
    setBusy(false);
    emit errorOccurred(errorMessage);
}
