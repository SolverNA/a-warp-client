#include "warpScanner.h"

#include <QAbstractSocket>
#include <QRandomGenerator>
#include <QSet>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>

#include "logger.h"
#include "core/utils/constants/warpConstants.h"

using namespace amnezia;

namespace
{
    Logger logger("WarpScanner");
}

WarpScanner::WarpScanner(QObject *parent) : QObject(parent)
{
}

WarpScanner::~WarpScanner()
{
    cancel();
}

void WarpScanner::scanBest()
{
    if (m_scanning) {
        return;
    }
    m_scanning = true;
    m_phase = Phase::Discovery;
    m_queue.clear();
    m_queuePos = 0;
    m_inFlight = 0;
    m_doneForProgress = 0;
    m_results.clear();
    m_confirmIps.clear();

    buildCandidatePool();
    m_totalForProgress = m_queue.size();

    if (m_queue.isEmpty()) {
        m_scanning = false;
        emit scanFailed(tr("Не удалось сформировать список эндпоинтов для сканирования"));
        return;
    }

    logger.info() << "WARP endpoint scan started," << m_queue.size() << "candidates";
    pumpQueue();
}

void WarpScanner::cancel()
{
    for (Probe *probe : m_active) {
        if (probe->socket) {
            probe->socket->disconnect(this);
            probe->socket->abort();
            probe->socket->deleteLater();
        }
        delete probe;
    }
    m_active.clear();
    m_inFlight = 0;
    m_scanning = false;
}

void WarpScanner::buildCandidatePool()
{
    QSet<QString> seen;
    auto *rng = QRandomGenerator::global();

    for (int p = 0; p < protocols::warp::scanCidrPoolCount; ++p) {
        const QString subnet = QString::fromLatin1(protocols::warp::scanCidrPools[p]);
        int picked = 0;
        int attempts = 0;
        // Sample N distinct hosts in 1..254 from each /24
        while (picked < protocols::warp::scanHostsPerPool && attempts < protocols::warp::scanHostsPerPool * 4) {
            ++attempts;
            const int host = int(rng->bounded(1, 255));
            const QString ip = QStringLiteral("%1.%2").arg(subnet).arg(host);
            if (seen.contains(ip)) {
                continue;
            }
            seen.insert(ip);
            m_queue.append(ip);
            ++picked;
        }
    }

    // Shuffle so probing fans out across subnets instead of pool-by-pool
    std::shuffle(m_queue.begin(), m_queue.end(), *rng);
}

void WarpScanner::pumpQueue()
{
    while (m_inFlight < protocols::warp::scanParallelism && m_queuePos < m_queue.size()) {
        const QString ip = m_queue.at(m_queuePos++);
        startProbe(ip);
    }

    if (m_inFlight == 0 && m_queuePos >= m_queue.size()) {
        maybeAdvancePhase();
    }
}

void WarpScanner::startProbe(const QString &ip)
{
    auto *probe = new Probe;
    probe->ip = ip;
    probe->socket = new QTcpSocket(this);
    probe->timer.start();

    ++m_inFlight;
    m_active.append(probe);

    connect(probe->socket, &QTcpSocket::connected, this, [this, probe]() { onProbeSucceeded(probe); });
    connect(probe->socket, &QAbstractSocket::errorOccurred, this, [this, probe](QAbstractSocket::SocketError) {
        onProbeFailed(probe);
    });

    probe->socket->connectToHost(probe->ip, protocols::warp::scanProbePort);

    // Per-probe timeout guard
    QTimer::singleShot(protocols::warp::scanProbeTimeoutMs, probe->socket, [this, probe]() {
        if (m_active.contains(probe) && probe->socket
            && probe->socket->state() != QAbstractSocket::ConnectedState) {
            onProbeFailed(probe);
        }
    });
}

void WarpScanner::onProbeSucceeded(Probe *probe)
{
    if (!m_active.contains(probe)) {
        return;
    }
    const int latency = int(probe->timer.elapsed());
    const QString ip = probe->ip;

    if (m_phase == Phase::Discovery) {
        Result r;
        r.ip = ip;
        r.totalLatency = latency;
        r.samples = 1;
        m_results.append(r);
    } else {
        for (Result &res : m_results) {
            if (res.ip == ip) {
                res.totalLatency += latency;
                res.samples += 1;
                break;
            }
        }
    }

    finishProbe(probe);
}

void WarpScanner::onProbeFailed(Probe *probe)
{
    if (!m_active.contains(probe)) {
        return;
    }
    finishProbe(probe);
}

void WarpScanner::finishProbe(Probe *probe)
{
    m_active.removeOne(probe);
    if (probe->socket) {
        probe->socket->disconnect(this);
        probe->socket->abort();
        probe->socket->deleteLater();
    }
    delete probe;
    --m_inFlight;
    ++m_doneForProgress;
    emit progress(m_doneForProgress, m_totalForProgress);

    // Early stop in discovery once we have enough live candidates
    if (m_phase == Phase::Discovery && m_results.size() >= protocols::warp::scanLiveLimit
        && m_queuePos < m_queue.size()) {
        m_queuePos = m_queue.size(); // stop scheduling new probes
    }

    pumpQueue();
}

void WarpScanner::maybeAdvancePhase()
{
    if (m_phase == Phase::Discovery) {
        if (m_results.isEmpty()) {
            m_scanning = false;
            logger.warning() << "WARP scan found no live endpoints";
            emit scanFailed(tr("Не удалось найти доступный эндпоинт Cloudflare WARP"));
            return;
        }
        startConfirmPhase();
        return;
    }

    finalize();
}

void WarpScanner::startConfirmPhase()
{
    // Pick top-N by discovery latency, re-probe them a few times to average
    std::sort(m_results.begin(), m_results.end(),
              [](const Result &a, const Result &b) { return a.bestLatency() < b.bestLatency(); });

    const int topN = std::min<int>(protocols::warp::scanConfirmTopN, m_results.size());
    m_confirmIps.clear();
    for (int i = 0; i < topN; ++i) {
        m_confirmIps.append(m_results.at(i).ip);
    }
    // Drop the non-top results so finalize() only weighs confirmed candidates
    m_results.erase(m_results.begin() + topN, m_results.end());

    m_phase = Phase::Confirm;
    m_queue.clear();
    for (int round = 0; round < protocols::warp::scanConfirmProbes; ++round) {
        m_queue += m_confirmIps;
    }
    m_queuePos = 0;
    m_totalForProgress += m_queue.size();

    if (m_queue.isEmpty()) {
        finalize();
        return;
    }
    pumpQueue();
}

void WarpScanner::finalize()
{
    m_scanning = false;

    if (m_results.isEmpty()) {
        emit scanFailed(tr("Не удалось найти доступный эндпоинт Cloudflare WARP"));
        return;
    }

    auto best = std::min_element(m_results.begin(), m_results.end(),
                                 [](const Result &a, const Result &b) { return a.bestLatency() < b.bestLatency(); });

    logger.info() << "WARP scan finished, best endpoint" << best->ip << best->bestLatency() << "ms";
    emit scanFinished(best->ip, best->bestLatency());
}
