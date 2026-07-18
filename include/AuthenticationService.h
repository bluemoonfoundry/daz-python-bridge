#pragma once
#include <QtCore/qstring.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qreadwritelock.h>
#include <string>

// Manages the DPB daemon's API token: generation, persistence, and the
// in-memory copy DSS attaches as X-DPB-Token on its own outbound calls
// (daz-python-bridge-sop.7).
//
// Ported from daz-script-server's AuthenticationService: DSS is the sole
// generator for both services' tokens (via SecureRandom), each writing its
// own file under ~/.daz3d -- dazscriptserver_token.txt there,
// dazpythonbridge_token.txt here. The Python daemon (daemon/auth.py) only
// ever reads this file at its own startup; it never generates one itself.
//
// Thread-safe: getToken()/setToken() may be called from any thread.
class AuthenticationService {
public:
    AuthenticationService();

    // Loads the existing token file or generates a new one via SecureRandom.
    // Returns false if crypto API is unavailable. outMessages receives log
    // lines the caller should display.
    bool loadOrGenerateToken(QStringList& outMessages);

    // Returns false on failure; outMessage receives a log line to display.
    bool saveToken(QString& outMessage);

    QString getToken() const;
    void    setToken(const QString& token);

    static QString getTokenFilePath();

private:
    QString generateToken() const;

    QString m_sToken;
    mutable QReadWriteLock m_tokenLock;
};
