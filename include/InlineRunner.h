#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

// Fires POST http://127.0.0.1:18812/run with an async QNetworkAccessManager
// request -- never a blocking call on the Qt main thread, matching
// PluginStatusManager's polling/action convention. Backs the Script-IDE-style
// pane's Execute control (daz-python-bridge-sop.2).
class InlineRunner : public QObject {
	Q_OBJECT
public:
	explicit InlineRunner(QObject *parent = nullptr);

	// Attaches X-DPB-Token to every request this class makes (daz-python-bridge-sop.7).
	// Must be set before execute() for the daemon to accept the call.
	void setAuthToken(const QString &token) { m_authToken = token; }

	// Fires POST /run with the given script as its "code" field. Result
	// arrives via runFinished(); does not block. Ignored if a previous call
	// is still in flight.
	void execute(const QString &code);

signals:
	// result/output/error mirror daemon/inline_runtime.py's response envelope:
	// {"success": bool, "result": <json>, "output": [str...], "error": str}.
	// resultJson is the raw JSON text of `result` (e.g. "2", "\"hi\"", "null"),
	// left to the caller to display since it may be any JSON type.
	void runFinished(bool success, const QString &resultJson, const QStringList &output, const QString &error);

private slots:
	// No QNetworkReply* param: connected via old-style SIGNAL()/SLOT() macros
	// (this class is compiled against Qt 4.8 for the SDK4 DSS plugin,
	// daz-python-bridge-7wq, which has no PMF-based connect()/lambda-slot
	// support), so the reply that fired finished() is recovered via sender().
	void onReplyFinished();

private:
	QNetworkAccessManager *m_networkManager = nullptr;
	QString m_authToken;
	bool m_requestInFlight = false;
};
