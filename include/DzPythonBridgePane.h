#pragma once

#include <dzpane.h>

#if DAZ_SDK_MAJOR_VERSION >= 6
// These widget classes moved from QtGui to QtWidgets in Qt5/6.
#include <QtWidgets/qwidget.h>
#else
#include <QtGui/qwidget.h>
#endif

// Dockable "Daz Python Bridge" pane shell, registered under Window > Panes by
// the companion DzPythonBridgePaneAction in pluginmain.cpp. This class only
// provides the title/layout scaffold — daz-python-bridge-sop.6 mounts the
// plugin status list and start/stop/restart/enable/disable controls into
// contentContainer().
class DzPythonBridgePane : public DzPane {
	Q_OBJECT

public:
	DzPythonBridgePane();

	// Empty widget that daz-python-bridge-sop.6's status list/controls attach to.
	QWidget* contentContainer() const { return m_pContentContainer; }

private:
	QWidget* m_pContentContainer;
};
