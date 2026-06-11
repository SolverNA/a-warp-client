#ifndef WARPCONTROLLER_H
#define WARPCONTROLLER_H

#include <QJsonObject>
#include <QObject>
#include <QVariantMap>

#include <functional>

#include "core/repositories/secureAppSettingsRepository.h"
#include "core/repositories/secureServersRepository.h"

class ImportController;

class WarpController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool hasConfig READ hasConfig NOTIFY hasConfigChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY busyChanged)

public:
    explicit WarpController(SecureServersRepository *serversRepository,
                            SecureAppSettingsRepository *appSettingsRepository,
                            ImportController *importController, QObject *parent = nullptr);

    bool hasConfig() const;
    bool isBusy() const;
    Q_INVOKABLE QString getConfigJson() const;
    Q_INVOKABLE QVariantMap getConfigFields() const;
    Q_INVOKABLE QVariantMap getDefaultConfigFields() const;
    Q_INVOKABLE bool saveConfig(const QVariantMap &fields);

public slots:
    void fetchNewConfig();
    void refreshConfig();

signals:
    void configReady();
    void configUpdated();
    void configSaved();
    void errorOccurred(const QString &errorMessage);
    void busyChanged(bool busy);
    void hasConfigChanged();

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
    void sendRequestAsync(const QByteArray &verb, const QString &endpoint, const QJsonObject &body, const QString &bearerToken,
                          const std::function<void(bool ok, const QJsonObject &response, const QString &errorMessage)> &onDone);

    void importNewConfig(const WarpSession &session);
    void updateExistingConfig(const QString &serverId, const WarpSession &session);

    static QString buildConfigText(const QString &privateKey, const QString &address, const QString &peerPublicKey,
                                   const WarpParams &params);
    QString findWarpServerId() const;

    void setBusy(bool busy);
    void fail(const QString &errorMessage);

    SecureServersRepository *m_serversRepository;
    SecureAppSettingsRepository *m_appSettingsRepository;
    ImportController *m_importController;

    bool m_isBusy = false;
};

#endif // WARPCONTROLLER_H
