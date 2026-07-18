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

	enum class Action { Start, Stop, Restart, Enable, Disable };
	Q_ENUM(Action)

	// Fires the corresponding POST /plugins/{pluginId}/{action} call. Result
	// arrives via actionFinished(); does not block. A refresh() is triggered
	// automatically once the action completes so the pane's list reflects it
	// promptly rather than waiting for the next poll tick.
	void performAction(const QString &pluginId, Action action);

	// Polls once immediately, independent of the timer interval.
	void refresh();

signals:
	// Emitted after every successful poll with the full current plugin list.
	void pluginsUpdated(const QVector<PluginStatus> &plugins);

	// Emitted once per performAction() call.
	void actionFinished(const QString &pluginId, Action action, bool success, const QString &errorMessage);

private:
	void onListReplyFinished(QNetworkReply *reply);
	void onActionReplyFinished(QNetworkReply *reply, const QString &pluginId, Action action);
	static QString actionPath(Action action);

	QNetworkAccessManager *m_networkManager = nullptr;
	QTimer m_timer;
	bool m_requestInFlight = false;
};
