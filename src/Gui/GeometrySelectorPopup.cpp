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

#include "GeometrySelectorPopup.h"

#include <QEvent>
#include <QKeyEvent>
#include <QListView>
#include <QMouseEvent>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "Application.h"
#include "FreeCADStyle.h"

using namespace Gui;

GeometrySelectorPopup::GeometrySelectorPopup(
    std::vector<GeometrySelectorOption> options,
    bool allowCustom,
    int currentIndex,
    QWidget* parent
)
    : QFrame(parent, Qt::Popup)
    , m_options(std::move(options))
    , m_allowCustom(allowCustom)
    , m_currentIndex(currentIndex)
{
    setObjectName(QStringLiteral("gsw_options_popup"));
    // The frame style a combo popup takes, which is what routes the surface through PE_Frame
    // and the contents inset through SE_ShapedFrameContents.
    setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
    // Any close() — Escape or an outside click — schedules deletion, so a dismissed popup
    // never lingers as a hidden child of the widget.
    setAttribute(Qt::WA_DeleteOnClose);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    m_view = new QListView(this);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Qt sets no WA_Hover on an item view's viewport, so a row is only told the pointer is over
    // it once the view sees plain moves — the same line Tree and the property editor carry.
    m_view->viewport()->setMouseTracking(true);
    m_view->viewport()->installEventFilter(this);
    m_view->installEventFilter(this);
    outerLayout->addWidget(m_view);

    // Keyboard reaches the view rather than the popup, so QListView's own navigation applies.
    // setFocus() is what actually resolves through the proxy onto the view; without it the
    // popup itself stays the focus widget and the view never sees a key.
    setFocusProxy(m_view);
    setFocus();

    buildModel();
    adoptAsDropdown();

    connect(m_view, &QAbstractItemView::clicked, this, [this](const QModelIndex& index) {
        activateIndex(index.row());
    });
}

int GeometrySelectorPopup::optionCount() const
{
    return static_cast<int>(m_options.size()) + (m_allowCustom ? 1 : 0);
}

void GeometrySelectorPopup::buildModel()
{
    m_model = new QStandardItemModel(this);

    const auto addRow = [this](const GeometrySelectorOption& option) {
        auto* item = new QStandardItem(option.label);
        item->setIcon(option.icon);
        item->setEditable(false);
        m_model->appendRow(item);
    };

    for (const GeometrySelectorOption& option : m_options) {
        addRow(option);
    }
    if (m_allowCustom) {
        addRow(GeometrySelectorOption::customEntry());
    }

    m_view->setModel(m_model);

    // Selects as well as moves the cursor, which is what paints the chosen entry and what the
    // "current" placement measures its offset from.
    if (m_currentIndex >= 0 && m_currentIndex < m_model->rowCount()) {
        m_view->setCurrentIndex(m_model->index(m_currentIndex, 0));
    }
}

void GeometrySelectorPopup::adoptAsDropdown()
{
    if (!Application::Instance) {
        return;  // headless: the popup still builds, navigates and activates
    }
    FreeCADStyle* style = Application::Instance->freeCADStyle();
    setStyle(style);
    style->constrainDropdown(m_view, m_currentIndex);
}

QSize GeometrySelectorPopup::sizeHint() const
{
    // Adding the view to the layout activates it, and an activating layout asks for this before
    // the model exists.
    if (!m_model) {
        return QFrame::sizeHint();
    }

    // QListView reports a fixed default extent, so the popup measures its own rows instead.
    // The frame's margins are the styled inset QFrame derives from SE_ShapedFrameContents.
    const QMargins frame = contentsMargins();

    int rowsHeight = 0;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        rowsHeight += m_view->sizeHintForRow(row);
    }

    return {
        m_view->sizeHintForColumn(0) + frame.left() + frame.right(),
        rowsHeight + frame.top() + frame.bottom()
    };
}

void GeometrySelectorPopup::activateIndex(int index)
{
    if (index < 0 || index >= optionCount()) {
        return;
    }
    Q_EMIT optionActivated(index);
}

bool GeometrySelectorPopup::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view->viewport() && event->type() == QEvent::MouseMove) {
        // The row under the pointer becomes the cursor, so Enter picks what the pointer is on —
        // the behaviour QComboBoxPrivateContainer gives a combo box's popup.
        const auto* move = static_cast<QMouseEvent*>(event);
        const QModelIndex under = m_view->indexAt(move->position().toPoint());
        if (under.isValid()) {
            m_view->setCurrentIndex(under);
        }
        return false;
    }

    if (watched == m_view && event->type() == QEvent::KeyPress
        && handleViewKeyPress(static_cast<QKeyEvent*>(event))) {
        return true;
    }

    return QFrame::eventFilter(watched, event);
}

/// Handles the keys a dropdown must answer for itself. Returns whether @p event was consumed;
/// an unconsumed key falls back to the base class through the caller.
bool GeometrySelectorPopup::handleViewKeyPress(QKeyEvent* event)
{
    switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (m_view->currentIndex().isValid()) {
                activateIndex(m_view->currentIndex().row());
            }
            return true;
        case Qt::Key_Escape:
            close();
            return true;
        default:
            return false;
    }
}

void GeometrySelectorPopup::mousePressEvent(QMouseEvent* event)
{
    // A Qt::Popup grabs the mouse; a press outside its geometry dismisses it. WA_DeleteOnClose
    // then frees the popup, so both the outside-click and Escape paths release it.
    if (!rect().contains(event->pos())) {
        close();
        return;
    }
    QFrame::mousePressEvent(event);
}
