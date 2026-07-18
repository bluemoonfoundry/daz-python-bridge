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
	ZipInstaller.cpp
	PluginDependencyInstaller.cpp
	PluginInstaller.cpp
	PluginStatusManager.cpp
	miniz.c
	../include/DaemonPaths.h
	../include/UvBootstrapper.h
	../include/DaemonProcess.h
	../include/DaemonHealthMonitor.h
	../include/ZipInstaller.h
	../include/PluginDependencyInstaller.h
	../include/PluginInstaller.h
	../include/PluginStatusManager.h
	../include/common_version.h
	miniz.h
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

# Manual/CI test harness for ZipInstaller — see tests/cpp/test_zip_installer.cpp.
# Builds standalone (no DAZ SDK) alongside DazPythonBridgeCore.
add_executable(test_zip_installer ../tests/cpp/test_zip_installer.cpp)
target_link_libraries(test_zip_installer PRIVATE DazPythonBridgeCore)

# Manual/CI test harness for PluginDependencyInstaller — see
# tests/cpp/test_plugin_dependency_installer.cpp. Requires `uv` on PATH and
# network access to resolve packages; not wired into CTest.
add_executable(test_plugin_dependency_installer ../tests/cpp/test_plugin_dependency_installer.cpp)
target_link_libraries(test_plugin_dependency_installer PRIVATE DazPythonBridgeCore)

# ─── DzPythonBridge: DSS plugin (pane) ────────────────────────────────────────
#
# Only built when BUILD_DSS_PLUGIN is ON (see top-level CMakeLists.txt), which
# is what sets up the dzcore/Qt4/Qt6-SDK import targets this depends on.

if(BUILD_DSS_PLUGIN)

add_library(DzPythonBridge SHARED
	pluginmain.cpp
	DzPythonBridgePane.cpp
	../include/DzPythonBridgePane.h
	../include/dazpythonbridge.h
	../include/common_version.h
)

target_include_directories(DzPythonBridge
	PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(DzPythonBridge
	PRIVATE
	dzcore
	${DZSDK_QT_CORE_TARGET}
	${DZSDK_QT_GUI_TARGET}
	${DZSDK_QT_NETWORK_TARGET}
	${DZSDK_QT_SCRIPT_TARGET}
)

target_compile_definitions(DzPythonBridge PRIVATE
	DAZ_SDK_MAJOR_VERSION=${DAZ_SDK_VERSION}
)

if(WIN32)
	target_link_libraries(DzPythonBridge PRIVATE ws2_32 advapi32)
	# Emit a PDB for every build config so VS Code / WinDbg can resolve symbols.
	target_compile_options(DzPythonBridge PRIVATE /Zi)
	target_link_options(DzPythonBridge PRIVATE /DEBUG /OPT:REF /OPT:ICF)
	if(DAZ_SDK_VERSION STREQUAL "6")
		target_link_libraries(DzPythonBridge PRIVATE dzsdkmemory)
	endif()
endif()

# SDK6's plugin scanner only recognizes DLLs named with the "dsp_" prefix
# (DZ_PLUGIN_PREFIX in the SDK6 CMakeLists.txt / every SDK6 sample's output
# name) — without it, DAZ Studio 6 silently skips the DLL at startup. SDK4
# has no such requirement, so keep its filename unprefixed/unchanged.
if(DAZ_SDK_VERSION STREQUAL "6")
	set(DAZPYTHONBRIDGE_OUTPUT_NAME "dsp_DazPythonBridge")
else()
	set(DAZPYTHONBRIDGE_OUTPUT_NAME "DazPythonBridge")
endif()

set_target_properties(DzPythonBridge PROPERTIES
	OUTPUT_NAME "${DAZPYTHONBRIDGE_OUTPUT_NAME}"
	PREFIX ""
	FOLDER ""
	PROJECT_LABEL "DzPythonBridge"
	# pluginmain.cpp includes its own moc output manually (#include
	# "pluginmain.moc") to define a Q_OBJECT class inline; -i suppresses moc's
	# default #include of the class's own header, which pluginmain.cpp has none of.
	AUTOMOC_MOC_OPTIONS "-i"
)

if(INSTALL_TO_DAZ AND DAZ_STUDIO_EXE_DIR)
	install(TARGETS DzPythonBridge
		RUNTIME DESTINATION "${DAZ_STUDIO_EXE_DIR}/plugins"
		LIBRARY DESTINATION "${DAZ_STUDIO_EXE_DIR}/plugins"
	)
	if(WIN32)
		install(FILES
			$<TARGET_PDB_FILE:DzPythonBridge>
			DESTINATION "${DAZ_STUDIO_EXE_DIR}/plugins"
			OPTIONAL
		)
	endif()
endif()

endif() # BUILD_DSS_PLUGIN
