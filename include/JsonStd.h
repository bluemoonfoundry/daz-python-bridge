#pragma once
// Thread-safe std::string/QVariant JSON utilities.
//
// Ported from daz-script-server's include/JsonStd.h (same dual-SDK problem:
// Qt 4.8, bundled with the DAZ Studio 4.5+ SDK, has no QJsonDocument -- that's
// a Qt 5.0+ class). Every JSON call site in DazPythonBridgeCore
// (daz-python-bridge-sop.3/.4/.6/.8, plus InlineRunner from sop.2) goes
// through this header instead of touching QJsonDocument/QJsonObject/
// QJsonArray directly, so the exact same .cpp files compile unmodified
// against either Qt6 (DazPythonBridgeCore's own standalone build, and the
// SDK6 DSS plugin) or Qt 4.8 + QtScript (the SDK4 DSS plugin, which links
// DZSDK_QT_SCRIPT_TARGET already) -- see daz-python-bridge-7wq.
//
// DAZ_SDK_MAJOR_VERSION must always be defined when this header is included:
// DazPythonBridgeCore's own CMakeLists.txt sets it to 6 unconditionally
// (matching the Qt6 it's always built against); DzPythonBridge's SDK4/SDK6
// targets set it to the real DAZ_SDK_VERSION.

#include <string>
#include <cstdio>
#include <ctime>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>
#include <QtCore/qmap.h>
#include <QtCore/qbytearray.h>

#if DAZ_SDK_MAJOR_VERSION >= 6
#include <QtCore/qjsondocument.h>
#include <QtCore/qjsonobject.h>
#else
// Qt 4.8 has no QJsonDocument -- fall back to QtScript's JS evaluator, same
// as daz-script-server's SDK4 build.
#include <QtScript/qscriptengine.h>
#include <QtScript/qscriptvalue.h>
#endif

namespace JsonStd {

// QString -> std::string via UTF-8.
inline std::string qstrToStd(const QString &s)
{
	QByteArray ba = s.toUtf8();
	return std::string(ba.constData(), ba.size());
}

// JSON-escape a UTF-8 std::string. Control characters below 0x20 that are not
// one of the named escapes are emitted as \uXXXX sequences.
inline std::string escape(const std::string &s)
{
	std::string r;
	r.reserve(s.size() + 4);
	for (size_t i = 0; i < s.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(s[i]);
		if      (c == '"')  r += "\\\"";
		else if (c == '\\') r += "\\\\";
		else if (c == '\n') r += "\\n";
		else if (c == '\r') r += "\\r";
		else if (c == '\t') r += "\\t";
		else if (c < 0x20)  { char esc[8]; std::snprintf(esc, sizeof(esc), "\\u%04x", c); r += esc; }
		else                r += static_cast<char>(c);
	}
	return r;
}

// Recursively serialize a QVariant to a JSON value string.
inline std::string variantToJson(const QVariant &v)
{
	if (!v.isValid() || v.isNull()) return "null";
	switch (v.type()) {
	case QVariant::Bool:
		return v.toBool() ? "true" : "false";
	case QVariant::Int:
	case QVariant::LongLong:
	case QVariant::UInt:
	case QVariant::ULongLong:
		return std::to_string(v.toLongLong());
	case QVariant::Double: {
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.15g", v.toDouble());
		return buf;
	}
	case QVariant::String:
		return "\"" + escape(qstrToStd(v.toString())) + "\"";
	case QVariant::List:
	case QVariant::StringList: {
		QVariantList list = v.toList();
		std::string s = "[";
		for (int i = 0; i < list.size(); ++i) {
			if (i > 0) s += ",";
			s += variantToJson(list.at(i));
		}
		s += "]";
		return s;
	}
	case QVariant::Map: {
		QVariantMap map = v.toMap();
		std::string s = "{";
		bool first = true;
		for (QVariantMap::const_iterator it = map.begin(); it != map.end(); ++it) {
			if (!first) s += ",";
			first = false;
			s += "\"" + escape(qstrToStd(it.key())) + "\":";
			s += variantToJson(it.value());
		}
		s += "}";
		return s;
	}
	default:
		return "\"" + escape(qstrToStd(v.toString())) + "\"";
	}
}

// Convenience wrapper: variantToJson() as a QByteArray, ready to write to a
// file or use as an HTTP request body.
inline QByteArray variantToJsonBytes(const QVariant &v)
{
	const std::string s = variantToJson(v);
	return QByteArray(s.data(), static_cast<int>(s.size()));
}

#if DAZ_SDK_MAJOR_VERSION >= 6

// Parse a JSON object body into a QVariantMap. QJsonDocument::fromJson is
// reentrant and has no QObject/thread affinity, so this is safe to call from
// any thread.
// Returns false and fills errorDetail with a human-readable message on
// failure (object expected at the top level counts as failure too).
inline bool parseObject(const QByteArray &jsonBody, QVariantMap &out, std::string &errorDetail)
{
	QJsonParseError err;
	QJsonDocument doc = QJsonDocument::fromJson(jsonBody, &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject()) {
		char buf[32];
		std::snprintf(buf, sizeof(buf), "offset %d: ", err.offset);
		errorDetail = buf + qstrToStd(err.error != QJsonParseError::NoError
			? err.errorString() : QString("expected a JSON object"));
		return false;
	}
	out = doc.object().toVariantMap();
	return true;
}

#else

// Parse a JSON object body into a QVariantMap via QtScript's JS evaluator
// (Qt 4.8 has no QJsonDocument). QScriptEngine is a QObject, so -- unlike the
// Qt6 overload above -- this must only be called on the Qt main thread.
// Returns false and fills errorDetail with a human-readable message on
// failure (object expected at the top level counts as failure too).
inline bool parseObject(const QByteArray &jsonBody, QVariantMap &out, std::string &errorDetail)
{
	QString bodyStr = QString::fromUtf8(jsonBody.constData(), jsonBody.size());
	QScriptEngine parseEngine;
	QScriptValue parsed = parseEngine.evaluate("(" + bodyStr + ")");
	if (parseEngine.hasUncaughtException()) {
		char buf[32];
		std::snprintf(buf, sizeof(buf), "line %d: ", parseEngine.uncaughtExceptionLineNumber());
		errorDetail = buf + qstrToStd(parseEngine.uncaughtException().toString());
		return false;
	}
	if (!parsed.isObject()) {
		errorDetail = "expected a JSON object";
		return false;
	}
	out = parsed.toVariant().toMap();
	return true;
}

#endif

} // namespace JsonStd
