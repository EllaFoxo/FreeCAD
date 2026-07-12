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

#include "GeometrySelectorWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QStyleOptionFrame>
#include <QStylePainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <App/DocumentObject.h>

#include "Application.h"
#include "Document.h"
#include "IconManager.h"
#include "ViewProvider.h"

using namespace Gui;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

GeometrySelectorWidget::GeometrySelectorWidget(GeometryQuantity mode, QWidget* parent)
    : QWidget(parent)
    , m_selection(new GeometrySelection(mode, this))
    , m_contentLayout(nullptr)
{
    // The outer layout provides margins that keep child widgets inside the frame
    // border drawn by paintEvent.
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(4, 2, 4, 2);
    outerLayout->setSpacing(0);

    auto* contentContainer = new QWidget(this);
    contentContainer->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_contentLayout = new QVBoxLayout(contentContainer);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(2);
    outerLayout->addWidget(contentContainer);

    // React to model changes.
    connect(m_selection, &GeometrySelection::referencesChanged, this, &GeometrySelectorWidget::rebuildRows);
    connect(
        m_selection,
        &GeometrySelection::selectionModeEntered,
        this,
        &GeometrySelectorWidget::onSelectionModeEntered
    );
    connect(
        m_selection,
        &GeometrySelection::selectionModeExited,
        this,
        &GeometrySelectorWidget::onSelectionModeExited
    );

    // Paint as a line-edit panel — signal to Qt that we draw our own background.
    setAutoFillBackground(false);

    rebuildRows();
}

// ---------------------------------------------------------------------------
// Painting — frame drawn via ambient QStyle so every theme works.
// ---------------------------------------------------------------------------

void GeometrySelectorWidget::paintEvent(QPaintEvent* /*event*/)
{
    QStylePainter painter(this);
    QStyleOptionFrame option;
    option.initFrom(this);
    option.state |= QStyle::State_Sunken;
    option.features = QStyleOptionFrame::None;
    painter.drawPrimitive(QStyle::PE_PanelLineEdit, option);
}

// ---------------------------------------------------------------------------
// Row building helpers
// ---------------------------------------------------------------------------

/// Returns the view-provider icon for the given object, or a null QIcon if
/// Gui::Application is not available or the object has no view provider.
static QIcon viewProviderIconFor(App::DocumentObject* object)
{
    if (!object) {
        return {};
    }
    if (!Gui::Application::Instance) {
        return {};
    }
    App::Document* appDoc = object->getDocument();
    if (!appDoc) {
        return {};
    }
    Gui::Document* guiDoc = Gui::Application::Instance->getDocument(appDoc);
    if (!guiDoc) {
        return {};
    }
    Gui::ViewProvider* viewProvider = guiDoc->getViewProvider(object);
    if (!viewProvider) {
        return {};
    }
    return viewProvider->getIcon();
}

GeometrySelectorWidget::ReferenceRow GeometrySelectorWidget::makeEmptyRow()
{
    auto* container = new QWidget(this);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* selectButton = new QToolButton(container);
    selectButton->setText(tr("+ Select geometry"));
    selectButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    selectButton->setAutoRaise(true);
    layout->addWidget(selectButton);
    layout->addStretch();

    connect(selectButton, &QToolButton::clicked, this, [this] { m_selection->startSelecting(); });

    return ReferenceRow {
        .container = container,
        .iconLabel = nullptr,
        .nameLabel = nullptr,
        .actionButton = selectButton,
        .removeButton = nullptr,
    };
}

GeometrySelectorWidget::ReferenceRow GeometrySelectorWidget::makeSelectingRow()
{
    auto* container = new QWidget(this);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* placeholderLabel = new QLabel(tr("Select sketch, face…"), container);
    placeholderLabel->setEnabled(false);
    layout->addWidget(placeholderLabel);
    layout->addStretch();

    auto* cancelButton = new QToolButton(container);
    cancelButton->setText(tr("Cancel"));
    cancelButton->setAutoRaise(true);
    layout->addWidget(cancelButton);

    connect(cancelButton, &QToolButton::clicked, this, [this] {
        m_selection->stopSelecting();
        m_selection->clear();
    });

    return ReferenceRow {
        .container = container,
        .iconLabel = nullptr,
        .nameLabel = placeholderLabel,
        .actionButton = cancelButton,
        .removeButton = nullptr,
    };
}

