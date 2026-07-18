#include "InlineRunner.h"

#include "DaemonProcess.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {

QString baseUrl() {
	return QStringLiteral("http://127.0.0.1:%1").arg(DaemonProcess::kPort);
}

QString jsonText(const QJsonValue &value) {
	if (value.isString()) {
		return value.toString();
	}
	if (value.isNull() || value.isUndefined()) {
		return QStringLiteral("null");
	}
	// Numbers/bools/objects/arrays: round-trip through a single-element array
	// so QJsonDocument can serialize any JSON value type, not just objects.
	QJsonArray wrapper{value};
	QByteArray wrapped = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
	return QString::fromUtf8(wrapped.mid(1, wrapped.size() - 2));
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

	QJsonObject body;
	body["code"] = code;
	QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

	QNetworkReply *reply = m_networkManager->post(request, payload);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		onReplyFinished(reply);
	});
}

void InlineRunner::onReplyFinished(QNetworkReply *reply) {
	m_requestInFlight = false;
	reply->deleteLater();

	if (reply->error() != QNetworkReply::NoError) {
		QString errorMessage = reply->errorString();
		const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
		if (doc.isObject() && doc.object().contains("detail")) {
			errorMessage = doc.object().value("detail").toString();
		}
		emit runFinished(false, QStringLiteral("null"), QStringList(), errorMessage);
		return;
	}

	const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
	if (!doc.isObject()) {
		emit runFinished(false, QStringLiteral("null"), QStringList(),
		                  QStringLiteral("Malformed response from daemon"));
		return;
	}

	const QJsonObject obj = doc.object();
	const bool success = obj.value("success").toBool();
	const QString resultJson = jsonText(obj.value("result"));
	QStringList output;
	for (const QJsonValue &line : obj.value("output").toArray()) {
		output.append(line.toString());
	}
	const QString error = obj.value("error").toString();

	emit runFinished(success, resultJson, output, error);
}
