#pragma once

#include <dzpane.h>

#include "AuthenticationService.h"
#include "DaemonHealthMonitor.h"
#include "DaemonProcess.h"
#include "InlineRunner.h"
#include "PluginStatusManager.h"
#include "UvBootstrapper.h"

#if DAZ_SDK_MAJOR_VERSION >= 6
// These widget classes moved from QtGui to QtWidgets in Qt5/6.
#include <QtWidgets/qwidget.h>
#include <QtWidgets/qtablewidget.h>
#include <QtWidgets/qplaintextedit.h>
#else
#include <QtGui/qwidget.h>
#include <QtGui/qtablewidget.h>
#include <QtGui/qplaintextedit.h>
#endif

class QLabel;
class QPushButton;

// Dockable "Daz Python Bridge" pane shell, registered under Window > Panes by
// the companion DzPythonBridgePaneAction in pluginmain.cpp. Hosts the plugin
// status table and start/stop/restart/enable/disable controls
// (daz-python-bridge-sop.6), driven by PluginStatusManager's async polling
// so nothing here ever blocks the DAZ Studio UI thread.
class DzPythonBridgePane : public DzPane {
	Q_OBJECT

public:
	DzPythonBridgePane();

	// Empty widget wrapping the plugin status table/controls below it.
	QWidget* contentContainer() const { return m_pContentContainer; }

private slots:
	void onPluginsUpdated(const QVector<PluginStatus> &plugins);
	void onActionFinished(const QString &pluginId, PluginStatusManager::Action action, bool success, const QString &errorMessage);
	void onStartClicked();
	void onStopClicked();
	void onRestartClicked();
	void onEnableClicked();
	void onDisableClicked();
	void onTableSelectionChanged();
	void onExecuteClicked();
	void onRunFinished(bool success, const QString &resultJson, const QStringList &output, const QString &error);

	// Daemon lifecycle: bootstrap (uv/Python 3.11/run_venv/deps) -> launch ->
	// health polling. See constructor for the full chain.
	void onBootstrapStepStarted(const QString &step);
	void onBootstrapStepSucceeded(const QString &step);
	void onBootstrapStepFailed(const QString &step, const QString &errorMessage);
	void onBootstrapReady();
	void onDaemonStarted();
	void onDaemonCrashed(int exitCode, QProcess::ExitStatus status);
	void onDaemonLogLine(const QString &line);
	void onHealthUp();
	void onHealthDown();

private:
	QString selectedPluginId() const;
	void performActionOnSelection(PluginStatusManager::Action action);
	void updateActionButtonsEnabled();
	void setDaemonStatus(const QString &text, const QString &color);
	void appendLog(const QString &source, const QString &message);

	QWidget*               m_pContentContainer;
	QTableWidget*          m_pTable;
	QPushButton*           m_pStartButton;
	QPushButton*           m_pStopButton;
	QPushButton*           m_pRestartButton;
	QPushButton*           m_pEnableButton;
	QPushButton*           m_pDisableButton;
	PluginStatusManager*   m_pStatusManager;
	AuthenticationService  m_authService;

	// Script-IDE-style inline execution pane (daz-python-bridge-sop.2): a code
	// editor, an Execute button, and a read-only console showing print()
	// output followed by the returned value or an error.
	QPlainTextEdit*         m_pScriptEditor;
	QPushButton*            m_pExecuteButton;
	QPlainTextEdit*         m_pRunOutput;
	InlineRunner*           m_pInlineRunner;

	// Daemon status/log section (visible at the top of the pane): a
	// color-coded one-line status label plus a running log combining
	// bootstrap steps, daemon process lifecycle events, the daemon
	// subprocess's own stdout/stderr, and health poll transitions -- so it's
	// obvious from the pane alone whether the daemon is up and, if not, why.
	QLabel*                 m_pDaemonStatusLabel;
	QPlainTextEdit*         m_pLogView;
	UvBootstrapper*         m_pBootstrapper;
	DaemonProcess*          m_pDaemonProcess;
	DaemonHealthMonitor*    m_pHealthMonitor;
};
