#include "DaemonProcess.h"

#include "DaemonPaths.h"

#include <QDir>

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

	m_process->setProgram(python);
	m_process->setArguments({
		"-m", "uvicorn",
		"daemon.app:app",
		"--host", "127.0.0.1",
		"--port", QString::number(kPort)
	});
	m_process->setWorkingDirectory(DaemonPaths::baseDir());

	connect(m_process, &QProcess::started, this, &DaemonProcess::onStarted);
	connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
	        this, &DaemonProcess::onFinished);

	m_process->start();
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
	return m_process ? m_process->processId() : 0;
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
