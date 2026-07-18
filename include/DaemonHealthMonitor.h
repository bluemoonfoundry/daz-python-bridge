#pragma once

#include <QObject>
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

signals:
	// Fires only on state transitions, not on every poll.
	void healthUp();
	void healthDown();

private slots:
	void poll();
	void onReplyFinished(QNetworkReply *reply);

private:
	QNetworkAccessManager *m_networkManager = nullptr;
	QTimer m_timer;
	bool m_isUp = false;
	bool m_requestInFlight = false;
};
