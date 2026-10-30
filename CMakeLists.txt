# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.25)
project(StereoLayer VERSION 1.12.1 LANGUAGES C CXX)
find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs6)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMGenerateExportHeader)
include(ECMPoQmTools)
include(CMakePackageConfigHelpers)
include(CTest)
ecm_install_po_files_as_qm(po)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Quick Qml WaylandClient)
find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
qt_policy(SET QTP0001 NEW)
find_package(KF6Screen REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(STEREO_DECLARE REQUIRED IMPORTED_TARGET stereo-declare)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/content/content-mime-map.inc
    ${CMAKE_CURRENT_BINARY_DIR}/content/stereo-content.xml ${CMAKE_CURRENT_BINARY_DIR}/content/mime-lists.json
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/content-mime.py
        ${CMAKE_CURRENT_SOURCE_DIR}/data/content/catalogue.json ${CMAKE_CURRENT_BINARY_DIR}/content
    DEPENDS tools/content-mime.py data/content/catalogue.json VERBATIM)
add_custom_target(stereo-content-catalogue DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/content/content-mime-map.inc)

# FFmpeg consumers share native multiview pairing without pulling in Qt.
pkg_check_modules(STEREO_AV REQUIRED IMPORTED_TARGET libavcodec>=63 libavutil>=61 libavformat>=63)
add_library(StereoAv SHARED src/av/stereo-av.c src/av/stereo-content.c)
add_dependencies(StereoAv stereo-content-catalogue)
target_include_directories(StereoAv PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/content)
set_target_properties(StereoAv PROPERTIES OUTPUT_NAME stereo-av VERSION ${PROJECT_VERSION} SOVERSION 1 EXPORT_NAME Av)
target_link_libraries(StereoAv PRIVATE PkgConfig::STEREO_AV ZLIB::ZLIB)
target_include_directories(StereoAv PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/av> $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}/stereo-av>)
install(TARGETS StereoAv EXPORT StereoLayerTargets LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR})
install(FILES src/av/stereo-av.h src/av/stereo-content.h DESTINATION ${KDE_INSTALL_INCLUDEDIR}/stereo-av)
configure_file(src/av/stereo-av.pc.in ${CMAKE_CURRENT_BINARY_DIR}/stereo-av.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stereo-av.pc DESTINATION ${KDE_INSTALL_LIBDIR}/pkgconfig)

# The FFmpeg library is independently deployable; its ABI has no Qt dependency.
option(STEREO_AV_ONLY "Build and test only the FFmpeg consumer library" OFF)
if(STEREO_AV_ONLY)
    if(BUILD_TESTING)
        add_executable(avtest autotests/avtest.c)
        target_compile_definitions(avtest PRIVATE FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/autotests/mvhevc/labelled.mp4")
        target_link_libraries(avtest PRIVATE StereoAv PkgConfig::STEREO_AV)
        add_test(NAME avtest COMMAND avtest)
    endif()
    return()
endif()

# The policy without Qt, for any program; the Qt layer builds on it
add_library(StereoPolicy SHARED src/policy/stereo-policy.c src/policy/stereo-policy-picture.c src/policy/stereo-policy.h)
add_library(Stereo::Policy ALIAS StereoPolicy)
set_target_properties(StereoPolicy PROPERTIES OUTPUT_NAME stereo-policy VERSION ${PROJECT_VERSION} SOVERSION 1 EXPORT_NAME Policy
    C_STANDARD 11 C_VISIBILITY_PRESET hidden)
target_link_libraries(StereoPolicy PRIVATE m)
target_include_directories(StereoPolicy PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/policy> $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}/stereo-policy>)
install(TARGETS StereoPolicy EXPORT StereoLayerTargets ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES src/policy/stereo-policy.h DESTINATION ${KDE_INSTALL_INCLUDEDIR}/stereo-policy)
configure_file(src/policy/stereo-policy.pc.in ${CMAKE_CURRENT_BINARY_DIR}/stereo-policy.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stereo-policy.pc DESTINATION ${KDE_INSTALL_LIBDIR}/pkgconfig)

