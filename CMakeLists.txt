cmake_minimum_required(VERSION 3.22)

set(CMAKE_CONFIGURATION_TYPES ${CMAKE_CONFIGURATION_TYPES} "MinSizeRel;RelWithDebInfo;Debug;Release")
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# BUILD_DSS_PLUGIN gates everything that pulls in the DAZ Studio SDK (the
# DzPythonBridgePane DSS plugin). DazPythonBridgeCore (bootstrap/launcher)
# needs only QtCore + QtNetwork and always builds/tests standalone, whether
# or not this is set.
option(BUILD_DSS_PLUGIN "Build the DAZ Studio plugin pane on top of DazPythonBridgeCore (requires the DAZ Studio SDK)" OFF)

if(BUILD_DSS_PLUGIN)
	set(DAZ_SDK_VERSION "4" CACHE STRING "Target DAZ Studio SDK major version (4 or 6)")
	set_property(CACHE DAZ_SDK_VERSION PROPERTY STRINGS "4" "6")
	if(NOT DAZ_SDK_VERSION STREQUAL "4" AND NOT DAZ_SDK_VERSION STREQUAL "6")
		message(FATAL_ERROR "DAZ_SDK_VERSION must be '4' or '6', got '${DAZ_SDK_VERSION}'")
	endif()
endif()

if(APPLE AND BUILD_DSS_PLUGIN)
	if(NOT CMAKE_OSX_ARCHITECTURES)
		set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "" FORCE)
	endif()
endif()

project("DazPythonBridge")

set(DAZ_STUDIO_EXE_DIR "" CACHE PATH "Path to DAZ Studio executable folder (for plugin installation)")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ─── Qt6 ──────────────────────────────────────────────────────────────────────
#
# DazPythonBridgeCore only needs QtCore + QtNetwork — it has no DAZ Studio SDK
# dependency and can be built/tested standalone. The DSS-facing pane/plugin
# registration below (only added when BUILD_DSS_PLUGIN is ON) is what pulls in
# the DAZ SDK and Qt Widgets.

set(Qt6_DIR "" CACHE PATH "Path to Qt6Config.cmake, e.g. <qt-install>/6.10.3/msvc2022_64/lib/cmake/Qt6")
if(Qt6_DIR)
	list(PREPEND CMAKE_PREFIX_PATH "${Qt6_DIR}")
endif()

if(BUILD_DSS_PLUGIN AND DAZ_SDK_VERSION STREQUAL "6")
	# SDK6 ships no Qt .lib/.cmake files of its own — a separate Qt6 devkit
	# matching the minor version bundled with DAZ Studio 6 is required (see
	# README/CLAUDE.md). Widgets/Core5Compat/Qml are needed by the pane; fold
	# them into the single find_package(Qt6) call so Core stays a subset of it.
	find_package(Qt6 REQUIRED COMPONENTS Core Core5Compat Widgets Network Qml)
else()
	find_package(Qt6 REQUIRED COMPONENTS Core Network)
endif()

# ─── DAZ SDK (only when BUILD_DSS_PLUGIN is ON) ──────────────────────────────
#
# Mirrors daz-script-server/CMakeLists.txt's dual SDK4/SDK6 setup — see that
# project for the full platform/Qt4 rationale.

if(BUILD_DSS_PLUGIN)

if(WIN32)
	set(DZ_LIB_SUFFIX ".lib")
	set(DZ_LIB_PREFIX "")
	set(UTIL_EXT ".exe")
	if(CMAKE_SIZEOF_VOID_P EQUAL 4)
		set(DZ_MIXED_PLATFORM Win32)
	elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(DZ_MIXED_PLATFORM x64)
	else()
		message(FATAL_ERROR "Unknown architecture")
	endif()
elseif(APPLE)
	set(DZ_LIB_SUFFIX ".dylib")
	set(DZ_LIB_PREFIX "lib")
	set(UTIL_EXT "")
	if(CMAKE_SIZEOF_VOID_P EQUAL 4)
		set(DZ_MIXED_PLATFORM Mac32)
	elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(DZ_MIXED_PLATFORM Mac64)
	else()
		message(FATAL_ERROR "Unknown architecture")
	endif()
	set(CMAKE_MACOSX_RPATH TRUE)
	set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
	if(DAZ_SDK_VERSION STREQUAL "4")
		# SDK4's Qt4 headers need c++11; Qt6 (SDK6) needs CMAKE_CXX_STANDARD 17 above.
		set(CMAKE_CXX_FLAGS "-std=c++11 -stdlib=libc++ ${CMAKE_CXX_FLAGS}")
	else()
		set(CMAKE_CXX_FLAGS "-stdlib=libc++ ${CMAKE_CXX_FLAGS}")
	endif()
