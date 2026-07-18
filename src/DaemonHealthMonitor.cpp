#include "DaemonHealthMonitor.h"

#include "DaemonProcess.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

DaemonHealthMonitor::DaemonHealthMonitor(QObject *parent) : QObject(parent) {
	m_networkManager = new QNetworkAccessManager(this);
	connect(&m_timer, &QTimer::timeout, this, &DaemonHealthMonitor::poll);
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

	QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/health").arg(DaemonProcess::kPort)));
	if (!m_authToken.isEmpty()) {
		request.setRawHeader("X-DPB-Token", m_authToken.toUtf8());
	}
	QNetworkReply *reply = m_networkManager->get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		onReplyFinished(reply);
	});
}

void DaemonHealthMonitor::onReplyFinished(QNetworkReply *reply) {
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
