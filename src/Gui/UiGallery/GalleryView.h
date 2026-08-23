// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2026 Ella Fox <ella@fox.gal>                            *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

/**
 * FreeCAD UI Gallery
 *
 * This is a debug catalog, which presents UI elements in a presentable manner,
 * so that UI/UX developers can quickly iterate and improve on them.
 *
 * GalleryView is a top-level QMainWindow, and is not part of the main MDIView by design.
 *
 * This window is inert, in that it does not modify the application state at all.
 * Workbenches push their UI elements into it via the GalleryRegistry, in a similar vein
 * to how WidgetFactory is also push, not pull.
 */

#pragma once

#include <memory>

#include <QMainWindow>

#include <FCGlobal.h>

namespace Gui::UiGallery
{

class Ui_GalleryView;

class GuiExport GalleryView: public QMainWindow
{
    Q_OBJECT

public:
    explicit GalleryView(QWidget* parent = nullptr);
    ~GalleryView() override;

private:
    std::unique_ptr<Ui_GalleryView> ui;
};

}  // namespace Gui::UiGallery
