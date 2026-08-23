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

## FreeCAD UI Gallery
#
# This is a debug catalog, which presents UI elements in presentable manner,
# so that UI/UX developers can quickly iterate and improve on them.
#
# Workbenches register their own UI elements in GalleryRegistry.
# Under the hood:
# - WidgetFactory: pulls dynamically registered widgets from here
# - UiLoader: provides runtime .ui loading

SET(UiGallery_CPP_SRCS
        GalleryRegistry.cpp
        GalleryView.cpp
        GalleryEntriesGui.cpp
)

SET(UiGallery_HPP_SRCS
        GalleryEntry.h
        GalleryRegistry.h
        GalleryView.h
)

SET(UiGallery_UI_SRCS
        GalleryView.ui
)

SET(UiGallery_SRCS
        ${UiGallery_CPP_SRCS}
        ${UiGallery_HPP_SRCS}
        ${UiGallery_UI_SRCS}
)

target_sources(FreeCADGui PRIVATE ${UiGallery_SRCS})
