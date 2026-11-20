pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/viewporter-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} client-header ${WAYLAND_PROTOCOLS_DIR}/stable/viewporter/viewporter.xml
        ${CMAKE_CURRENT_BINARY_DIR}/viewporter-client-protocol.h VERBATIM)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/viewporter-protocol.c
    COMMAND ${WAYLAND_SCANNER} private-code ${WAYLAND_PROTOCOLS_DIR}/stable/viewporter/viewporter.xml
        ${CMAKE_CURRENT_BINARY_DIR}/viewporter-protocol.c VERBATIM)
add_library(StereoGtk SHARED stereo-gtk-file-menu.c stereo-gtk-area.c stereo-gtk-native.c stereo-gtk-unpack.c
    ${CMAKE_CURRENT_BINARY_DIR}/viewporter-client-protocol.h ${CMAKE_CURRENT_BINARY_DIR}/viewporter-protocol.c)
set_target_properties(StereoGtk PROPERTIES OUTPUT_NAME stereo-gtk VERSION ${PROJECT_VERSION} SOVERSION 1
    C_STANDARD 11 C_VISIBILITY_PRESET hidden)
target_link_libraries(StereoGtk PUBLIC PkgConfig::GTK4 PRIVATE StereoPolicy
    PkgConfig::STEREO_DECLARE PkgConfig::WAYLAND_CLIENT PkgConfig::X11)
target_include_directories(StereoGtk PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(StereoGtk PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}/stereo-gtk>)
install(TARGETS StereoGtk LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR})
install(FILES stereo-gtk.h DESTINATION ${KDE_INSTALL_INCLUDEDIR}/stereo-gtk)
configure_file(stereo-gtk.pc.in ${CMAKE_CURRENT_BINARY_DIR}/stereo-gtk.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stereo-gtk.pc DESTINATION ${KDE_INSTALL_LIBDIR}/pkgconfig)
find_program(MSGFMT msgfmt REQUIRED)
file(GLOB CATALOGUES "${PROJECT_SOURCE_DIR}/po/*/stereo-layer_qt.po")
foreach(CATALOGUE IN LISTS CATALOGUES)
    get_filename_component(LANG_DIR "${CATALOGUE}" DIRECTORY)
    get_filename_component(LANGUAGE "${LANG_DIR}" NAME)
    set(MO "${CMAKE_CURRENT_BINARY_DIR}/${LANGUAGE}/stereo-gtk.mo")
    add_custom_command(OUTPUT "${MO}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/${LANGUAGE}"
        COMMAND ${MSGFMT} --check -o "${MO}" "${CATALOGUE}" DEPENDS "${CATALOGUE}" VERBATIM)
    list(APPEND MO_FILES "${MO}")
    install(FILES "${MO}" DESTINATION "${KDE_INSTALL_LOCALEDIR}/${LANGUAGE}/LC_MESSAGES")
endforeach()
add_custom_target(stereo-gtk-translations ALL DEPENDS ${MO_FILES})
