# SPDX-License-Identifier: LGPL-2.1-or-later

#***************************************************************************#
#                                                                           #
#    Copyright (c) 2026 Ella Fox <ella@fox.gal>                             #
#                                                                           #
#    This file is part of FreeCAD.                                          #
#                                                                           #
#    FreeCAD is free software: you can redistribute it and/or modify it     #
#    under the terms of the GNU Lesser General Public License as            #
#    published by the Free Software Foundation, either version 2.1 of the   #
#    License, or (at your option) any later version.                        #
#                                                                           #
#    FreeCAD is distributed in the hope that it will be useful, but         #
#    WITHOUT ANY WARRANTY; without even the implied warranty of             #
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       #
#    Lesser General Public License for more details.                        #
#                                                                           #
#    You should have received a copy of the GNU Lesser General Public       #
#    License along with FreeCAD. If not, see                                #
#    <https://www.gnu.org/licenses/>.                                       #
#                                                                           #
#***************************************************************************#

# ===========
# Next Gen UI
# -----------
# This is the testing playground for testing next generation FreeCAD UI.
# NextGenUI is written Qt6 Quick and Quick3D native, in QML.
# ===========

# clangd/clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

SET(NextGenUI_CPP_SRCS
        src/Application.cpp
        src/window/MainWindow.cpp
)

SET(NextGenUI_HPP_SRCS
)

SET(NextGenUI_QML_SRCS
        qml/window/MainWindow.qml
)

qt6_add_executable(NextGenUI
        ${NextGenUI_CPP_SRCS}
        ${NextGenUI_HPP_SRCS}
)

qt6_add_qml_module(NextGenUI
        URI NextGenUI
        VERSION 1.0
        OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/NextGenUI
        QML_FILES
        ${NextGenUI_QML_SRCS}
)

target_link_libraries(NextGenUI PRIVATE
        Qt6::Quick
        Qt6::Qml
        Qt6::QuickControls2
        Qt6::Quick3D
        Qt6::Gui
        Qt6::Svg
)
