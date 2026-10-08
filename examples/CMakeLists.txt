# SPDX-License-Identifier: GPL-2.0-or-later
add_executable(stereo-layer-demo main.cpp)
target_link_libraries(stereo-layer-demo PRIVATE StereoLayer Qt6::Widgets Qt6::Qml)
