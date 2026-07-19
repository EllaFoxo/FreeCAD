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

#include <vector>

#include <QWidget>

#include <FCGlobal.h>

#include "GeometrySelection.h"

class QEnterEvent;
class QToolButton;
class QVBoxLayout;

namespace Gui
{

/**
 * Composite view widget that owns a GeometrySelection core and renders it
 * as a line-edit-styled frame. Exposes the core via selection() so callers
 * can configure gate, binding, and preview hooks.
 *
 * The frame is painted via QStyle::PE_PanelLineEdit and all spacing comes from
 * the design-system tokens (LineEditPadding, FormControlIconSpacing, …) so any
 * ambient QStyle themes it correctly — no hard dependency on FreeCADStyle.
 *
 * A filled reference presents only its icon and name at rest; the change and
 * remove controls are revealed while the pointer hovers the widget, matching
 * the Figma design.
 */
class GuiExport GeometrySelectorWidget: public QWidget
{
    Q_OBJECT
    Q_PROPERTY(Gui::GeometryQuantity quantity READ quantity WRITE setQuantity)

public:
    explicit GeometrySelectorWidget(GeometryQuantity mode, QWidget* parent = nullptr);

    explicit GeometrySelectorWidget(QWidget* parent = nullptr);

    /// The owned core; callers use this to configure gate, binding, etc.
    GeometrySelection* selection() const
    {
        return m_selection;
    }

    /// The selection quantity mode (Single / AllowMultiple); delegates to the core.
    GeometryQuantity quantity() const;
    void setQuantity(GeometryQuantity mode);

    /// The four rendered states, derived from mode + references + session.
    enum class VisualState
    {
        Empty,             // idle, no references
        SelectingInline,   // selecting with ≤1 committed reference (single-row chrome)
        ReferenceList,     // idle with ≥1 references (capped scroll list)
        SelectingOverlay,  // selecting with ≥2 committed references (overlay chrome)
    };

    /// Classifies the current state from the core alone; independent of any QStyle or
    /// Gui::Application, so it is well-defined even in a headless harness.
    VisualState visualState() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void changeEvent(QEvent* event) override;

private Q_SLOTS:
    void rebuildRows();

private:
    QWidget* makeEmptyRow();
    QWidget* makeSelectingRow();
    /// A capped scroll list with one row per reference, each row revealing its own
    /// remove control on hover.
    QWidget* makeReferenceList();

    void clearRows();
    /// Resolves layout margins, spacing and fixed height from style tokens.
    void applyStyleMetrics();
    /// Swaps filled references between their rest (icon + name) and hover
    /// (change + remove) presentations.
    void setHovered(bool hovered);
    /// The resolved List row height, or a font-based fallback when headless.
    int rowHeight() const;

    GeometrySelection* m_selection;
    QVBoxLayout* m_contentLayout;
    /// Item spacing within a row, resolved from the LineEdit icon-spacing token.
    int m_itemSpacing = 6;
    /// The empty-state "Select geometry" prompt: placeholder text until hovered.
    QToolButton* m_placeholderButton = nullptr;
};

}  // namespace Gui
