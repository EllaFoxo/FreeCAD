// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2026 Kacper Donat <kacper@kadet.net>                     *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#pragma once

#include <cstddef>

#include <QWidget>

#include <FCGlobal.h>

#include "GeometrySelection.h"

class QLabel;
class QToolButton;
class QVBoxLayout;

namespace Gui
{

/**
 * Composite view widget that owns a GeometrySelection core and renders it
 * as a line-edit-styled frame. Exposes the core via selection() so callers
 * can configure gate, binding, and preview hooks.
 *
 * The frame is painted via QStyle::PE_PanelLineEdit so any ambient QStyle
 * themes it correctly — no hard dependency on FreeCADStyle.
 */
class GuiExport GeometrySelectorWidget: public QWidget
{
    Q_OBJECT

public:
    explicit GeometrySelectorWidget(
        GeometryQuantity mode = GeometryQuantity::Single,
        QWidget* parent = nullptr
    );

    /// The owned core; callers use this to configure gate, binding, etc.
    GeometrySelection* selection() const
    {
        return m_selection;
    }

protected:
    void paintEvent(QPaintEvent* event) override;

private Q_SLOTS:
    void rebuildRows();
    void onSelectionModeEntered();
    void onSelectionModeExited();

private:
    /// One rendered row for a single reference (or the empty/selecting placeholder).
    struct ReferenceRow
    {
        QWidget* container = nullptr;
        QLabel* iconLabel = nullptr;
        QLabel* nameLabel = nullptr;
        QToolButton* actionButton = nullptr;
        QToolButton* removeButton = nullptr;
    };

    ReferenceRow makeEmptyRow();
    ReferenceRow makeSelectingRow();
    ReferenceRow makeReferenceRow(std::size_t index);

    void clearRows();
    void addRowWidget(QWidget* rowWidget);

    GeometrySelection* m_selection;
    QVBoxLayout* m_contentLayout;
};

}  // namespace Gui
