#include "InlineRunner.h"

#include "DaemonProcess.h"
#include "JsonStd.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {

QString baseUrl() {
	return QString::fromLatin1("http://127.0.0.1:%1").arg(DaemonProcess::kPort);
}

QString jsonText(const QVariant &value) {
	if (value.type() == QVariant::String) {
		return value.toString();
	}
	if (!value.isValid() || value.isNull()) {
		return QString::fromLatin1("null");
	}
	return QString::fromStdString(JsonStd::variantToJson(value));
}

} // namespace

InlineRunner::InlineRunner(QObject *parent) : QObject(parent) {
	m_networkManager = new QNetworkAccessManager(this);
}

void InlineRunner::execute(const QString &code) {
	if (m_requestInFlight) {
		return;
	}
	m_requestInFlight = true;

	QNetworkRequest request(QUrl(baseUrl() + "/run"));
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	if (!m_authToken.isEmpty()) {
		request.setRawHeader("X-DPB-Token", m_authToken.toUtf8());
	}

	QVariantMap body;
	body["code"] = code;
	const QByteArray payload = JsonStd::variantToJsonBytes(body);

	QNetworkReply *reply = m_networkManager->post(request, payload);
	// Old-style SIGNAL()/SLOT() connect -- see the header comment on
	// onReplyFinished().
	connect(reply, SIGNAL(finished()), this, SLOT(onReplyFinished()));
}

void InlineRunner::onReplyFinished() {
	QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
	if (!reply) {
		return;
	}

	m_requestInFlight = false;
	reply->deleteLater();

	if (reply->error() != QNetworkReply::NoError) {
		QString errorMessage = reply->errorString();
		QVariantMap body;
		std::string parseError;
		if (JsonStd::parseObject(reply->readAll(), body, parseError) && body.contains("detail")) {
			errorMessage = body.value("detail").toString();
		}
		emit runFinished(false, QString::fromLatin1("null"), QStringList(), errorMessage);
		return;
	}

	QVariantMap body;
	std::string parseError;
	if (!JsonStd::parseObject(reply->readAll(), body, parseError)) {
		emit runFinished(false, QString::fromLatin1("null"), QStringList(),
		                  QString::fromLatin1("Malformed response from daemon"));
		return;
	}

	const bool success = body.value("success").toBool();
	const QString resultJson = jsonText(body.value("result"));
	QStringList output;
	for (const QVariant &line : body.value("output").toList()) {
		output.append(line.toString());
	}
	const QString error = body.value("error").toString();

	emit runFinished(success, resultJson, output, error);
}

// Manually included -- see the comment in DaemonHealthMonitor.cpp
// (daz-python-bridge-7wq).
#include "moc_InlineRunner.cpp"
