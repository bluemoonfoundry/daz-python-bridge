#include "DaemonProcess.h"

#include "DaemonPaths.h"

#include <QDir>

// Q_PID on Windows is PROCESS_INFORMATION*, only forward-declared by Qt4's
// own headers -- windows.h is needed for processId() below to dereference it
// (Qt5+'s QProcess::processId() needs none of this) (daz-python-bridge-7wq).
#if DAZ_SDK_MAJOR_VERSION < 6 && defined(Q_OS_WIN)
#include <windows.h>
#endif

DaemonProcess::DaemonProcess(QObject *parent) : QObject(parent) {}

DaemonProcess::~DaemonProcess() {
	stop();
}

void DaemonProcess::start() {
	if (isRunning()) {
		return;
	}

	m_stopRequested = false;
	m_process = new QProcess(this);

#ifdef Q_OS_WIN
	const QString python = QDir(DaemonPaths::runVenvDir()).filePath("Scripts/python.exe");
#else
	const QString python = QDir(DaemonPaths::runVenvDir()).filePath("bin/python");
#endif

	// QStringList() << ... , not a brace-init list: QStringList's
	// initializer-list constructor is Qt5+ only (daz-python-bridge-7wq).
	const QStringList arguments = QStringList()
		<< "-m" << "uvicorn"
		<< "daemon.app:app"
		<< "--host" << "127.0.0.1"
		<< "--port" << QString::number(kPort);
	m_process->setWorkingDirectory(DaemonPaths::baseDir());

	m_stdoutBuffer.clear();
	m_stderrBuffer.clear();

	// Old-style SIGNAL()/SLOT() connect (not PMF-based): this class is
	// compiled against Qt 4.8 for the SDK4 DSS plugin (daz-python-bridge-7wq),
	// which has no function-pointer connect() overload.
	connect(m_process, SIGNAL(started()), this, SLOT(onStarted()));
	connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)),
	        this, SLOT(onFinished(int, QProcess::ExitStatus)));
	connect(m_process, SIGNAL(readyReadStandardOutput()), this, SLOT(onReadyReadStandardOutput()));
	connect(m_process, SIGNAL(readyReadStandardError()), this, SLOT(onReadyReadStandardError()));

	// start(program, arguments) in one call, not setProgram()+setArguments()+
	// start() -- the latter is Qt5.6+ only; the combined-args overload is the
	// one form that's always existed, from Qt4 through Qt6.
	m_process->start(python, arguments);
}

void DaemonProcess::stop() {
	if (!m_process) {
		return;
	}
	m_stopRequested = true;
	m_process->terminate();
	if (!m_process->waitForFinished(3000)) {
		m_process->kill();
		m_process->waitForFinished(1000);
	}
	m_process->deleteLater();
	m_process = nullptr;
}

bool DaemonProcess::isRunning() const {
	return m_process && m_process->state() != QProcess::NotRunning;
}

qint64 DaemonProcess::processId() const {
	if (!m_process) {
		return 0;
	}
#if DAZ_SDK_MAJOR_VERSION >= 6
	return m_process->processId();
#elif defined(Q_OS_WIN)
	// Qt4's QProcess::pid() returns Q_PID, which on Windows is a
	// PROCESS_INFORMATION* rather than a plain integer -- unlike
	// QProcess::processId() (Qt5+), which this replaces here for the SDK4
	// DSS plugin (daz-python-bridge-7wq).
	Q_PID pid = m_process->pid();
	return pid ? static_cast<qint64>(pid->dwProcessId) : 0;
#else
	return static_cast<qint64>(m_process->pid());
#endif
}

void DaemonProcess::onStarted() {
	emit started();
}

void DaemonProcess::onFinished(int exitCode, QProcess::ExitStatus status) {
	const bool wasStopRequested = m_stopRequested;
	if (m_process) {
		m_process->deleteLater();
		m_process = nullptr;
	}
	if (!wasStopRequested) {
		emit crashed(exitCode, status);
	}
}

void DaemonProcess::emitBufferedLines(QByteArray &buffer, const QByteArray &chunk) {
	buffer += chunk;
	int newlineIndex;
	while ((newlineIndex = buffer.indexOf('\n')) != -1) {
		QByteArray line = buffer.left(newlineIndex);
		buffer.remove(0, newlineIndex + 1);
		if (line.endsWith('\r')) {
			line.chop(1);
		}
		emit logLine(QString::fromUtf8(line));
	}
}

void DaemonProcess::onReadyReadStandardOutput() {
	if (m_process) {
		emitBufferedLines(m_stdoutBuffer, m_process->readAllStandardOutput());
	}
}

void DaemonProcess::onReadyReadStandardError() {
	if (m_process) {
		emitBufferedLines(m_stderrBuffer, m_process->readAllStandardError());
	}
}

// Manually included -- see the comment in DaemonHealthMonitor.cpp
// (daz-python-bridge-7wq).
#include "moc_DaemonProcess.cpp"
