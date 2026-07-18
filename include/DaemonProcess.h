#pragma once

#include <QObject>
#include <QProcess>

// Launches and supervises the DPB daemon subprocess: run_venv's python running
// uvicorn, bound to 127.0.0.1:18812 only (loopback — never 0.0.0.0).
//
// This only tracks OS-process liveness (did the process exit). Application-level
// liveness (is the FastAPI app actually answering requests) is DaemonHealthMonitor's
// job — the two are deliberately separate so a hung-but-still-running daemon can be
// told apart from a crashed one.
class DaemonProcess : public QObject {
	Q_OBJECT
public:
	static constexpr quint16 kPort = 18812;

	explicit DaemonProcess(QObject *parent = nullptr);
	~DaemonProcess() override;

	// No-ops if already running. Assumes UvBootstrapper::ready() has already
	// fired — this does not bootstrap.
	void start();
	void stop();
	bool isRunning() const;
	qint64 processId() const;

signals:
	void started();
	// exitCode/status are only meaningful when wasCrash is true; a clean
	// stop() does not emit this.
	void crashed(int exitCode, QProcess::ExitStatus status);

	// One emission per line of the daemon subprocess's own stdout/stderr
	// (uvicorn's startup banner, request logs, any print() output), so a
	// caller (the status pane) can show what the daemon is actually doing --
	// there is no other way to see this, since nothing else surfaces the
	// daemon's own log output.
	void logLine(const QString &line);

private slots:
	void onStarted();
	void onFinished(int exitCode, QProcess::ExitStatus status);
	void onReadyReadStandardOutput();
	void onReadyReadStandardError();

private:
	void emitBufferedLines(QByteArray &buffer, const QByteArray &chunk);

	QProcess *m_process = nullptr;
	bool m_stopRequested = false;
	QByteArray m_stdoutBuffer;
	QByteArray m_stderrBuffer;
};
