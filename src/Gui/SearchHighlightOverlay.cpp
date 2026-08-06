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

#include "SearchHighlightOverlay.h"

#include <QAbstractScrollArea>
#include <QEvent>
#include <QPainter>
#include <QScrollBar>

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>

using namespace Gui;

SearchHighlightOverlay::SearchHighlightOverlay(QAbstractScrollArea* area)
    : QWidget(area->viewport())
{
    // Never take a click: the controls under the halo stay fully usable.
    setAttribute(Qt::WA_TransparentForMouseEvents);

    // Resolves the SearchHighlight* tokens through FreeCADStyle's custom-namespace mechanism.
    setProperty("component", "SearchHighlight");

    setGeometry(area->viewport()->rect());
    hide();

    area->viewport()->installEventFilter(this);

    // The overlay stands still while the content slides underneath it.
    connect(area->horizontalScrollBar(), &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
    connect(area->verticalScrollBar(), &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
}

QWidget* SearchHighlightOverlay::target() const
{
    return _target;
}

void SearchHighlightOverlay::setTarget(QWidget* target)
{
    if (_target == target) {
        return;
    }

    if (_target) {
        _target->removeEventFilter(this);
    }

    // Held as a Connection rather than disconnected by signature: the pointer-to-member
    // disconnect overload cannot take a null method, and a connection left over from a
    // previous target would clear the current halo when that old widget is eventually deleted.
    disconnect(_targetDestroyed);

    _target = target;

    if (_target) {
        _target->installEventFilter(this);
        _targetDestroyed = connect(_target, &QObject::destroyed, this, [this] { setTarget(nullptr); });

        // The scroll area's own content widget was parented to the viewport first, so the
        // overlay has to be lifted above it.
        raise();
    }

    setVisible(_target != nullptr);
    update();
}

QRect SearchHighlightOverlay::highlightRect(const QWidget* target, const QWidget* viewport, QMargins margins)
{
    if (target == nullptr || viewport == nullptr) {
        return {};
    }

    // isVisibleTo answers "would this show if the viewport were on screen", which is what the
    // halo needs: a target parked on an inactive stacked page has nothing to mark, but one on
    // the live page does even before the dialog is first shown.
    if (!viewport->isAncestorOf(target) || !target->isVisibleTo(viewport)) {
        return {};
    }

    const QRect inViewport {target->mapTo(viewport, QPoint(0, 0)), target->size()};

    return inViewport.marginsAdded(margins);
}

void SearchHighlightOverlay::paintEvent(QPaintEvent* /*event*/)
{
    if (Application::Instance == nullptr) {
        return;
    }

    auto* style = Application::Instance->freeCADStyle();

    const StyleParameters::StyleContext context = FreeCADStyle::contextOf(this);
    const QMarginsF margin = style->resolveBoxGeometry(context).margin;

    const QRect rect = highlightRect(_target, parentWidget(), margin.toMargins());
    if (rect.isNull()) {
        return;
    }

    QPainter painter(this);
    style->paintBox(&painter, rect, context);
}

bool SearchHighlightOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
    }

    switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
            update();
            break;
        default:
            break;
    }

    return QWidget::eventFilter(watched, event);
}
