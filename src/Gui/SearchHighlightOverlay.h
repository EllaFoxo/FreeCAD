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

#include <QMargins>
#include <QPointer>
#include <QRect>
#include <QWidget>

#include <FCGlobal.h>

class QAbstractScrollArea;

namespace Gui
{

/**
 * Marks one widget inside a scroll area with a halo, without altering that widget.
 *
 * The halo is painted from the SearchHighlight* style tokens, so it follows the theme, and the
 * marked widget is only ever read. Nothing about its own styling changes, so it stays painted
 * by FreeCADStyle for as long as it is marked.
 */
class GuiExport SearchHighlightOverlay: public QWidget
{
    Q_OBJECT

public:
    /// Covers @p area's viewport and paints above its contents. Starts with no target.
    explicit SearchHighlightOverlay(QAbstractScrollArea* area);

    /// Marks @p target, or clears the highlight when it is nullptr.
    void setTarget(QWidget* target);

    /// The widget currently marked, or nullptr.
    QWidget* target() const;

    /**
     * @brief @p target's rect in @p viewport coordinates, grown by @p margins.
     *
     * Returns a null rect whenever there is nothing to mark: no target, no viewport, a target
     * that does not live inside @p viewport, or one an ancestor hides — the inactive page of a
     * QStackedWidget, for instance.
     */
    static QRect highlightRect(const QWidget* target, const QWidget* viewport, QMargins margins);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPointer<QWidget> _target;
    QMetaObject::Connection _targetDestroyed;
};

}  // namespace Gui
