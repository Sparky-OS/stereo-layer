# SPDX-License-Identifier: GPL-2.0-or-later
add_executable(policytest policytest.c)
target_link_libraries(policytest PRIVATE StereoPolicy m)
target_compile_definitions(policytest PRIVATE VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors")
add_test(NAME policytest COMMAND policytest)
# The per-file store with every path in one entry file and several writers at once; the same without the store's lock
# must fail (it is the control that shows the first can).
add_executable(policyfiletest policyfiletest.c)
target_compile_definitions(policyfiletest PRIVATE STEREO_POLICY_FILE_HASH_MASK=0ULL)
target_include_directories(policyfiletest PRIVATE ${CMAKE_SOURCE_DIR}/src/policy)
add_test(NAME policyfiletest COMMAND policyfiletest)
add_executable(policyfiletest-nolock policyfiletest.c)
target_compile_definitions(policyfiletest-nolock PRIVATE STEREO_POLICY_FILE_HASH_MASK=0ULL STEREO_POLICY_FILE_TEST_NO_LOCK)
target_include_directories(policyfiletest-nolock PRIVATE ${CMAKE_SOURCE_DIR}/src/policy)
add_test(NAME policyfiletest-nolock COMMAND policyfiletest-nolock)
set_tests_properties(policyfiletest-nolock PROPERTIES WILL_FAIL TRUE)
add_executable(dibrtest dibrtest.c)
target_link_libraries(dibrtest PRIVATE StereoDibr m)
add_test(NAME dibrtest COMMAND dibrtest)
# the default way of drawing the views is the best way measured on scenes with known views (prints the table)
add_executable(dibrchoosetest dibrchoosetest.c)
target_link_libraries(dibrchoosetest PRIVATE StereoDibr m)
add_test(NAME dibrchoosetest COMMAND dibrchoosetest)

add_executable(dibrbench dibrbench.c)
target_link_libraries(dibrbench PRIVATE StereoDibr m)
add_test(NAME dibrbench COMMAND dibrbench)
set_tests_properties(dibrbench PROPERTIES TIMEOUT 900)
# the GLSL passes against the C code, in Mesa's software renderer; skipped without EGL
pkg_check_modules(EGL IMPORTED_TARGET egl)
if(EGL_FOUND)
    add_executable(dibrgltest dibrgltest.c)
    target_link_libraries(dibrgltest PRIVATE StereoDibr PkgConfig::EGL m)
    add_test(NAME dibrgltest COMMAND dibrgltest)
    set_tests_properties(dibrgltest PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
    # not a test: the time of every way of drawing on a GPU (GLSL) and on the CPU, at the size a player has to keep up with
    add_executable(dibrtime dibrtime.c)
    target_link_libraries(dibrtime PRIVATE StereoDibr PkgConfig::EGL m)
endif()
# a reader that gives pictures with their depth map the way the edition's image readers do
add_library(depthtestreader MODULE depthreader/depthreader.cpp)
target_link_libraries(depthtestreader PRIVATE Qt6::Gui)
set_target_properties(depthtestreader PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/plugins/imageformats AUTOMOC ON)
add_executable(picturetest picturetest.cpp)
target_link_libraries(picturetest PRIVATE StereoLayer Qt6::Test Qt6::Widgets Qt6::DBus ZLIB::ZLIB)
target_include_directories(picturetest PRIVATE ${CMAKE_SOURCE_DIR}/src/widgets)
add_dependencies(picturetest depthtestreader)
# a session bus of the test's own: the follower of renamed and moved files hears the desktop's file jobs there
find_program(DBUS_RUN_SESSION dbus-run-session)
if(DBUS_RUN_SESSION)
    add_test(NAME picturetest COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:picturetest>)
else()
    # a build that runs no tests (the nocheck profile leaves dbus-daemon out) still configures them
    message(WARNING "dbus-run-session was not found (package dbus-daemon): picturetest will skip the follower of renamed and moved files unless a session bus is running")
    add_test(NAME picturetest COMMAND picturetest)
endif()
cmake_path(CONVERT "$ENV{QT_PLUGIN_PATH}" TO_CMAKE_PATH_LIST _picture_plugins)
list(PREPEND _picture_plugins "${CMAKE_CURRENT_BINARY_DIR}/plugins")
cmake_path(CONVERT "${_picture_plugins}" TO_NATIVE_PATH_LIST _picture_plugin_path)
string(REPLACE ";" "\\;" _picture_plugin_path "${_picture_plugin_path}")
set_tests_properties(picturetest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${_picture_plugin_path}")
add_executable(videotest videotest.cpp)
target_link_libraries(videotest PRIVATE StereoLayer Qt6::Test)
target_compile_definitions(videotest PRIVATE VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors")
add_test(NAME videotest COMMAND videotest)
add_executable(screenmodetest screenmodetest.cpp)
target_link_libraries(screenmodetest PRIVATE StereoLayer Qt6::Test Qt6::DBus KF6::Screen)
add_test(NAME screenmodetest COMMAND screenmodetest)
set_tests_properties(screenmodetest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen:configfile=${CMAKE_CURRENT_SOURCE_DIR}/screens.json")

add_executable(qmlimporttest qmlimporttest.cpp)
target_link_libraries(qmlimporttest PRIVATE Qt6::Quick Qt6::Qml Qt6::Test)
add_dependencies(qmlimporttest StereoLayerplugin)
add_test(NAME qmlimporttest COMMAND qmlimporttest)
set_tests_properties(qmlimporttest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QML_IMPORT_PATH=${CMAKE_BINARY_DIR}/qml")

# The QML face of the per-file choice: the same list as the widgets' menu, the same writes to the store
add_executable(filechoiceqmltest filechoiceqmltest.cpp)
target_link_libraries(filechoiceqmltest PRIVATE StereoLayer StereoPolicy Qt6::Quick Qt6::Qml Qt6::Widgets Qt6::DBus Qt6::Test)
target_include_directories(filechoiceqmltest PRIVATE ${CMAKE_SOURCE_DIR}/src/quick ${CMAKE_SOURCE_DIR}/src/widgets)
add_dependencies(filechoiceqmltest StereoLayerplugin)
# a session bus of the test's own, as for picturetest: the QML type follows renamed and moved files there
if(DBUS_RUN_SESSION)
    add_test(NAME filechoiceqmltest COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:filechoiceqmltest>)
else()
    add_test(NAME filechoiceqmltest COMMAND filechoiceqmltest)
endif()
set_tests_properties(filechoiceqmltest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;QML_IMPORT_PATH=${CMAKE_BINARY_DIR}/qml")
# StereoDepth: the parallax of a depth against the boot splash's numbers, and a window flat where the desktop tells no depth
add_executable(stereodepthtest stereodepthtest.cpp)
target_link_libraries(stereodepthtest PRIVATE StereoLayer Qt6::Quick Qt6::Qml Qt6::Test)
target_include_directories(stereodepthtest PRIVATE ${CMAKE_SOURCE_DIR}/src/quick)
add_dependencies(stereodepthtest StereoLayerplugin)
add_test(NAME stereodepthtest COMMAND stereodepthtest)
set_tests_properties(stereodepthtest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QML_IMPORT_PATH=${CMAKE_BINARY_DIR}/qml")

pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat>=63)
add_executable(avtest avtest.c)
target_compile_definitions(avtest PRIVATE FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/mvhevc/labelled.mp4")
target_link_libraries(avtest PRIVATE StereoAv PkgConfig::STEREO_AV PkgConfig::AVFORMAT)
add_test(NAME avtest COMMAND avtest)

# The writers of the 3D layout into Matroska, WebM and MP4 need ffmpeg and mkvtoolnix to make and judge their files
add_executable(marktest marktest.c)
target_link_libraries(marktest PRIVATE StereoAv PkgConfig::STEREO_AV PkgConfig::AVFORMAT)
target_include_directories(marktest PRIVATE ${CMAKE_SOURCE_DIR}/src/policy)
add_test(NAME marktest COMMAND marktest)

add_executable(depthcontrolstest depthcontrolstest.cpp)
target_link_libraries(depthcontrolstest PRIVATE StereoLayer StereoDibr Qt6::Quick Qt6::Qml Qt6::Widgets Qt6::Test)
target_compile_definitions(depthcontrolstest PRIVATE QML_IMPORT_DIR="${CMAKE_BINARY_DIR}/qml")
add_dependencies(depthcontrolstest StereoLayerplugin)
target_include_directories(depthcontrolstest PRIVATE ${CMAKE_SOURCE_DIR}/src/quick ${CMAKE_SOURCE_DIR}/src/widgets ${CMAKE_SOURCE_DIR}/src/dibr)
add_test(NAME depthcontrolstest COMMAND depthcontrolstest)
set_tests_properties(depthcontrolstest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QML_IMPORT_PATH=${CMAKE_BINARY_DIR}/qml")

add_executable(anaglyphtransformtest anaglyphtransformtest.cpp)
target_link_libraries(anaglyphtransformtest PRIVATE StereoLayer)
add_test(NAME anaglyphtransformtest COMMAND anaglyphtransformtest)

add_executable(translationtest translationtest.cpp)
target_link_libraries(translationtest PRIVATE StereoLayer Qt6::Core)
set(_translation_root ${CMAKE_CURRENT_BINARY_DIR}/translations)
set(_french_qm ${_translation_root}/locale/fr/LC_MESSAGES/stereo-layer_qt.qm)
add_custom_command(OUTPUT ${_french_qm}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/ECMPoQm/fr/stereo-layer_qt.qm ${_french_qm}
    DEPENDS ${CMAKE_BINARY_DIR}/ECMPoQm/fr/stereo-layer_qt.qm)
add_custom_target(translationtestqm DEPENDS ${_french_qm})
add_dependencies(translationtest translationtestqm)
add_test(NAME translationtest-fr
    COMMAND translationtest "La couleur est reconstruite et approximative : un anaglyphe ne conserve qu'une partie de la couleur de chaque œil. Le fichier d'origine n'est jamais modifié." "Intensité de la profondeur" "Activer la 3D")
set_tests_properties(translationtest-fr PROPERTIES
    ENVIRONMENT "LANGUAGE=fr:en;LC_ALL=fr_FR.UTF-8;XDG_DATA_DIRS=${_translation_root}")
add_test(NAME translationtest-c
    COMMAND translationtest "Colour is reconstructed and approximate: an anaglyph keeps only some of each eye's colour. The original file is never changed." "Depth strength" "Enable 3D")
set_tests_properties(translationtest-c PROPERTIES ENVIRONMENT "LC_ALL=C")

# A film with a picture and no audio track says so, in one sentence of the layer's catalogue (ORDER 579): the rule and the ten languages
add_executable(nosoundtest nosoundtest.cpp)
target_link_libraries(nosoundtest PRIVATE StereoLayer Qt6::Core)
target_include_directories(nosoundtest PRIVATE ${CMAKE_SOURCE_DIR}/src/quick)
add_test(NAME nosoundtest-c COMMAND nosoundtest "This film has no sound.")
set_tests_properties(nosoundtest-c PROPERTIES ENVIRONMENT "LC_ALL=C")
set(_ns_fr_qm ${_translation_root}/locale/fr/LC_MESSAGES/stereo-layer_qt.qm)
add_custom_command(OUTPUT ${_ns_fr_qm}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/ECMPoQm/fr/stereo-layer_qt.qm ${_ns_fr_qm}
    DEPENDS ${CMAKE_BINARY_DIR}/ECMPoQm/fr/stereo-layer_qt.qm)
add_custom_target(nosoundqm-fr DEPENDS ${_ns_fr_qm})
add_dependencies(nosoundtest nosoundqm-fr)
add_test(NAME nosoundtest-fr COMMAND nosoundtest "Ce film n’a pas de son.")
set_tests_properties(nosoundtest-fr PROPERTIES ENVIRONMENT "LANGUAGE=fr:en;LC_ALL=fr_FR.UTF-8;XDG_DATA_DIRS=${_translation_root}")
set(_ns_de_qm ${_translation_root}/locale/de/LC_MESSAGES/stereo-layer_qt.qm)
add_custom_command(OUTPUT ${_ns_de_qm}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/ECMPoQm/de/stereo-layer_qt.qm ${_ns_de_qm}
    DEPENDS ${CMAKE_BINARY_DIR}/ECMPoQm/de/stereo-layer_qt.qm)
add_custom_target(nosoundqm-de DEPENDS ${_ns_de_qm})
add_dependencies(nosoundtest nosoundqm-de)
add_test(NAME nosoundtest-de COMMAND nosoundtest "Dieser Film hat keinen Ton.")
set_tests_properties(nosoundtest-de PROPERTIES ENVIRONMENT "LANGUAGE=de:en;LC_ALL=de_DE.UTF-8;XDG_DATA_DIRS=${_translation_root}")
set(_ns_es_qm ${_translation_root}/locale/es/LC_MESSAGES/stereo-layer_qt.qm)
add_custom_command(OUTPUT ${_ns_es_qm}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/ECMPoQm/es/stereo-layer_qt.qm ${_ns_es_qm}
    DEPENDS ${CMAKE_BINARY_DIR}/ECMPoQm/es/stereo-layer_qt.qm)
add_custom_target(nosoundqm-es DEPENDS ${_ns_es_qm})
add_dependencies(nosoundtest nosoundqm-es)
add_test(NAME nosoundtest-es COMMAND nosoundtest "Esta película no tiene sonido.")
set_tests_properties(nosoundtest-es PROPERTIES ENVIRONMENT "LANGUAGE=es:en;LC_ALL=es_ES.UTF-8;XDG_DATA_DIRS=${_translation_root}")
set(_ns_ja_qm ${_translation_root}/locale/ja/LC_MESSAGES/stereo-layer_qt.qm)
add_custom_command(OUTPUT ${_ns_ja_qm}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/ECMPoQm/ja/stereo-layer_qt.qm ${_ns_ja_qm}
    DEPENDS ${CMAKE_BINARY_DIR}/ECMPoQm/ja/stereo-layer_qt.qm)
add_custom_target(nosoundqm-ja DEPENDS ${_ns_ja_qm})
add_dependencies(nosoundtest nosoundqm-ja)
add_test(NAME nosoundtest-ja COMMAND nosoundtest "この映像には音声がありません。")
set_tests_properties(nosoundtest-ja PROPERTIES ENVIRONMENT "LANGUAGE=ja:en;LC_ALL=ja_JP.UTF-8;XDG_DATA_DIRS=${_translation_root}")
set(_ns_ko_qm ${_translation_root}/locale/ko/LC_MESSAGES/stereo-layer_qt.qm)
add_custom_command(OUTPUT ${_ns_ko_qm}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/ECMPoQm/ko/stereo-layer_qt.qm ${_ns_ko_qm}
    DEPENDS ${CMAKE_BINARY_DIR}/ECMPoQm/ko/stereo-layer_qt.qm)
add_custom_target(nosoundqm-ko DEPENDS ${_ns_ko_qm})
add_dependencies(nosoundtest nosoundqm-ko)
add_test(NAME nosoundtest-ko COMMAND nosoundtest "이 영상에는 소리가 없습니다.")
set_tests_properties(nosoundtest-ko PROPERTIES ENVIRONMENT "LANGUAGE=ko:en;LC_ALL=ko_KR.UTF-8;XDG_DATA_DIRS=${_translation_root}")
set(_ns_pl_qm ${_translation_root}/locale/pl/LC_MESSAGES/stereo-layer_qt.qm)
add_custom_command(OUTPUT ${_ns_pl_qm}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/ECMPoQm/pl/stereo-layer_qt.qm ${_ns_pl_qm}
    DEPENDS ${CMAKE_BINARY_DIR}/ECMPoQm/pl/stereo-layer_qt.qm)
add_custom_target(nosoundqm-pl DEPENDS ${_ns_pl_qm})
add_dependencies(nosoundtest nosoundqm-pl)
add_test(NAME nosoundtest-pl COMMAND nosoundtest "Ten film nie ma dźwięku.")
set_tests_properties(nosoundtest-pl PROPERTIES ENVIRONMENT "LANGUAGE=pl:en;LC_ALL=pl_PL.UTF-8;XDG_DATA_DIRS=${_translation_root}")
set(_ns_pt_BR_qm ${_translation_root}/locale/pt_BR/LC_MESSAGES/stereo-layer_qt.qm)
add_custom_command(OUTPUT ${_ns_pt_BR_qm}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/ECMPoQm/pt_BR/stereo-layer_qt.qm ${_ns_pt_BR_qm}
    DEPENDS ${CMAKE_BINARY_DIR}/ECMPoQm/pt_BR/stereo-layer_qt.qm)
add_custom_target(nosoundqm-pt_BR DEPENDS ${_ns_pt_BR_qm})
add_dependencies(nosoundtest nosoundqm-pt_BR)
add_test(NAME nosoundtest-pt_BR COMMAND nosoundtest "Este filme não tem som.")
set_tests_properties(nosoundtest-pt_BR PROPERTIES ENVIRONMENT "LANGUAGE=pt_BR:en;LC_ALL=pt_BR.UTF-8;XDG_DATA_DIRS=${_translation_root}")
set(_ns_zh_CN_qm ${_translation_root}/locale/zh_CN/LC_MESSAGES/stereo-layer_qt.qm)
add_custom_command(OUTPUT ${_ns_zh_CN_qm}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/ECMPoQm/zh_CN/stereo-layer_qt.qm ${_ns_zh_CN_qm}
    DEPENDS ${CMAKE_BINARY_DIR}/ECMPoQm/zh_CN/stereo-layer_qt.qm)
add_custom_target(nosoundqm-zh_CN DEPENDS ${_ns_zh_CN_qm})
add_dependencies(nosoundtest nosoundqm-zh_CN)
add_test(NAME nosoundtest-zh_CN COMMAND nosoundtest "此影片没有声音。")
set_tests_properties(nosoundtest-zh_CN PROPERTIES ENVIRONMENT "LANGUAGE=zh_CN:en;LC_ALL=zh_CN.UTF-8;XDG_DATA_DIRS=${_translation_root}")
set(_ns_zh_TW_qm ${_translation_root}/locale/zh_TW/LC_MESSAGES/stereo-layer_qt.qm)
add_custom_command(OUTPUT ${_ns_zh_TW_qm}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/ECMPoQm/zh_TW/stereo-layer_qt.qm ${_ns_zh_TW_qm}
    DEPENDS ${CMAKE_BINARY_DIR}/ECMPoQm/zh_TW/stereo-layer_qt.qm)
add_custom_target(nosoundqm-zh_TW DEPENDS ${_ns_zh_TW_qm})
add_dependencies(nosoundtest nosoundqm-zh_TW)
add_test(NAME nosoundtest-zh_TW COMMAND nosoundtest "此影片沒有聲音。")
set_tests_properties(nosoundtest-zh_TW PROPERTIES ENVIRONMENT "LANGUAGE=zh_TW:en;LC_ALL=zh_TW.UTF-8;XDG_DATA_DIRS=${_translation_root}")

add_executable(picturesavetest picturesavetest.cpp)
add_executable(jpegcorrecttest jpegcorrecttest.c)
target_link_libraries(jpegcorrecttest PRIVATE StereoPolicy)
target_compile_definitions(jpegcorrecttest PRIVATE CORRECTION_DATA="${CMAKE_CURRENT_SOURCE_DIR}/corrections")
add_test(NAME jpegcorrecttest COMMAND jpegcorrecttest)
target_link_libraries(picturesavetest PRIVATE StereoLayer StereoPolicy Qt6::Test)
target_compile_definitions(picturesavetest PRIVATE SAVE_TRANSLATIONS_DIR="${CMAKE_BINARY_DIR}/ECMPoQm")
add_test(NAME picturesavetest COMMAND picturesavetest)
set_tests_properties(picturesavetest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${_picture_plugin_path}")
if(BUILD_GTK)
    add_executable(gtkunpacktest gtkunpacktest.c)
    target_link_libraries(gtkunpacktest PRIVATE StereoGtk StereoPolicy)
    add_test(NAME gtkunpacktest COMMAND gtkunpacktest)
endif()
