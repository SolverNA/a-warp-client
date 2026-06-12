#include "relaysController.h"

#include <QAbstractSocket>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>

#include "logger.h"
#include "core/controllers/api/warpController.h"
#include "core/utils/config/awarpBackendConfig.h"

namespace
{
    Logger logger("RelaysController");

    // TCP-connect probe parameters (no special privileges, no tunnel needed).
    constexpr int kProbeTimeoutMs = 2000;
}

RelaysController::RelaysController(SecureAppSettingsRepository *appSettingsRepository,
                                   WarpController *warpController, QObject *parent)
    : QAbstractListModel(parent)
    , m_appSettingsRepository(appSettingsRepository)
    , m_warpController(warpController)
{
    rebuild();
}

int RelaysController::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_relays.size();
}

QVariant RelaysController::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_relays.size()) {
        return QVariant();
    }
    const Relay &relay = m_relays.at(index.row());
    switch (role) {
    case IdRole: return relay.id;
    case LabelRole: return relay.label;
    case HostRole: return relay.host;
    case PortRole: return relay.port;
    case CountryRole: return relay.country;
    case LatencyMsRole: return relay.latencyMs;
    case IsBuiltinRole: return relay.isBuiltin;
    case IsSelectedRole: {
        const QString endpoint = m_warpController ? m_warpController->currentEndpoint() : QString();
        return endpoint == QStringLiteral("%1:%2").arg(relay.host).arg(relay.port);
    }
    default: return QVariant();
    }
}

QHash<int, QByteArray> RelaysController::roleNames() const
{
    return {
        { IdRole, "id" },
        { LabelRole, "label" },
        { HostRole, "host" },
        { PortRole, "port" },
        { CountryRole, "country" },
        { LatencyMsRole, "latencyMs" },
        { IsSelectedRole, "isSelected" },
        { IsBuiltinRole, "isBuiltin" },
    };
}

void RelaysController::reload()
{
    beginResetModel();
    rebuild();
    endResetModel();
}

void RelaysController::rebuild()
{
    // Preserve previously measured latencies across a rebuild, keyed by host:port.
    QHash<QString, int> previousLatency;
    for (const Relay &relay : m_relays) {
        previousLatency.insert(QStringLiteral("%1:%2").arg(relay.host).arg(relay.port), relay.latencyMs);
    }

    m_relays.clear();

    // Built-in relays (read-only)
    const QVector<AwarpBackendConfig::Relay> builtin = AwarpBackendConfig::relays();
    int builtinIndex = 0;
    for (const AwarpBackendConfig::Relay &b : builtin) {
        Relay relay;
        relay.id = QStringLiteral("builtin:%1").arg(builtinIndex++);
        relay.label = b.label.isEmpty() ? QStringLiteral("%1:%2").arg(b.host).arg(b.port) : b.label;
        relay.host = b.host;
        relay.port = b.port;
        relay.country = b.country;
        relay.isBuiltin = true;
        relay.latencyMs = previousLatency.value(QStringLiteral("%1:%2").arg(b.host).arg(b.port), -1);
        m_relays << relay;
    }

    // User relays (CRUD)
    const QString json = m_appSettingsRepository->getWarpRelays();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isArray()) {
        const QJsonArray arr = doc.array();
        for (const QJsonValue &value : arr) {
            const QJsonObject obj = value.toObject();
            Relay relay;
            relay.id = obj.value("id").toString();
            if (relay.id.isEmpty()) {
                relay.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            }
            relay.label = obj.value("label").toString();
            relay.host = obj.value("host").toString();
            relay.port = obj.value("port").toInt();
            relay.country = obj.value("country").toString();
            relay.isBuiltin = false;
            if (relay.host.isEmpty() || relay.port <= 0) {
                continue;
            }
            relay.latencyMs = previousLatency.value(QStringLiteral("%1:%2").arg(relay.host).arg(relay.port), -1);
            m_relays << relay;
        }
    }
}

