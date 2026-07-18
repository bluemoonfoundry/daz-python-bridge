cmake_minimum_required(VERSION 3.22)

set(CMAKE_CONFIGURATION_TYPES ${CMAKE_CONFIGURATION_TYPES} "MinSizeRel;RelWithDebInfo;Debug;Release")
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project("DazPythonBridge")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ─── Qt6 ──────────────────────────────────────────────────────────────────────
#
# The bootstrap/launcher core (this CMakeLists) only needs QtCore + QtNetwork —
# it has no DAZ Studio SDK dependency and can be built/tested standalone. The
# DSS-facing pane/plugin registration (added on top of DazPythonBridgeCore by
# the pane-shell work) is what pulls in the DAZ SDK and Qt Widgets.

set(Qt6_DIR "" CACHE PATH "Path to Qt6Config.cmake, e.g. <qt-install>/6.10.3/msvc2022_64/lib/cmake/Qt6")
if(Qt6_DIR)
	list(PREPEND CMAKE_PREFIX_PATH "${Qt6_DIR}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_subdirectory("src")
