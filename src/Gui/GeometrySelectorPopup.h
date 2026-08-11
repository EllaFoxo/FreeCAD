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

#include <QFrame>

#include <FCGlobal.h>

#include "GeometrySelectorWidget.h"  // GeometrySelectorOption

class QListView;
class QStandardItemModel;

namespace Gui
{

/**
 * The geometry selector's dropdown: one selectable row per predefined option, with an optional
 * trailing "Custom…" row, painted and placed as any other dropdown. A top-level Qt::Popup
 * positioned under the control by the caller. Emits optionActivated(index) on mouse click or
 * keyboard activation; the Custom entry is the last index (== options.size()).
 */
class GuiExport GeometrySelectorPopup: public QFrame
{
    Q_OBJECT

public:
    GeometrySelectorPopup(
        std::vector<GeometrySelectorOption> options,
        bool allowCustom,
        int currentIndex,
        QWidget* parent = nullptr
    );

    /// Number of selectable rows: predefined options plus the Custom row when enabled.
    int optionCount() const;
    /// Validates @p index and emits optionActivated; ignored when out of range.
    void activateIndex(int index);

    /// The extent every row needs, so a caller can size the popup before showing it.
    QSize sizeHint() const override;

Q_SIGNALS:
    void optionActivated(int index);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void buildModel();
    void adoptAsDropdown();
    bool handleViewKeyPress(QKeyEvent* event);

    std::vector<GeometrySelectorOption> m_options;
    bool m_allowCustom;
    int m_currentIndex;
    QListView* m_view = nullptr;
    QStandardItemModel* m_model = nullptr;
};

}  // namespace Gui
