#ifndef WARPSCANNER_H
#define WARPSCANNER_H

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QVector>

class QTcpSocket;

/**
 * WarpScanner finds the lowest-latency Cloudflare WARP endpoint by performing
 * plain TCP-connect probes to port 80 across the known WARP /24 pools.
 *
 * Probing is async and batched (parallelism ~64, per-probe timeout ~1s). It
 * never opens a tunnel — a TCP handshake to :80 is harmless and the handshake
 * time is used as the latency metric. Top candidates are re-probed and averaged
 * for stability; the endpoint with the minimum average latency wins.
 */
class WarpScanner : public QObject
{
    Q_OBJECT

public:
    explicit WarpScanner(QObject *parent = nullptr);
    ~WarpScanner() override;

    bool isScanning() const { return m_scanning; }

public slots:
    // Starts an async scan. Emits scanFinished or scanFailed exactly once.
    void scanBest();
    // Aborts an in-flight scan (no signal emitted afterwards).
    void cancel();

signals:
    void scanFinished(const QString &bestIp, int latencyMs);
    void scanFailed(const QString &errorMessage);
    void progress(int done, int total);

private:
    struct Probe
    {
        QString ip;
        QTcpSocket *socket = nullptr;
        QElapsedTimer timer;
    };

    struct Result
    {
        QString ip;
        qint64 totalLatency = 0; // sum of latencies across probes
        int samples = 0;         // number of successful probes
        int bestLatency() const { return samples > 0 ? int(totalLatency / samples) : 0; }
    };

    void buildCandidatePool();
    void pumpQueue();
    void startProbe(const QString &ip);
    void onProbeSucceeded(Probe *probe);
    void onProbeFailed(Probe *probe);
    void finishProbe(Probe *probe);
    void maybeAdvancePhase();
    void startConfirmPhase();
    void finalize();

    bool m_scanning = false;

    // Phase 1: discovery across the pool. Phase 2: confirm top-N.
    enum class Phase { Discovery, Confirm };
    Phase m_phase = Phase::Discovery;

    QVector<QString> m_queue;      // IPs still to probe
    int m_queuePos = 0;
    int m_inFlight = 0;
    int m_totalForProgress = 0;
    int m_doneForProgress = 0;

    QList<Probe *> m_active;
    QList<Result> m_results;       // discovery results (live hosts)
    QVector<QString> m_confirmIps; // top-N IPs being confirmed
};

#endif // WARPSCANNER_H