else()
	message(FATAL_ERROR "Unsupported platform")
endif()

if(DAZ_SDK_VERSION STREQUAL "6")
	# SDK6 moved the platform directory above lib/bin: x64/lib/dzcore.lib, x64/bin/dpc.exe
	set(DAZ_SDK_CORE_RELATIVE_PATH "${DZ_MIXED_PLATFORM}/lib/${DZ_LIB_PREFIX}dzcore${DZ_LIB_SUFFIX}")
	set(DAZ_SDK_DPC_RELATIVE_PATH  "${DZ_MIXED_PLATFORM}/bin/dpc${UTIL_EXT}")
else()
	set(DAZ_SDK_CORE_RELATIVE_PATH "lib/${DZ_MIXED_PLATFORM}/${DZ_LIB_PREFIX}dzcore${DZ_LIB_SUFFIX}")
	set(DAZ_SDK_DPC_RELATIVE_PATH  "bin/${DZ_MIXED_PLATFORM}/dpc${UTIL_EXT}")
endif()

set(DAZ_SDK_DIR "" CACHE PATH "Path to root of the DAZ Studio SDK")
if(NOT DAZ_SDK_DIR)
	message(FATAL_ERROR "BUILD_DSS_PLUGIN requires DAZ_SDK_DIR (path to the DAZ Studio SDK root)")
endif()

# We only have release libraries for dzcore/Qt — use /MD and undef _DEBUG even in Debug builds.
if(WIN32)
	add_compile_options("/MD" "/U_DEBUG")
endif()

# dzcore import target
set(DZ_SDK_INCLUDE "${DAZ_SDK_DIR}/include" CACHE FILEPATH "Path to DAZ SDK includes")
set(DAZ_SDK_LIB    "${DAZ_SDK_DIR}/${DAZ_SDK_CORE_RELATIVE_PATH}" CACHE FILEPATH "Path to dzcore")
if(NOT EXISTS ${DAZ_SDK_LIB})
	message(FATAL_ERROR "dzcore library not found — check DAZ_SDK_DIR: ${DAZ_SDK_DIR}")
endif()

add_library(dzcore SHARED IMPORTED)
if(WIN32)
	set_property(TARGET dzcore APPEND PROPERTY IMPORTED_IMPLIB ${DAZ_SDK_LIB})
else()
	set_property(TARGET dzcore APPEND PROPERTY IMPORTED_LOCATION ${DAZ_SDK_LIB})
