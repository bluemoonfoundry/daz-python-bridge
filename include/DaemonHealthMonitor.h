#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

// Polls GET http://127.0.0.1:18812/health on a QTimer using async
// QNetworkAccessManager requests — never a blocking call on the Qt main
// thread, matching the existing AsyncRequestManager polling convention used
// elsewhere in DSS for outbound daemon calls.
//
// This is how DSS tells "daemon process is running" (DaemonProcess::isRunning)
// apart from "daemon is actually answering requests" (this class) — the plugin
// status pane consumes both.
class DaemonHealthMonitor : public QObject {
	Q_OBJECT
public:
	explicit DaemonHealthMonitor(QObject *parent = nullptr);

	// intervalMs: how often to poll. Defaults to 2000ms.
	void start(int intervalMs = 2000);
	void stop();
	bool isUp() const { return m_isUp; }

	// Attaches X-DPB-Token to outbound requests (daz-python-bridge-sop.7).
	// /health itself doesn't require it, but sending it is harmless and keeps
	// this class consistent with PluginStatusManager's authenticated calls.
	void setAuthToken(const QString &token) { m_authToken = token; }

signals:
	// Fires only on state transitions, not on every poll.
	void healthUp();
	void healthDown();

private slots:
	void poll();
	// No QNetworkReply* param: connected via old-style SIGNAL()/SLOT() macros
	// (this class is compiled against Qt 4.8 for the SDK4 DSS plugin,
	// daz-python-bridge-7wq, which has no PMF-based connect()/lambda-slot
	// support), so the reply that fired finished() is recovered via sender()
	// instead of being lambda-captured.
	void onReplyFinished();

private:
	QNetworkAccessManager *m_networkManager = nullptr;
	QTimer m_timer;
	bool m_isUp = false;
	bool m_requestInFlight = false;
	QString m_authToken;
};
