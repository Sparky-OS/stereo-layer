# SPDX-License-Identifier: GPL-2.0-or-later
add_executable(picturetest picturetest.cpp)
target_link_libraries(picturetest PRIVATE StereoLayer Qt6::Test)
add_test(NAME picturetest COMMAND picturetest)
set_tests_properties(picturetest PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
