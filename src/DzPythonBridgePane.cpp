#include "DzPythonBridgePane.h"
#include "common_version.h"

#if DAZ_SDK_MAJOR_VERSION >= 6
#include <QtWidgets/qboxlayout.h>
#include <QtWidgets/qframe.h>
#include <QtWidgets/qheaderview.h>
#include <QtWidgets/qlabel.h>
#include <QtWidgets/qpushbutton.h>
#else
#include <QtGui/qboxlayout.h>
#include <QtGui/qframe.h>
#include <QtGui/qheaderview.h>
#include <QtGui/qlabel.h>
#include <QtGui/qpushbutton.h>
#endif

namespace {

const int kPollIntervalMs = 2000;
const int kColPlugin = 0;
const int kColState = 1;
const int kColPid = 2;
const int kColMemory = 3;
const int kColLastUsed = 4;

QString formatMemory(qint64 bytes) {
	if (bytes < 0) {
		return QStringLiteral("—");
	}
	return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

QString formatPid(qint64 pid) {
	return pid < 0 ? QStringLiteral("—") : QString::number(pid);
}

QString formatLastUsed(double lastUsed) {
	return lastUsed <= 0 ? QStringLiteral("never") : QStringLiteral("%1s ago").arg((int)lastUsed);
}

} // namespace

DzPythonBridgePane::DzPythonBridgePane()
	: DzPane("Daz Python Bridge")
{
	QLabel* titleLabel = new QLabel(
		QString("Daz Python Bridge  v%1").arg(DPB_VERSION_STR), this);
	titleLabel->setStyleSheet(
		"QLabel { font-size: 11pt; font-weight: bold; padding: 4px 0px; }");
	titleLabel->setAlignment(Qt::AlignCenter);

	QFrame* titleSep = new QFrame(this);
	titleSep->setFrameShape(QFrame::HLine);
	titleSep->setFrameShadow(QFrame::Sunken);

	m_pContentContainer = new QWidget(this);
	QVBoxLayout* contentLayout = new QVBoxLayout(m_pContentContainer);
	contentLayout->setContentsMargins(0, 0, 0, 0);

	m_pTable = new QTableWidget(0, 5, m_pContentContainer);
	m_pTable->setHorizontalHeaderLabels(
		QStringList() << "Plugin" << "State" << "PID" << "Memory" << "Last Used");
	m_pTable->horizontalHeader()->setStretchLastSection(true);
	m_pTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_pTable->setSelectionMode(QAbstractItemView::SingleSelection);
	m_pTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	contentLayout->addWidget(m_pTable);

	QHBoxLayout* buttonLayout = new QHBoxLayout();
	m_pStartButton = new QPushButton("Start", m_pContentContainer);
	m_pStopButton = new QPushButton("Stop", m_pContentContainer);
	m_pRestartButton = new QPushButton("Restart", m_pContentContainer);
	m_pEnableButton = new QPushButton("Enable", m_pContentContainer);
	m_pDisableButton = new QPushButton("Disable", m_pContentContainer);
	buttonLayout->addWidget(m_pStartButton);
	buttonLayout->addWidget(m_pStopButton);
	buttonLayout->addWidget(m_pRestartButton);
	buttonLayout->addWidget(m_pEnableButton);
	buttonLayout->addWidget(m_pDisableButton);
	contentLayout->addLayout(buttonLayout);

	connect(m_pStartButton, &QPushButton::clicked, this, &DzPythonBridgePane::onStartClicked);
	connect(m_pStopButton, &QPushButton::clicked, this, &DzPythonBridgePane::onStopClicked);
	connect(m_pRestartButton, &QPushButton::clicked, this, &DzPythonBridgePane::onRestartClicked);
	connect(m_pEnableButton, &QPushButton::clicked, this, &DzPythonBridgePane::onEnableClicked);
	connect(m_pDisableButton, &QPushButton::clicked, this, &DzPythonBridgePane::onDisableClicked);

	m_pStatusManager = new PluginStatusManager(this);
	connect(m_pStatusManager, &PluginStatusManager::pluginsUpdated,
	        this, &DzPythonBridgePane::onPluginsUpdated);
	connect(m_pStatusManager, &PluginStatusManager::actionFinished,
	        this, &DzPythonBridgePane::onActionFinished);
	m_pStatusManager->start(kPollIntervalMs);

	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(4, 4, 4, 4);
	mainLayout->addWidget(titleLabel);
	mainLayout->addWidget(titleSep);
	mainLayout->addWidget(m_pContentContainer);
	setLayout(mainLayout);
}

void DzPythonBridgePane::onPluginsUpdated(const QVector<PluginStatus> &plugins) {
	const QString previouslySelected = selectedPluginId();

	m_pTable->setRowCount(plugins.size());
	int restoredRow = -1;
	for (int row = 0; row < plugins.size(); ++row) {
		const PluginStatus &status = plugins.at(row);
		m_pTable->setItem(row, kColPlugin, new QTableWidgetItem(status.pluginId));
		m_pTable->setItem(row, kColState, new QTableWidgetItem(status.state));
		m_pTable->setItem(row, kColPid, new QTableWidgetItem(formatPid(status.pid)));
		m_pTable->setItem(row, kColMemory, new QTableWidgetItem(formatMemory(status.memoryBytes)));
		m_pTable->setItem(row, kColLastUsed, new QTableWidgetItem(formatLastUsed(status.lastUsed)));
		if (status.pluginId == previouslySelected) {
			restoredRow = row;
		}
	}
	if (restoredRow >= 0) {
		m_pTable->selectRow(restoredRow);
	}
}

void DzPythonBridgePane::onActionFinished(const QString &pluginId, PluginStatusManager::Action action,
                                           bool success, const QString &errorMessage) {
	Q_UNUSED(action);
	if (!success) {
		// Non-blocking status feedback; a full toast/notification surface is
		// out of scope here -- the daemon's error reason lands in the title.
		m_pTable->setToolTip(QString("%1: %2").arg(pluginId, errorMessage));
	}
}

QString DzPythonBridgePane::selectedPluginId() const {
	const int row = m_pTable->currentRow();
	if (row < 0) {
		return QString();
	}
	QTableWidgetItem *item = m_pTable->item(row, kColPlugin);
	return item ? item->text() : QString();
}

void DzPythonBridgePane::performActionOnSelection(PluginStatusManager::Action action) {
	const QString pluginId = selectedPluginId();
	if (pluginId.isEmpty()) {
		return;
	}
	m_pStatusManager->performAction(pluginId, action);
}

void DzPythonBridgePane::onStartClicked() { performActionOnSelection(PluginStatusManager::Action::Start); }
void DzPythonBridgePane::onStopClicked() { performActionOnSelection(PluginStatusManager::Action::Stop); }
void DzPythonBridgePane::onRestartClicked() { performActionOnSelection(PluginStatusManager::Action::Restart); }
void DzPythonBridgePane::onEnableClicked() { performActionOnSelection(PluginStatusManager::Action::Enable); }
void DzPythonBridgePane::onDisableClicked() { performActionOnSelection(PluginStatusManager::Action::Disable); }

#include "moc_DzPythonBridgePane.cpp"
