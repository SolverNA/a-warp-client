#ifndef WARPCONTROLLER_H
#define WARPCONTROLLER_H

#include <QJsonObject>
#include <QObject>

#include <functional>

#include "core/repositories/secureAppSettingsRepository.h"
#include "core/repositories/secureServersRepository.h"

class ImportController;

class WarpController : public QObject
{
    Q_OBJECT

public:
    explicit WarpController(SecureServersRepository *serversRepository,
                            SecureAppSettingsRepository *appSettingsRepository,
                            ImportController *importController, QObject *parent = nullptr);

    Q_INVOKABLE bool hasConfig() const;
    Q_INVOKABLE QString getConfigJson() const;
    Q_INVOKABLE bool saveConfig(const QString &configJson);

public slots:
    void fetchNewConfig();
    void refreshConfig();

signals:
    void configReady();
    void configUpdated();
    void errorOccurred(const QString &errorMessage);
    void busyChanged(bool busy);

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

    void registerAccount(const std::function<void(bool ok, const WarpSession &session)> &onDone);
    void sendRequestAsync(const QByteArray &verb, const QString &endpoint, const QJsonObject &body, const QString &bearerToken,
                          const std::function<void(bool ok, const QJsonObject &response, const QString &errorMessage)> &onDone);

    void importNewConfig(const WarpSession &session);
    void updateExistingConfig(const QString &serverId, const WarpSession &session);

    QString buildConfigText(const WarpSession &session) const;
    QString findWarpServerId() const;

    void setBusy(bool busy);
    void fail(const QString &errorMessage);

    SecureServersRepository *m_serversRepository;
    SecureAppSettingsRepository *m_appSettingsRepository;
    ImportController *m_importController;

    bool m_isBusy = false;
};

#endif // WARPCONTROLLER_H
