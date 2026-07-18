#include <dzplugin.h>
#include <dzapp.h>
#include <dzaction.h>

#include "common_version.h"
#include "DzPythonBridgePane.h"

#include "dazpythonbridge.h"

// DzPaneAction companion — this is what registers the pane in Window → Panes.
// No implementation needed; the base class handles everything.
class DzPythonBridgePaneAction : public DzPaneAction {
	Q_OBJECT
public:
	DzPythonBridgePaneAction() : DzPaneAction("DzPythonBridgePane") {}
};

#include "pluginmain.moc"

CPP_PLUGIN_DEFINITION("Daz Python Bridge")

DZ_PLUGIN_AUTHOR("DazPythonBridge Contributors");

DZ_PLUGIN_VERSION(DPB_MAJOR, DPB_MINOR, DPB_REV, DPB_BUILD);

DZ_PLUGIN_DESCRIPTION(QString(
	"Dockable pane for managing the Daz Python Bridge daemon and installed Python plugins."
));

DZ_PLUGIN_CLASS_GUID(DzPythonBridgePane,       af0273c9-c2a9-4364-83c2-78f3dba2d61e);
DZ_PLUGIN_CLASS_GUID(DzPythonBridgePaneAction, 39135127-3723-4897-a0db-2b0f73efc0f6);
