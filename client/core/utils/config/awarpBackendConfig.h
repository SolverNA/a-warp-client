#ifndef AWARPBACKENDCONFIG_H
#define AWARPBACKENDCONFIG_H

#include <QString>
#include <QVector>

// AWARP: in-app backend configuration baked into the binary as a Qt resource
// (:/awarp/warp_bootstrap.json). Provides the bootstrap fallback endpoint used
// when the direct Cloudflare registration fails. The real file is gitignored and
// carries a live API secret; the committed *.example.json only has placeholders.
//
// The secret is NEVER logged (masked everywhere). Loaded once and cached.
namespace AwarpBackendConfig
{
    struct Relay
    {
        QString host;
        int port = 0;
    };

    bool bootstrapEnabled();
    QString bootstrapUrl();
    QString bootstrapSecretHeader();
    QString bootstrapSecret();
    int bootstrapTimeoutMs();
    QVector<Relay> relays();
    QString updateUrl();
} // namespace AwarpBackendConfig

#endif // AWARPBACKENDCONFIG_H
