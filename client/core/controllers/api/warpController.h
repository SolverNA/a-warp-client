#ifndef WARPCONTROLLER_H
#define WARPCONTROLLER_H

#include <QJsonObject>
#include <QObject>
#include <QVariantMap>

#include <functional>

#include "core/repositories/secureAppSettingsRepository.h"
#include "core/repositories/secureServersRepository.h"

class ImportController;
class WarpScanner;
class QTimer;

class WarpController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool hasConfig READ hasConfig NOTIFY hasConfigChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(bool isScanning READ isScanning NOTIFY scanningChanged)
    Q_PROPERTY(bool endpointAuto READ endpointAuto NOTIFY endpointAutoChanged)
    Q_PROPERTY(int latencyMs READ latencyMs NOTIFY latencyChanged)

public:
    explicit WarpController(SecureServersRepository *serversRepository,
                            SecureAppSettingsRepository *appSettingsRepository,
                            ImportController *importController, QObject *parent = nullptr);

    bool hasConfig() const;
    bool isBusy() const;
    bool isScanning() const;
    bool endpointAuto() const;
    int latencyMs() const;
    Q_INVOKABLE QString getConfigJson() const;
    Q_INVOKABLE QVariantMap getConfigFields() const;
    Q_INVOKABLE QVariantMap getDefaultConfigFields() const;
    Q_INVOKABLE bool saveConfig(const QVariantMap &fields);

    // AWARP relays: apply a relay endpoint to the saved WARP config and switch
    // to manual endpoint mode (the scanner never overwrites a manual endpoint).
    Q_INVOKABLE bool applyRelay(const QString &host, int port);
    // Return to automatic endpoint mode; the next refresh/scan re-picks the endpoint.
    Q_INVOKABLE void setEndpointAuto(bool autoMode);
    // Active endpoint of the saved WARP config as "host:port" ("" if unknown).
    Q_INVOKABLE QString currentEndpoint() const;

    // AWARP: detect already-active VPN tunnels (e.g. AmneziaVPN) before
    // connecting, so the user can be warned about a conflicting VPN. Returns a
    // list of human-readable interface descriptions; an empty list means clean.
    Q_INVOKABLE QStringList detectActiveVpns() const;

public slots:
    void fetchNewConfig();
    void refreshConfig();

    // Latency measurement to the active endpoint, driven by the connection
    // state from QML. startLatencyMonitor() probes immediately and repeats
    // periodically while connected; stopLatencyMonitor() halts it.
    void startLatencyMonitor();
    void stopLatencyMonitor();

signals:
    void configReady();
    void configUpdated();
    void configSaved();
    void errorOccurred(const QString &errorMessage);
    void busyChanged(bool busy);
    void hasConfigChanged();
    void scanningChanged(bool scanning);
    void endpointAutoChanged(bool endpointAuto);
    void latencyChanged(int latencyMs);

private:
    struct WarpSession
    {
        QString clientPrivateKey;
        QString clientPublicKey;
        QString peerPublicKey;
        QString addressV4;
        QString addressV6;

        QString addresses() const;
    };

    // User-editable (non-session) parameters of the WARP config
    struct WarpParams
    {
        QString junkPacketCount;
        QString junkPacketMinSize;
        QString junkPacketMaxSize;
        QString initPacketJunkSize;
        QString responsePacketJunkSize;
        QString initPacketMagicHeader;
        QString responsePacketMagicHeader;
        QString underloadPacketMagicHeader;
        QString transportPacketMagicHeader;
        QString specialJunk1;
        QString mtu;
        QString dns;
        QString allowedIps;
        QString endpointHost;
        QString endpointPort;
    };

    static WarpParams defaultParams();

    void registerAccount(const std::function<void(bool ok, const WarpSession &session)> &onDone);
    // AWARP: fallback path — fetch a ready WARP session from our own bootstrap
    // service when the direct Cloudflare registration fails.
    void registerAccountViaBootstrap(
            const std::function<void(bool ok, const WarpSession &session, const QString &errorMessage)> &onDone);
    void sendRequestAsync(const QByteArray &verb, const QString &endpoint, const QJsonObject &body, const QString &bearerToken,
                          const std::function<void(bool ok, const QJsonObject &response, const QString &errorMessage)> &onDone);

    void importNewConfig(const WarpSession &session);
    void updateExistingConfig(const QString &serverId, const WarpSession &session);

    static QString buildConfigText(const QString &privateKey, const QString &address, const QString &peerPublicKey,
                                   const WarpParams &params);
    QString findWarpServerId() const;

    void setBusy(bool busy);
    void setScanning(bool scanning);
    void fail(const QString &errorMessage);

    // Endpoint scanner orchestration (parallel with registerAccount)
    void startEndpointScan();
    void applyScannedEndpoint(const QString &serverId, const QString &ip, int port);

    // Latency measurement
    QString activeEndpointHost() const;
    void probeLatencyOnce();
    void setLatency(int latencyMs);

    SecureServersRepository *m_serversRepository;
    SecureAppSettingsRepository *m_appSettingsRepository;
    ImportController *m_importController;

    WarpScanner *m_scanner = nullptr;

    bool m_isBusy = false;
    bool m_isScanning = false;

    // Best endpoint found by the most recent scan (empty until the scan finishes)
    QString m_scannedEndpointIp;

    // Latency-to-active-endpoint monitor
    QTimer *m_latencyTimer = nullptr;
    int m_latencyMs = -1; // -1 = unknown / not measured
};

#endif // WARPCONTROLLER_H