void RelaysController::persistUserRelays()
{
    QJsonArray arr;
    for (const Relay &relay : m_relays) {
        if (relay.isBuiltin) {
            continue;
        }
        QJsonObject obj;
        obj["id"] = relay.id;
        obj["label"] = relay.label;
        obj["host"] = relay.host;
        obj["port"] = relay.port;
        obj["country"] = relay.country;
        arr.append(obj);
    }
    m_appSettingsRepository->setWarpRelays(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

int RelaysController::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_relays.size(); ++i) {
        if (m_relays.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

bool RelaysController::addRelay(const QString &label, const QString &host, int port, const QString &country)
{
    const QString trimmedHost = host.trimmed();
    if (trimmedHost.isEmpty() || port <= 0 || port > 65535) {
        return false;
    }

    Relay relay;
    relay.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    relay.label = label.trimmed().isEmpty() ? QStringLiteral("%1:%2").arg(trimmedHost).arg(port) : label.trimmed();
    relay.host = trimmedHost;
    relay.port = port;
    relay.country = country.trimmed().toUpper();
    relay.isBuiltin = false;

    beginInsertRows(QModelIndex(), m_relays.size(), m_relays.size());
    m_relays << relay;
    endInsertRows();

    persistUserRelays();
    return true;
}

bool RelaysController::editRelay(const QString &id, const QString &label, const QString &host, int port,
                                 const QString &country)
{
    const int row = indexOfId(id);
    if (row < 0 || m_relays.at(row).isBuiltin) {
        return false;
    }
    const QString trimmedHost = host.trimmed();
    if (trimmedHost.isEmpty() || port <= 0 || port > 65535) {
        return false;
    }

    Relay &relay = m_relays[row];
    relay.label = label.trimmed().isEmpty() ? QStringLiteral("%1:%2").arg(trimmedHost).arg(port) : label.trimmed();
    relay.host = trimmedHost;
    relay.port = port;
    relay.country = country.trimmed().toUpper();
    relay.latencyMs = -1;

    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
    persistUserRelays();
    return true;
}

bool RelaysController::removeRelay(const QString &id)
{
    const int row = indexOfId(id);
    if (row < 0 || m_relays.at(row).isBuiltin) {
        return false;
    }
    beginRemoveRows(QModelIndex(), row, row);
    m_relays.remove(row);
    endRemoveRows();
    persistUserRelays();
    return true;
}

bool RelaysController::selectRelay(const QString &id)
{
    const int row = indexOfId(id);
    if (row < 0 || !m_warpController) {
        return false;
    }
    const Relay relay = m_relays.at(row);
    const bool ok = m_warpController->applyRelay(relay.host, relay.port);
    if (ok) {
        // The selected flag is derived from the active endpoint; refresh all rows.
        emit dataChanged(index(0), index(m_relays.size() - 1), { IsSelectedRole });
    }
    return ok;
}

void RelaysController::selectAuto()
{
    if (!m_warpController) {
        return;
    }
    m_warpController->setEndpointAuto(true);
    emit dataChanged(index(0), index(m_relays.size() - 1), { IsSelectedRole });
}

void RelaysController::updateLatency(const QString &id, int latencyMs)
{
    const int row = indexOfId(id);
    if (row < 0) {
        return;
    }
    m_relays[row].latencyMs = latencyMs;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { LatencyMsRole });
}

void RelaysController::probeRelay(const Relay &relay)
{
    const QString id = relay.id;
    const QString host = relay.host;
    const quint16 port = static_cast<quint16>(relay.port);

    auto *socket = new QTcpSocket(this);
    auto *timer = new QElapsedTimer;
    auto *done = new bool(false);
    timer->start();

    auto finish = [this, id, socket, timer, done](int latency) {
        if (*done) {
            return;
        }
        *done = true;
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
        delete timer;
        delete done;
        updateLatency(id, latency);
    };

    connect(socket, &QTcpSocket::connected, this, [finish, timer]() {
        finish(int(timer->elapsed()));
    });
    connect(socket, &QAbstractSocket::errorOccurred, this, [finish](QAbstractSocket::SocketError) {
        finish(-1);
    });

    socket->connectToHost(host, port);

    QTimer::singleShot(kProbeTimeoutMs, socket, [socket, finish]() {
        if (socket->state() != QAbstractSocket::ConnectedState) {
            finish(-1);
        }
    });
}

void RelaysController::pingRelay(const QString &id)
{
    const int row = indexOfId(id);
    if (row < 0) {
        return;
    }
    probeRelay(m_relays.at(row));
}

void RelaysController::pingAll()
{
    for (const Relay &relay : m_relays) {
        probeRelay(relay);
    }
}
