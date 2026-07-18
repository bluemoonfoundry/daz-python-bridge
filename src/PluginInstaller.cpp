#include "PluginInstaller.h"

PluginInstaller::PluginInstaller(QString pluginsDir, ZipInstaller::Limits limits, QObject *parent)
	: QObject(parent)
	, m_pluginsDir(std::move(pluginsDir))
	, m_limits(limits)
{
}

void PluginInstaller::install(const QString &zipPath) {
	ZipInstaller zipInstaller(m_pluginsDir, m_limits);
	const ZipInstaller::Result result = zipInstaller.install(zipPath);

	if (!result.success) {
		emit extractionFailed(result.errorMessage);
		emit finished(result.pluginId, false, result.errorMessage);
		return;
	}

	m_depInstaller = new PluginDependencyInstaller(this);
	connect(m_depInstaller, &PluginDependencyInstaller::finished, this, &PluginInstaller::finished);
	m_depInstaller->run(result.pluginId, result.finalPluginDir);
}