# Depth-image-based rendering without Qt, for any program that turns a picture and its depth into stereo
# The GLSL passes of the DIBR core, built into the library as byte arrays and installed for other programs
file(GLOB STEREO_DIBR_GLSL CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/dibr/glsl/*.frag ${CMAKE_CURRENT_SOURCE_DIR}/src/dibr/glsl/*.vert)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/stereo-dibr-glsl.c
    COMMAND ${CMAKE_COMMAND} -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/stereo-dibr-glsl.c "-DFILES=${STEREO_DIBR_GLSL}" -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedGlsl.cmake
    DEPENDS ${STEREO_DIBR_GLSL} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedGlsl.cmake VERBATIM)
add_library(StereoDibr SHARED src/dibr/stereo-dibr.c src/dibr/stereo-depth.c src/dibr/stereo-dibr.h src/dibr/stereo-dibr-gl.c src/dibr/stereo-dibr-gl.h
    ${CMAKE_CURRENT_BINARY_DIR}/stereo-dibr-glsl.c)
add_library(Stereo::Dibr ALIAS StereoDibr)
set_target_properties(StereoDibr PROPERTIES OUTPUT_NAME stereo-dibr VERSION ${PROJECT_VERSION} SOVERSION 1 EXPORT_NAME Dibr
    C_STANDARD 11 C_VISIBILITY_PRESET hidden)
target_link_libraries(StereoDibr PRIVATE m)
target_include_directories(StereoDibr PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/dibr> $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}/stereo-dibr>)
install(TARGETS StereoDibr EXPORT StereoLayerTargets ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES src/dibr/stereo-dibr.h src/dibr/stereo-dibr-gl.h DESTINATION ${KDE_INSTALL_INCLUDEDIR}/stereo-dibr)
install(FILES ${STEREO_DIBR_GLSL} DESTINATION ${KDE_INSTALL_DATADIR}/stereo-dibr/glsl)
configure_file(src/dibr/stereo-dibr.pc.in ${CMAKE_CURRENT_BINARY_DIR}/stereo-dibr.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stereo-dibr.pc DESTINATION ${KDE_INSTALL_LIBDIR}/pkgconfig)

add_library(StereoLayer SHARED src/picture/picture.cpp src/picture/mark.cpp src/picture/anaglyph.cpp src/picture/panorama.cpp src/picture/render.cpp
    src/video/video.cpp src/video/video.h
    src/output/screenmode.cpp src/output/screenmode.h
    src/wayland/surface.cpp src/wayland/surface_p.h
    src/widgets/widgetlayer.cpp src/widgets/widgetlayer.h)
add_library(Stereo::Layer ALIAS StereoLayer)
ecm_create_qm_loader(StereoLayer stereo-layer_qt)
ecm_generate_export_header(StereoLayer BASE_NAME STEREO_LAYER VERSION ${PROJECT_VERSION} EXPORT_FILE_NAME stereo_layer_export.h)
set_target_properties(StereoLayer PROPERTIES OUTPUT_NAME stereo-layer VERSION ${PROJECT_VERSION} SOVERSION 1 EXPORT_NAME Layer)
target_include_directories(StereoLayer PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/picture> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/video> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/widgets> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/output> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/quick> $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}> $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}/stereo-layer>)
target_link_libraries(StereoLayer PUBLIC Qt6::Gui Qt6::Quick PRIVATE StereoPolicy StereoDibr Qt6::Widgets Qt6::GuiPrivate KF6::Screen ZLIB::ZLIB PkgConfig::STEREO_DECLARE PkgConfig::WAYLAND)
target_include_directories(StereoLayer PRIVATE src/wayland)
qt6_generate_wayland_protocol_client_sources(StereoLayer FILES ${WAYLAND_PROTOCOLS_DIR}/stable/viewporter/viewporter.xml)
qt_add_qml_module(StereoLayer URI org.sparky.stereo VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/org/sparky/stereo
    SOURCES src/quick/pictureitem.h src/quick/pictureitem.cpp)
set_target_properties(StereoLayerplugin PROPERTIES PREFIX "${CMAKE_SHARED_LIBRARY_PREFIX}")
install(TARGETS StereoLayerplugin DESTINATION ${KDE_INSTALL_QMLDIR}/org/sparky/stereo)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/qml/org/sparky/stereo/qmldir ${CMAKE_CURRENT_BINARY_DIR}/qml/org/sparky/stereo/StereoLayer.qmltypes DESTINATION ${KDE_INSTALL_QMLDIR}/org/sparky/stereo)
install(TARGETS StereoLayer EXPORT StereoLayerTargets ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES src/picture/picture.h src/picture/render.h src/video/video.h src/widgets/widgetlayer.h src/output/screenmode.h src/quick/pictureitem.h ${CMAKE_CURRENT_BINARY_DIR}/stereo_layer_export.h DESTINATION ${KDE_INSTALL_INCLUDEDIR}/stereo-layer)
install(EXPORT StereoLayerTargets NAMESPACE Stereo:: DESTINATION ${KDE_INSTALL_CMAKEPACKAGEDIR}/StereoLayer)
configure_package_config_file(cmake/StereoLayerConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/StereoLayerConfig.cmake INSTALL_DESTINATION ${KDE_INSTALL_CMAKEPACKAGEDIR}/StereoLayer)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/StereoLayerConfigVersion.cmake VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/StereoLayerConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/StereoLayerConfigVersion.cmake DESTINATION ${KDE_INSTALL_CMAKEPACKAGEDIR}/StereoLayer)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test DBus)
    add_subdirectory(autotests)
endif()
option(BUILD_EXAMPLES "Build the widget and QML example client" OFF)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

option(BUILD_CONTENT_ENGINE "Build the desktop content refinement service and probe" OFF)
if(BUILD_CONTENT_ENGINE)
add_executable(stereo-content-probe src/content/probe.c)
target_link_libraries(stereo-content-probe PRIVATE StereoAv)
install(TARGETS stereo-content-probe RUNTIME DESTINATION ${KDE_INSTALL_BINDIR})

# Application selection clients stay independent of FFmpeg and Qt.
pkg_check_modules(CONTENT_GIO REQUIRED IMPORTED_TARGET gio-unix-2.0)
add_library(StereoContentClient SHARED src/content/client.c)
set_target_properties(StereoContentClient PROPERTIES OUTPUT_NAME stereo-content-client
    VERSION ${PROJECT_VERSION} SOVERSION 1 C_STANDARD 11 C_VISIBILITY_PRESET hidden)
target_link_libraries(StereoContentClient PRIVATE PkgConfig::CONTENT_GIO)
target_include_directories(StereoContentClient PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/content>
    $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}/stereo-content>)
install(TARGETS StereoContentClient LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR})
install(FILES src/content/client.h DESTINATION ${KDE_INSTALL_INCLUDEDIR}/stereo-content)
add_executable(stereo-content-worker src/content/worker.c)
target_link_libraries(stereo-content-worker PRIVATE StereoAv)
add_executable(stereo-contentd src/content/service.c)
target_include_directories(stereo-contentd PRIVATE src/av)
target_link_libraries(stereo-contentd PRIVATE PkgConfig::CONTENT_GIO)
target_compile_definitions(stereo-contentd PRIVATE STEREO_CONTENT_WORKER="${KDE_INSTALL_FULL_LIBEXECDIR}/stereo-content-worker")
install(TARGETS stereo-content-worker stereo-contentd RUNTIME DESTINATION ${KDE_INSTALL_LIBEXECDIR})
configure_file(src/content/org.stereo.Content1.service.in org.stereo.Content1.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.stereo.Content1.service DESTINATION ${KDE_INSTALL_DATADIR}/dbus-1/services)
endif()
