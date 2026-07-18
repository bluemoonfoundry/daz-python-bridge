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
		return QString::fromLatin1("—");
	}
	return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

QString formatPid(qint64 pid) {
	return pid < 0 ? QString::fromLatin1("—") : QString::number(pid);
}

QString formatLastUsed(double lastUsed) {
	return lastUsed <= 0 ? QString::fromLatin1("never") : QString::fromLatin1("%1s ago").arg((int)lastUsed);
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

	// Old-style SIGNAL()/SLOT() connect throughout this constructor, not the
	// PMF-based connect() syntax: this pane is compiled against Qt 4.8 for
	// the SDK4 build (daz-python-bridge-7wq), which predates Qt5's
	// function-pointer connect() entirely.
	connect(m_pStartButton, SIGNAL(clicked()), this, SLOT(onStartClicked()));
	connect(m_pStopButton, SIGNAL(clicked()), this, SLOT(onStopClicked()));
	connect(m_pRestartButton, SIGNAL(clicked()), this, SLOT(onRestartClicked()));
	connect(m_pEnableButton, SIGNAL(clicked()), this, SLOT(onEnableClicked()));
	connect(m_pDisableButton, SIGNAL(clicked()), this, SLOT(onDisableClicked()));

	// ─── Script-IDE-style inline execution section (daz-python-bridge-sop.2) ───
	QFrame* runSep = new QFrame(this);
	runSep->setFrameShape(QFrame::HLine);
	runSep->setFrameShadow(QFrame::Sunken);

	QLabel* runLabel = new QLabel("Run Python (isolated run_venv)", m_pContentContainer);

	m_pScriptEditor = new QPlainTextEdit(m_pContentContainer);
#if DAZ_SDK_MAJOR_VERSION >= 6
	// setPlaceholderText (Qt5.3+), setTabStopDistance/horizontalAdvance
	// (Qt5.10+/5.11+) have no Qt4 equivalents that matter here -- SDK4
	// (daz-python-bridge-7wq) just gets the older setTabStopWidth() and no
	// placeholder text, a purely cosmetic difference.
	m_pScriptEditor->setPlaceholderText("print('hello')\nresult = 1 + 1\nresult");
	m_pScriptEditor->setTabStopDistance(4 * m_pScriptEditor->fontMetrics().horizontalAdvance(' '));
#else
	m_pScriptEditor->setTabStopWidth(4 * m_pScriptEditor->fontMetrics().width(' '));
#endif

	m_pExecuteButton = new QPushButton("Execute", m_pContentContainer);

	m_pRunOutput = new QPlainTextEdit(m_pContentContainer);
	m_pRunOutput->setReadOnly(true);
#if DAZ_SDK_MAJOR_VERSION >= 6
	m_pRunOutput->setPlaceholderText("Output and result appear here.");
#endif

	contentLayout->addWidget(runSep);
	contentLayout->addWidget(runLabel);
	contentLayout->addWidget(m_pScriptEditor);
	contentLayout->addWidget(m_pExecuteButton);
	contentLayout->addWidget(m_pRunOutput);

	connect(m_pExecuteButton, SIGNAL(clicked()), this, SLOT(onExecuteClicked()));

	// DSS is the sole generator for the daemon's token (daz-python-bridge-sop.7);
	// the daemon only ever reads dazpythonbridge_token.txt at its own startup.
	QStringList authMessages;
	m_authService.loadOrGenerateToken(authMessages);

	m_pStatusManager = new PluginStatusManager(this);
	m_pStatusManager->setAuthToken(m_authService.getToken());
	connect(m_pStatusManager, SIGNAL(pluginsUpdated(QVector<PluginStatus>)),
	        this, SLOT(onPluginsUpdated(QVector<PluginStatus>)));
	// Unqualified "Action" (not "PluginStatusManager::Action") to match moc's
	// registered signature: it normalizes each signal's parameter types as
	// literally spelled at the signal's own declaration site (inside the
	// class body, where "Action" needs no qualification) -- writing the
	// qualified name here would silently fail to match at connect() time.
	connect(m_pStatusManager, SIGNAL(actionFinished(QString, Action, bool, QString)),
	        this, SLOT(onActionFinished(QString, PluginStatusManager::Action, bool, QString)));
	m_pStatusManager->start(kPollIntervalMs);

	m_pInlineRunner = new InlineRunner(this);
	m_pInlineRunner->setAuthToken(m_authService.getToken());
	connect(m_pInlineRunner, SIGNAL(runFinished(bool, QString, QStringList, QString)),
	        this, SLOT(onRunFinished(bool, QString, QStringList, QString)));

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

void DzPythonBridgePane::onExecuteClicked() {
	m_pExecuteButton->setEnabled(false);
	m_pRunOutput->setPlainText("Running...");
	m_pInlineRunner->execute(m_pScriptEditor->toPlainText());
}

void DzPythonBridgePane::onRunFinished(bool success, const QString &resultJson,
                                        const QStringList &output, const QString &error) {
	m_pExecuteButton->setEnabled(true);

	QString text = output.join("\n");
	if (!text.isEmpty()) {
		text += "\n";
	}
	text += success ? QString("=> %1").arg(resultJson) : QString("Error: %1").arg(error);
	m_pRunOutput->setPlainText(text);
}

#include "moc_DzPythonBridgePane.cpp"
