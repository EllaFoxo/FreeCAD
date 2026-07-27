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

#include <QMargins>
#include <QWidget>

#include <FCGlobal.h>

#include "GeometrySelection.h"

class QVBoxLayout;

namespace Gui
{

/**
 * Composite view widget that owns a GeometrySelection core and renders it
 * as a list-styled frame. Exposes the core via selection() so callers can
 * configure gate, binding, and preview hooks.
 *
 * The frame is painted via the ambient QStyle, and all spacing comes from the
 * design-system tokens resolved through the List token chain, so any ambient
 * QStyle themes it correctly — no hard dependency on FreeCADStyle.
 *
 * A filled reference presents its icon and name at rest; each row reveals its
 * own remove control only while the pointer is over that row, matching the
 * Figma design.
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

    /// The three rendered states, derived from references + session.
    enum class VisualState
    {
        Empty,          // idle, no references
        Selecting,      // in a selection session: horizontal prompt chrome over a dimming backdrop
        ReferenceList,  // idle with ≥1 references (capped scroll list)
    };

    /// Classifies the current state from the core alone; independent of any QStyle or
    /// Gui::Application, so it is well-defined even in a headless harness.
    VisualState visualState() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;
    // In the empty state a click anywhere on the frame starts selecting, so the prompt is
    // a plain placeholder label rather than a button.
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private Q_SLOTS:
    void rebuildRows();

private:
    QWidget* makeEmptyRow();
    /// A capped scroll list with one row per reference, each row revealing its own
    /// remove control on hover.
    QWidget* makeReferenceList();
    /// The selection-session chrome: a horizontal placeholder + Cancel (and Done for
    /// multi-select) row over a dimming backdrop, above the committed references when any.
    QWidget* makeSelecting();

    void clearRows();
    /// Resolves layout margins, spacing and fixed height from style tokens.
    void applyStyleMetrics();
    /// One row's height: its icon/label content plus the resolved item vertical padding.
    int rowHeight() const;
    /// The reference list's rendered height: rows up to the visible-row cap.
    int referenceListHeight() const;

    GeometrySelection* m_selection;
    QVBoxLayout* m_contentLayout;
    /// Per-row inset, resolved from the ListItemPadding token; the frame itself is flush.
    QMargins m_itemPadding {6, 4, 6, 4};
    /// Icon-to-label spacing within a row, resolved from the ListItemIconSpacing token.
    int m_itemSpacing = 6;
};

}  // namespace Gui
