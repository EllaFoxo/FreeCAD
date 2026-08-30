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

#include <memory>

#include <QQmlComponent>
#include <QQuickWindow>
#include <QWindow>

#include <Base/Console.h>
#include <Gui/Application.h>

#include "DlgAboutQml.h"

using namespace Gui::Dialog;

void QmlAboutDialogFactory::show(QWidget* parent) const
{
    // Grab the engine from the Application's singleton instance
    QQmlComponent component(Gui::Application::Instance->qmlEngine());
    component.loadFromModule("FreeCADGui.Dialog", "About");

    if (component.isError()) {
        Base::Console().error(
            "About dialog failed to load: %s\n",
            component.errorString().trimmed().toUtf8().constData()
        );
        return;
    }

    std::unique_ptr<QObject> object(component.create());
    auto* window = qobject_cast<QQuickWindow*>(object.get());
    if (!window) {
        Base::Console().error("About dialog root object is not a Window\n");
        return;
    }
    object.release();

    // Center the dialog's opening over the main window, and bind parent to the main window.
    if (parent) {
        if (auto* handle = parent->window()->windowHandle()) {
            window->setTransientParent(handle);
        }
    }

    QObject::connect(window, &QQuickWindow::closing, window, &QObject::deleteLater);
    window->show();
}
