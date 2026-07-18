set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# DazPythonBridgeCore: the bootstrap/launch/health-check backend. Deliberately
# has no DAZ Studio SDK dependency (Qt Core + Network only) so it can be built
# and unit-tested standalone. The DSS plugin pane links this in (SDK6) or
# recompiles the same sources directly (SDK4, see below) and adds the
# SDK-dependent pane/registration code on top.
#
# None of these sources touch QJsonDocument/QJsonObject/QJsonArray directly
# (JsonStd.h's QVariant-based helpers do that instead, picking QJsonDocument
# or a QtScript-based fallback per DAZ_SDK_MAJOR_VERSION) specifically so this
# exact source list can compile unmodified against either Qt6 or Qt 4.8 --
# Qt 4.8 (SDK4) has no JSON module of its own (daz-python-bridge-7wq).
set(DAZPYTHONBRIDGE_CORE_SOURCES
	DaemonPaths.cpp
	UvBootstrapper.cpp
	DaemonProcess.cpp
	DaemonHealthMonitor.cpp
	ZipInstaller.cpp
	PluginDependencyInstaller.cpp
	PluginInstaller.cpp
	PluginStatusManager.cpp
	InlineRunner.cpp
	SecureRandom.cpp
	AuthenticationService.cpp
	miniz.c
)
set(DAZPYTHONBRIDGE_CORE_HEADERS
	../include/DaemonPaths.h
	../include/UvBootstrapper.h
	../include/DaemonProcess.h
	../include/DaemonHealthMonitor.h
	../include/ZipInstaller.h
	../include/PluginDependencyInstaller.h
	../include/PluginInstaller.h
	../include/PluginStatusManager.h
	../include/InlineRunner.h
	../include/SecureRandom.h
	../include/AuthenticationService.h
	../include/JsonStd.h
	../include/PortableFs.h
	../include/common_version.h
	miniz.h
)

add_library(DazPythonBridgeCore STATIC
	${DAZPYTHONBRIDGE_CORE_SOURCES}
	${DAZPYTHONBRIDGE_CORE_HEADERS}
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

# This library is always built against plain Qt6 (see the top-level
# find_package(Qt6) call, unconditional regardless of BUILD_DSS_PLUGIN/
# DAZ_SDK_VERSION) -- tell JsonStd.h that explicitly so its
# #if DAZ_SDK_MAJOR_VERSION >= 6 branch picks QJsonDocument even in this
# library's own standalone build, where DAZ_SDK_MAJOR_VERSION would
# otherwise be undefined (BUILD_DSS_PLUGIN off, the common case for testing).
target_compile_definitions(DazPythonBridgeCore PRIVATE DAZ_SDK_MAJOR_VERSION=6)

# Manual/CI test harness for ZipInstaller — see tests/cpp/test_zip_installer.cpp.
# Builds standalone (no DAZ SDK) alongside DazPythonBridgeCore.
add_executable(test_zip_installer ../tests/cpp/test_zip_installer.cpp)
target_link_libraries(test_zip_installer PRIVATE DazPythonBridgeCore)

# Manual/CI test harness for PluginDependencyInstaller — see
# tests/cpp/test_plugin_dependency_installer.cpp. Requires `uv` on PATH and
# network access to resolve packages; not wired into CTest.
add_executable(test_plugin_dependency_installer ../tests/cpp/test_plugin_dependency_installer.cpp)
target_link_libraries(test_plugin_dependency_installer PRIVATE DazPythonBridgeCore)

# Manual/CI test harness for PluginInstaller's reinstall-over-an-existing-id
# flow — see tests/cpp/test_plugin_installer.cpp. Same uv/network requirement
# as test_plugin_dependency_installer above.
add_executable(test_plugin_installer ../tests/cpp/test_plugin_installer.cpp)
target_link_libraries(test_plugin_installer PRIVATE DazPythonBridgeCore)

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

if(DAZ_SDK_VERSION STREQUAL "6")
	# DazPythonBridgeCore (PluginStatusManager, InlineRunner, AuthenticationService,
	# SecureRandom -- all the pane's non-SDK helper classes) is built against plain
	# Qt6, same as SDK6's own Qt6 devkit here, so linking the already-built static
	# lib in is ABI-safe.
	target_link_libraries(DzPythonBridge PRIVATE DazPythonBridgeCore)
else()
	# SDK4 links Qt 4.8 instead, which is not ABI-compatible with
	# DazPythonBridgeCore's Qt6 build, so the same sources are compiled again
	# here directly against Qt4 rather than linking that static lib
	# (daz-python-bridge-7wq). DAZ_SDK_MAJOR_VERSION=4 (set on this whole
	# target below) routes JsonStd.h's JSON handling through the QtScript
	# fallback instead of the Qt6-only QJsonDocument.
	target_sources(DzPythonBridge PRIVATE
		${DAZPYTHONBRIDGE_CORE_SOURCES}
		${DAZPYTHONBRIDGE_CORE_HEADERS}
	)
endif()

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
