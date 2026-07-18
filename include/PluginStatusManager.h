#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

// One row of GET /plugins, as reported by daemon/plugin_registry.py's
// PluginRegistry.status(): state is one of "resolving", "ready", "starting",
// "running", "failed", "disabled".
struct PluginStatus {
	QString pluginId;
	QString state;
	qint64  pid = -1;          // -1 if the plugin has no live worker
	qint64  memoryBytes = -1;  // -1 if unknown (psutil unavailable, or no live worker)
	double  lastUsed = 0;      // monotonic seconds from WorkerManager; 0 if never called
};

// Polls GET http://127.0.0.1:18812/plugins on a QTimer using async
// QNetworkAccessManager requests -- never a blocking call on the Qt main
// thread, matching DaemonHealthMonitor's polling convention. Also issues the
// POST /plugins/{id}/{start,stop,restart,enable,disable} action calls the
// status pane's controls (daz-python-bridge-sop.6) trigger.
class PluginStatusManager : public QObject {
	Q_OBJECT
public:
	explicit PluginStatusManager(QObject *parent = nullptr);

	// intervalMs: how often to poll GET /plugins. Defaults to 2000ms.
	void start(int intervalMs = 2000);
	void stop();

	// Attaches X-DPB-Token to every request this class makes (daz-python-bridge-sop.7).
	// Must be set before start()/performAction() for the daemon to accept them,
	// since /plugins/* rejects requests without a valid token.
	void setAuthToken(const QString &token) { m_authToken = token; }

	enum class Action { Start, Stop, Restart, Enable, Disable };
	// Q_ENUMS (not Q_ENUM): Q_ENUM is Qt 5.5+ only, and this header is
	// compiled against Qt 4.8 for the SDK4 DSS plugin (daz-python-bridge-7wq).
	// Verified against the real SDK4 moc.exe (Qt 4.8.1): it parses
	// "enum class" fine and registers signals using the enum's unqualified
	// name (e.g. actionFinished(QString,Action,bool)), which is why the
	// connect() calls in DzPythonBridgePane.cpp use unqualified "Action".
	Q_ENUMS(Action)

	// Fires the corresponding POST /plugins/{pluginId}/{action} call. Result
	// arrives via actionFinished(); does not block. A refresh() is triggered
	// automatically once the action completes so the pane's list reflects it
	// promptly rather than waiting for the next poll tick.
	void performAction(const QString &pluginId, Action action);

signals:
	// Emitted after every successful poll with the full current plugin list.
	void pluginsUpdated(const QVector<PluginStatus> &plugins);

	// Emitted once per performAction() call.
	void actionFinished(const QString &pluginId, Action action, bool success, const QString &errorMessage);

public slots:
	// Polls once immediately, independent of the timer interval. A slot (not
	// just a plain method) so m_timer's timeout() can target it via
	// old-style SIGNAL()/SLOT() connect -- this class is compiled against
	// Qt 4.8 for the SDK4 DSS plugin (daz-python-bridge-7wq), which has no
	// PMF-based connect().
	void refresh();

private slots:
	// No QNetworkReply*/pluginId/Action params: connected via old-style
	// SIGNAL()/SLOT() macros, which can't lambda-capture extra context the
	// way the PMF-based connect() this used to use could. The reply itself
	// comes back via sender(); onActionReplyFinished's extra pluginId/action
	// context travels as dynamic properties stashed on the reply in
	// performAction() below (see the "dpb_" properties there).
	void onListReplyFinished();
	void onActionReplyFinished();

private:
	static QString actionPath(Action action);

	QNetworkAccessManager *m_networkManager = nullptr;
	QTimer m_timer;
	bool m_requestInFlight = false;
	QString m_authToken;
};