GeometrySelectorWidget::ReferenceRow GeometrySelectorWidget::makeReferenceRow(std::size_t index)
{
    const std::vector<GeometryReference>& refs = m_selection->references();
    const GeometryReference& ref = refs[index];

    auto* container = new QWidget(this);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    // Type icon from view provider — degrades gracefully if unavailable.
    auto* iconLabel = new QLabel(container);
    const QIcon providerIcon = viewProviderIconFor(ref.object);
    if (!providerIcon.isNull()) {
        const QSize iconSize(16, 16);
        iconLabel->setPixmap(providerIcon.pixmap(iconSize));
        iconLabel->setFixedSize(iconSize);
    }
    else {
        iconLabel->hide();
    }
    layout->addWidget(iconLabel);

    // Object + subelement name.
    const QString objectName = ref.object ? QString::fromStdString(ref.object->Label.getValue())
                                          : tr("<deleted>");
    const QString subName = QString::fromStdString(ref.subName);
    const QString displayText = subName.isEmpty() ? objectName : objectName + u'.' + subName;

    auto* nameLabel = new QLabel(displayText, container);
    nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(nameLabel);

    // Change button (re-enter selecting mode to pick a replacement).
    auto* changeButton = new QToolButton(container);
    changeButton->setText(tr("Change"));
    changeButton->setAutoRaise(true);
    layout->addWidget(changeButton);

    connect(changeButton, &QToolButton::clicked, this, [this] { m_selection->startSelecting(); });

    // Remove button: only shown in AllowMultiple mode.
    QToolButton* removeButton = nullptr;
    if (m_selection->quantity() == GeometryQuantity::AllowMultiple) {
        removeButton = new QToolButton(container);
        removeButton->setText(tr("Remove"));
        removeButton->setAutoRaise(true);
        layout->addWidget(removeButton);

        connect(removeButton, &QToolButton::clicked, this, [this, index] {
            m_selection->removeReference(index);
        });
    }
    else {
        // Single mode: clear button acts as remove.
        auto* clearButton = new QToolButton(container);
        clearButton->setText(tr("Clear"));
        clearButton->setAutoRaise(true);
        layout->addWidget(clearButton);

        connect(clearButton, &QToolButton::clicked, this, [this] { m_selection->clear(); });

        removeButton = clearButton;
    }

    return ReferenceRow {
        .container = container,
        .iconLabel = iconLabel,
        .nameLabel = nameLabel,
        .actionButton = changeButton,
        .removeButton = removeButton,
    };
}

// ---------------------------------------------------------------------------
// Row management
// ---------------------------------------------------------------------------

void GeometrySelectorWidget::clearRows()
{
    while (QLayoutItem* item = m_contentLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void GeometrySelectorWidget::addRowWidget(QWidget* rowWidget)
{
    m_contentLayout->addWidget(rowWidget);
}

// ---------------------------------------------------------------------------
// Slot implementations
// ---------------------------------------------------------------------------

void GeometrySelectorWidget::rebuildRows()
{
    clearRows();

    const std::vector<GeometryReference>& refs = m_selection->references();

    if (m_selection->isSelecting()) {
        addRowWidget(makeSelectingRow().container);
        return;
    }

    if (refs.empty()) {
        addRowWidget(makeEmptyRow().container);
        return;
    }

    for (std::size_t index = 0; index < refs.size(); ++index) {
        addRowWidget(makeReferenceRow(index).container);
    }
}

void GeometrySelectorWidget::onSelectionModeEntered()
{
    rebuildRows();
}

void GeometrySelectorWidget::onSelectionModeExited()
{
    rebuildRows();
}
