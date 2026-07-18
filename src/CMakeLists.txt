set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# DazPythonBridgeCore: the bootstrap/launch/health-check backend. Deliberately
# has no DAZ Studio SDK dependency (Qt Core + Network only) so it can be built
# and unit-tested standalone. The DSS plugin pane links this in and adds the
# SDK-dependent pane/registration code on top (see the pane-shell work).
add_library(DazPythonBridgeCore STATIC
	DaemonPaths.cpp
	UvBootstrapper.cpp
	DaemonProcess.cpp
	DaemonHealthMonitor.cpp
	../include/DaemonPaths.h
	../include/UvBootstrapper.h
	../include/DaemonProcess.h
	../include/DaemonHealthMonitor.h
	../include/common_version.h
)

target_include_directories(DazPythonBridgeCore
	PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(DazPythonBridgeCore
	PUBLIC
	Qt6::Core
	Qt6::Network
)
