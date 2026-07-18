#pragma once

#include <dzpane.h>

#include "AuthenticationService.h"
#include "InlineRunner.h"
#include "PluginStatusManager.h"

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
	void onExecuteClicked();
	void onRunFinished(bool success, const QString &resultJson, const QStringList &output, const QString &error);

private:
	QString selectedPluginId() const;
	void performActionOnSelection(PluginStatusManager::Action action);

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
};
