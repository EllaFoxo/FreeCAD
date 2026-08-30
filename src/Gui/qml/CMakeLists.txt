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

set(FREECAD_QML_IMPORT_DIR ${CMAKE_BINARY_DIR}/qml)

function(freecad_qml_module name)
    set(target FreeCADGuiQml${name})

    qt6_add_library(${target} STATIC)

    qt6_add_qml_module(${target}
            URI FreeCADGui.${name}
            VERSION 1.0
            CLASS_NAME FreeCADGui${name}Plugin
            OUTPUT_DIRECTORY ${FREECAD_QML_IMPORT_DIR}/FreeCADGui/${name}
            QML_FILES ${ARGN}
    )

    set_target_properties(${target} ${target}plugin PROPERTIES
            POSITION_INDEPENDENT_CODE ON
    )

    target_link_libraries(${target} PRIVATE
            ${QtQuick_LIBRARIES}
            ${QtQml_LIBRARIES}
            ${QtQuickControls2_LIBRARIES}
    )

    target_link_libraries(FreeCADGui PRIVATE ${target} ${target}plugin)
endfunction()

add_subdirectory(dialog)
