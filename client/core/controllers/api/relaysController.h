#ifndef RELAYSCONTROLLER_H
#define RELAYSCONTROLLER_H

#include <QAbstractListModel>
#include <QString>
#include <QVector>

#include "core/repositories/secureAppSettingsRepository.h"

class WarpController;

// AWARP: relays model. Combines built-in relays (baked into the binary via
// AwarpBackendConfig, read-only) with user-defined relays (CRUD, persisted in
// SecureAppSettingsRepository under "Warp/relays"). Selecting a relay applies
// its host:port to the saved WARP config through WarpController (manual endpoint
// mode). Latency is measured with a plain TCP-connect probe (no privileges, no
// tunnel required), mirroring WarpController::probeLatencyOnce.
class RelaysController : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        LabelRole,
        HostRole,
        PortRole,
        CountryRole,
        LatencyMsRole,
        IsSelectedRole,
        IsBuiltinRole,
    };

    explicit RelaysController(SecureAppSettingsRepository *appSettingsRepository,
                              WarpController *warpController, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void reload();
    Q_INVOKABLE bool addRelay(const QString &label, const QString &host, int port, const QString &country);
    Q_INVOKABLE bool editRelay(const QString &id, const QString &label, const QString &host, int port,
                               const QString &country);
    Q_INVOKABLE bool removeRelay(const QString &id);
    Q_INVOKABLE bool selectRelay(const QString &id);
    Q_INVOKABLE void selectAuto();
    Q_INVOKABLE void pingRelay(const QString &id);
    Q_INVOKABLE void pingAll();

private:
    struct Relay
    {
        QString id;
        QString label;
        QString host;
        int port = 0;
        QString country;
        int latencyMs = -1; // -1 = not measured
        bool isBuiltin = false;
    };

    void rebuild();
    void persistUserRelays();
    int indexOfId(const QString &id) const;
    void updateLatency(const QString &id, int latencyMs);
    void probeRelay(const Relay &relay);

    SecureAppSettingsRepository *m_appSettingsRepository;
    WarpController *m_warpController;

    QVector<Relay> m_relays;
};

#endif // RELAYSCONTROLLER_H