endif()
if(APPLE AND DAZ_SDK_VERSION STREQUAL "4")
	# Prepend the patch overlay so our fixed qiterator.h shadows the SDK copy.
	# The SDK's version has bare namespace std { struct ...; } forward declarations
	# that are ambiguous with libc++'s std::__1 inline namespace on modern clang.
	# Qt4-only: SDK6 uses real Qt6, which must not have this overlay shadowing
	# its own qiterator.h (it defines QKeyValueIterator, used by qhash.h).
	set_property(TARGET dzcore APPEND PROPERTY
		INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/cmake/qt4_patches")
endif()
set_property(TARGET dzcore APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${DZ_SDK_INCLUDE}")

# dzsdkmemory import target — required by SDK6 on Windows, doesn't exist for SDK4
if(DAZ_SDK_VERSION STREQUAL "6" AND WIN32)
	set(DAZ_SDK_MEMORY_LIB "${DAZ_SDK_DIR}/${DZ_MIXED_PLATFORM}/lib/dzsdkmemory${DZ_LIB_SUFFIX}" CACHE FILEPATH "Path to dzsdkmemory")
	if(NOT EXISTS ${DAZ_SDK_MEMORY_LIB})
		message(FATAL_ERROR "dzsdkmemory library not found — check DAZ_SDK_DIR: ${DAZ_SDK_DIR}")
	endif()
	add_library(dzsdkmemory SHARED IMPORTED)
	set_property(TARGET dzsdkmemory APPEND PROPERTY IMPORTED_IMPLIB ${DAZ_SDK_MEMORY_LIB})
	set_property(TARGET dzsdkmemory APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${DZ_SDK_INCLUDE}")
endif()

# dpc import target (Daz pre-compiler)
set(DAZ_SDK_DPC_EXE "${DAZ_SDK_DIR}/${DAZ_SDK_DPC_RELATIVE_PATH}" CACHE FILEPATH "Path to SDK dpc")
if(NOT EXISTS ${DAZ_SDK_DPC_EXE})
	message(FATAL_ERROR "dpc executable not found — check DAZ_SDK_DIR: ${DAZ_SDK_DIR}")
endif()
add_executable(dpc IMPORTED)
set_property(TARGET dpc APPEND PROPERTY IMPORTED_LOCATION ${DAZ_SDK_DPC_EXE})

if(DAZ_SDK_VERSION STREQUAL "4")

	# ─── Qt 4.8 (bundled with the DAZStudio 4.5+ SDK) ────────────────────────
	if(WIN32)
		set(DAZ_SDK_QTCORE_LIBRARY "${DAZ_SDK_DIR}/lib/${DZ_MIXED_PLATFORM}/QtCore4.lib")
	elseif(APPLE)
		set(DAZ_SDK_QTCORE_LIBRARY "${DAZ_SDK_DIR}/lib/${DZ_MIXED_PLATFORM}/QtCore.framework")
		# SDK qmake is i386-only and can't run on macOS 10.15+; use our custom
		# cmake/FindQt4.cmake instead, which bypasses qmake entirely.
		list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
	endif()

	set(QT_BINARY_DIR_DEFAULT "${DAZ_SDK_DIR}/bin/${DZ_MIXED_PLATFORM}")
	set(QT_IMPORTS_DIR        "${DAZ_SDK_DIR}/lib/${DZ_MIXED_PLATFORM}")
	set(QT_QTCORE_LIBRARY_RELEASE ${DAZ_SDK_QTCORE_LIBRARY})
	set(QT_QMAKE_EXECUTABLE   "${DAZ_SDK_DIR}/bin/${DZ_MIXED_PLATFORM}/qmake${UTIL_EXT}")
	set(QT_BINARY_DIR         "${DAZ_SDK_DIR}/bin/${DZ_MIXED_PLATFORM}")
	set(QT_HEADERS_DIR        "${DAZ_SDK_DIR}/include")
	set(QT_QTCORE_INCLUDE_DIR "${DAZ_SDK_DIR}/include/QtCore")

	# The Qt find module needs this folder; fake it.
	file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/DUMMY_MKSPECS/default")
	set(QT_MKSPECS_DIR "${CMAKE_CURRENT_BINARY_DIR}/DUMMY_MKSPECS")

	find_package(Qt4 4.8.1 REQUIRED QtCore QtGui QtScript QtNetwork)

	set(DZSDK_QT_CORE_TARGET    Qt4::QtCore)
	set(DZSDK_QT_GUI_TARGET     Qt4::QtGui)
	set(DZSDK_QT_SCRIPT_TARGET  Qt4::QtScript)
	set(DZSDK_QT_NETWORK_TARGET Qt4::QtNetwork)

else() # DAZ_SDK_VERSION STREQUAL "6"

	# ─── Qt6 (matching version bundled with DAZ Studio 6 at runtime) ─────────
	#
	# SDK6 ships no Qt .lib/.cmake files at all — only the runtime DLLs inside
	# the installed DAZ Studio 6 app carry Qt6, and those have no import libs.
	# Building requires a separate Qt6 devkit of the same minor version (see
	# README/CLAUDE.md); at runtime the plugin resolves Qt6 symbols against
	# DAZ Studio 6's own bundled DLLs already loaded in-process.
	# find_package(Qt6 ...) for this case already ran above (Core/Core5Compat/
	# Widgets/Network/Qml), shared with DazPythonBridgeCore's plain Core+Network.

	set(DZSDK_QT_CORE_TARGET    Qt6::Core Qt6::Core5Compat)
	set(DZSDK_QT_GUI_TARGET     Qt6::Gui Qt6::Widgets)
	set(DZSDK_QT_NETWORK_TARGET Qt6::Network)
	# DzScript::result()/evaluate() return QJSValue (QtQml) on SDK6. Qml is only
	# needed for QJSValue's full type definition.
	set(DZSDK_QT_SCRIPT_TARGET  Qt6::Qml)

endif()

# ─── Output directories ──────────────────────────────────────────────────────

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugin)

# ─── Optional: copy plugin directly to Daz Studio plugins folder ────────────

option(INSTALL_TO_DAZ "Copy DLL to DAZ_STUDIO_EXE_DIR/plugins after build" OFF)
if(INSTALL_TO_DAZ)
	if(NOT DAZ_STUDIO_EXE_DIR)
		message(FATAL_ERROR "INSTALL_TO_DAZ requires DAZ_STUDIO_EXE_DIR to be set")
	endif()
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${DAZ_STUDIO_EXE_DIR}/plugins/DazPythonBridge")
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${DAZ_STUDIO_EXE_DIR}/plugins/DazPythonBridge")
endif()

endif() # BUILD_DSS_PLUGIN

add_subdirectory("src")
