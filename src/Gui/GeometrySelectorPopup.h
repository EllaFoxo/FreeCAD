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

#include "GeometrySelectorWidget.h"  // GeometrySelectorOption

class QVBoxLayout;

namespace Gui
{

/**
 * The geometry selector's dropdown: one selectable row per predefined option, painted
 * through the List token chain, with an optional trailing "Custom…" row. A top-level
 * Qt::Popup positioned under the control by the caller. Emits optionActivated(index) on
 * mouse click or keyboard activation; the Custom entry is the last index (== options.size()).
 */
class GuiExport GeometrySelectorPopup: public QWidget
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

Q_SIGNALS:
    void optionActivated(int index);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void buildRows();
    void setHighlight(int index);
    void moveHighlight(int delta);

    std::vector<GeometrySelectorOption> m_options;
    bool m_allowCustom;
    int m_currentIndex;
    int m_highlight = -1;
    QVBoxLayout* m_rowsLayout = nullptr;
    std::vector<QWidget*> m_rows;
};

}  // namespace Gui
