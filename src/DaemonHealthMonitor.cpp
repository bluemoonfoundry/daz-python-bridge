#include "DaemonHealthMonitor.h"

#include "DaemonProcess.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

DaemonHealthMonitor::DaemonHealthMonitor(QObject *parent) : QObject(parent) {
	m_networkManager = new QNetworkAccessManager(this);
	// Old-style SIGNAL()/SLOT() connect, not PMF-based -- see the class
	// comment on onReplyFinished() below.
	connect(&m_timer, SIGNAL(timeout()), this, SLOT(poll()));
}

void DaemonHealthMonitor::start(int intervalMs) {
	m_timer.start(intervalMs);
	poll();
}

void DaemonHealthMonitor::stop() {
	m_timer.stop();
}

void DaemonHealthMonitor::poll() {
	// Skip this tick if the previous request hasn't completed yet — avoids
	// piling up requests if the daemon is slow to respond.
	if (m_requestInFlight) {
		return;
	}
	m_requestInFlight = true;

	QNetworkRequest request(QUrl(QString::fromLatin1("http://127.0.0.1:%1/health").arg(DaemonProcess::kPort)));
	if (!m_authToken.isEmpty()) {
		request.setRawHeader("X-DPB-Token", m_authToken.toUtf8());
	}
	QNetworkReply *reply = m_networkManager->get(request);
	connect(reply, SIGNAL(finished()), this, SLOT(onReplyFinished()));
}

void DaemonHealthMonitor::onReplyFinished() {
	QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
	if (!reply) {
		return;
	}

	m_requestInFlight = false;
	const bool ok = reply->error() == QNetworkReply::NoError;
	reply->deleteLater();

	if (ok && !m_isUp) {
		m_isUp = true;
		emit healthUp();
	} else if (!ok && m_isUp) {
		m_isUp = false;
		emit healthDown();
	}
}

// Manually included (not left to AUTOMOC's aggregated mocs_compilation.cpp)
// because this file is compiled twice with different AUTOMOC_MOC_OPTIONS:
// once into DazPythonBridgeCore (Qt6, default self-including moc output) and
// once directly into DzPythonBridge for SDK4 (Qt 4.8), whose target sets
// "-i" globally for pluginmain.cpp's header-less inline class -- which would
// otherwise also suppress this header's self-include (daz-python-bridge-7wq).
#include "moc_DaemonHealthMonitor.cpp"
