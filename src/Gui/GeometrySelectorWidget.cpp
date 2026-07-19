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

#include <functional>

#include <QCoreApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QScrollArea>
#include <QStringList>
#include <QStyleOptionFrame>
#include <QStylePainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <App/DocumentObject.h>

#include "Application.h"
#include "Document.h"
#include "ElideLabel.h"
#include "FreeCADStyle.h"
#include "IconManager.h"
#include "ViewProvider.h"

using namespace Gui;

// ---------------------------------------------------------------------------
// Local constants and helpers.
// ---------------------------------------------------------------------------

namespace
{
/// Standard glyph size for the inline action icons.
constexpr int IconSize = 16;

// Style-metric fallbacks used only when no Gui::Application (and thus no
// FreeCADStyle) is available, e.g. in the headless test harness. In the running
// application these are superseded by the resolved LineEdit box geometry.
constexpr QMargins FallbackPadding {6, 4, 6, 4};
constexpr int FallbackSpacing = 6;
constexpr int FallbackHeight = 28;

/// A compact, flat (auto-raise) icon button styled by the ambient QStyle.
QToolButton* makeActionButton(QWidget* parent, const QIcon& icon)
{
    auto* button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setIcon(icon);
    button->setIconSize(QSize(IconSize, IconSize));
    return button;
}

/// Renders @p button as the simplified "InternalButton" component painted by
/// FreeCADStyle, so the custom inner controls look the same under any application QStyle.
void styleAsInternalButton(QToolButton* button)
{
    button->setAutoRaise(false);
    button->setProperty("component", "InternalButton");
    if (Gui::Application::Instance) {
        button->setStyle(Gui::Application::Instance->freeCADStyle());
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

GeometrySelectorWidget::GeometrySelectorWidget(GeometryQuantity mode, QWidget* parent)
    : QWidget(parent)
    , m_selection(new GeometrySelection(mode, this))
    , m_contentLayout(nullptr)
{
    // Resolves the LineEdit token chain so the widget matches native inputs.
    setProperty("component", "LineEdit");

    // The outer layout insets child widgets to the frame border + padding drawn
    // by paintEvent; the concrete margins come from applyStyleMetrics().
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setSpacing(0);

    auto* contentContainer = new QWidget(this);
    m_contentLayout = new QVBoxLayout(contentContainer);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(contentContainer);

    // React to model changes — every signal simply rebuilds the visible rows.
    connect(m_selection, &GeometrySelection::referencesChanged, this, &GeometrySelectorWidget::rebuildRows);
    connect(
        m_selection,
        &GeometrySelection::selectionModeEntered,
        this,
        &GeometrySelectorWidget::rebuildRows
    );
    connect(
        m_selection,
        &GeometrySelection::selectionModeExited,
        this,
        &GeometrySelectorWidget::rebuildRows
    );

    // Paint as a line-edit panel — signal to Qt that we draw our own background.
    setAutoFillBackground(false);

    applyStyleMetrics();
    rebuildRows();
}

GeometrySelectorWidget::GeometrySelectorWidget(QWidget* parent)
    : GeometrySelectorWidget(GeometryQuantity::Single, parent)
{}

GeometryQuantity GeometrySelectorWidget::quantity() const
{
    return m_selection->quantity();
}

void GeometrySelectorWidget::setQuantity(GeometryQuantity mode)
{
    if (m_selection->quantity() == mode) {
        return;
    }
    m_selection->setQuantity(mode);
    // The mode changes both the fixed-height rule and the rendered rows.
    applyStyleMetrics();
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
    option.lineWidth = 1;
    painter.drawPrimitive(QStyle::PE_PanelLineEdit, option);
}

void GeometrySelectorWidget::enterEvent(QEnterEvent* /*event*/)
{
    setHovered(true);
}

void GeometrySelectorWidget::leaveEvent(QEvent* /*event*/)
{
    setHovered(false);
}

void GeometrySelectorWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    // Re-resolve token-driven metrics when the theme or style swaps out.
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) {
        applyStyleMetrics();
    }
}

// ---------------------------------------------------------------------------
// Style metrics — margins, spacing and fixed height sourced from tokens.
// ---------------------------------------------------------------------------

void GeometrySelectorWidget::applyStyleMetrics()
{
    QMargins padding = FallbackPadding;
    int spacing = FallbackSpacing;
    int height = FallbackHeight;

    // Pull the LineEdit box geometry through the cached, enum-based token path;
    // the padding, spacing and height match every other native input field.
    if (Application::Instance) {
        auto* fcStyle = Application::Instance->freeCADStyle();
        const FreeCADStyle::BoxGeometryDefinition geometry = fcStyle->resolveBoxGeometry(
            FreeCADStyle::contextOf(this)
        );
        padding = geometry.padding.toMargins();
        spacing = geometry.iconSpacing;
        if (geometry.height) {
            height = *geometry.height;
        }
    }

    m_itemSpacing = spacing;
    layout()->setContentsMargins(padding);
    m_contentLayout->setSpacing(spacing);

    // A single-value selector is exactly one line-edit tall; a multi-value one
    // grows with its rows, so only the single-value form pins its height.
    if (m_selection->quantity() == GeometryQuantity::Single) {
        setFixedHeight(height);
    }
    else {
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
    }
}

void GeometrySelectorWidget::setHovered(bool hovered)
{
    if (m_placeholderButton != nullptr) {
        // Placeholder colour at rest, normal text colour while hovered.
        const QPalette::ColorRole textSource = hovered ? QPalette::ButtonText
                                                       : QPalette::PlaceholderText;
        QPalette buttonPalette = m_placeholderButton->palette();
        buttonPalette.setColor(QPalette::ButtonText, palette().color(textSource));
        m_placeholderButton->setPalette(buttonPalette);
    }
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

/// Builds a row's horizontal layout with the given item spacing.
static QHBoxLayout* makeRowLayout(QWidget* container, int spacing)
{
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(spacing);
    return layout;
}

/// Human-readable label for one reference: "Object" or "Object.Sub".
static QString referenceLabel(const GeometryReference& ref)
{
    const QString objectName = ref.object
        ? QString::fromStdString(ref.object->Label.getValue())
        : QCoreApplication::translate("Gui::GeometrySelectorWidget", "<deleted>");
    const QString subName = QString::fromStdString(ref.subName);
    return subName.isEmpty() ? objectName : objectName + u'.' + subName;
}

/// Every reference joined into one comma-separated label.
static QString joinedReferenceText(const std::vector<GeometryReference>& references)
{
    QStringList labels;
    labels.reserve(static_cast<qsizetype>(references.size()));
    for (const GeometryReference& ref : references) {
        labels << referenceLabel(ref);
    }
    return labels.join(QStringLiteral(", "));
}

namespace
{
/// A single reference row: type icon + elided label, plus a remove button that is
/// revealed only while the pointer is over this row. The row body neither highlights nor
/// changes cursor; a click on the body (not the remove button) invokes onActivate.
class ReferenceRow: public QWidget
{
public:
    ReferenceRow(
        const GeometryReference& reference,
        int spacing,
        std::function<void()> onActivate,
        std::function<void()> onRemove,
        QWidget* parent
    )
        : QWidget(parent)
        , m_activate(std::move(onActivate))
    {
        setObjectName(QStringLiteral("gsw_reference_row"));
        auto* rowLayout = new QHBoxLayout(this);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(spacing);

        const QIcon icon = viewProviderIconFor(reference.object);
        if (!icon.isNull()) {
            auto* iconLabel = new QLabel(this);
            iconLabel->setPixmap(icon.pixmap(IconSize, IconSize));
            rowLayout->addWidget(iconLabel);
        }

        auto* label = new ElideLabel(this);
        label->setText(referenceLabel(reference));
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        rowLayout->addWidget(label, 1);

        m_remove = makeActionButton(
            this,
            IconManager::instance().icon(":/icons/tabler/outline/trash.svg")
        );
        m_remove->setToolTip(QCoreApplication::translate("Gui::GeometrySelectorWidget", "Remove"));
        styleAsInternalButton(m_remove);
        m_remove->hide();
        QObject::connect(m_remove, &QToolButton::clicked, this, [handler = std::move(onRemove)] {
            handler();
        });
        rowLayout->addWidget(m_remove);
    }

protected:
    void enterEvent(QEnterEvent* /*event*/) override
    {
        m_remove->show();
    }
    void leaveEvent(QEvent* /*event*/) override
    {
        m_remove->hide();
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        // A click anywhere on the row body (not consumed by the remove button) re-selects.
        if (event->button() == Qt::LeftButton && m_activate) {
            m_activate();
        }
    }

private:
    QToolButton* m_remove = nullptr;
    std::function<void()> m_activate;
};
}  // namespace

QWidget* GeometrySelectorWidget::makeEmptyRow()
{
    auto* container = new QWidget(this);
    auto* layout = makeRowLayout(container, m_itemSpacing);

    // Full-width prompt: "+ Select geometry", styled like the other internal
    // buttons. Its text is drawn in the placeholder colour until hovered (see
    // setHovered), matching a native input's placeholder.
    auto* selectButton = makeActionButton(
        container,
        IconManager::instance().icon(":/icons/tabler/outline/plus.svg")
    );
    selectButton->setText(tr("Select geometry"));
    selectButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    selectButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    styleAsInternalButton(selectButton);
    layout->addWidget(selectButton);

    connect(selectButton, &QToolButton::clicked, this, [this] { m_selection->startSelecting(); });

    m_placeholderButton = selectButton;
    return container;
}

QWidget* GeometrySelectorWidget::makeSelectingRow()
{
    auto* container = new QWidget(this);
    auto* layout = makeRowLayout(container, m_itemSpacing);

    const std::vector<GeometryReference>& refs = m_selection->references();

    if (refs.empty()) {
        // Nothing picked yet: an italic prompt in the muted placeholder colour. The
        // fixed prompt is short, so a plain label suffices.
        auto* prompt = new QLabel(tr("Select sketch, face…"), container);
        QFont italicFont = prompt->font();
        italicFont.setItalic(true);
        prompt->setFont(italicFont);
        prompt->setForegroundRole(QPalette::PlaceholderText);
        prompt->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        layout->addWidget(prompt);
    }
    else {
        // Show the live selection so the user sees what they have and what each edit
        // changes. Elided so a long list never widens the task panel.
        auto* selectionLabel = new ElideLabel(container);
        selectionLabel->setText(joinedReferenceText(refs));
        selectionLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        layout->addWidget(selectionLabel);
    }

    // Confirm the pick: commits the current references and ends the session.
    // A tick glyph, sitting just left of Cancel.
    auto* confirmButton = makeActionButton(
        container,
        IconManager::instance().icon(":/icons/tabler/outline/check.svg")
    );
    confirmButton->setToolTip(tr("Confirm"));
    styleAsInternalButton(confirmButton);
    layout->addWidget(confirmButton);

    connect(confirmButton, &QToolButton::clicked, this, [this] { m_selection->stopSelecting(); });

    // Cancel the pick: ends the session and restores the previous selection.
    // Plain text — a cancel glyph would not read clearly here.
    auto* cancelButton = new QToolButton(container);
    cancelButton->setText(tr("Cancel"));
    cancelButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    styleAsInternalButton(cancelButton);
    layout->addWidget(cancelButton);

    connect(cancelButton, &QToolButton::clicked, this, [this] { m_selection->cancelSelecting(); });

    return container;
}

QWidget* GeometrySelectorWidget::makeReferenceList()
{
    const std::vector<GeometryReference>& references = m_selection->references();

    auto* rowsContainer = new QWidget;
    auto* rowsLayout = new QVBoxLayout(rowsContainer);
    rowsLayout->setContentsMargins(0, 0, 0, 0);
    rowsLayout->setSpacing(m_itemSpacing);

    for (std::size_t index = 0; index < references.size(); ++index) {
        rowsLayout->addWidget(new ReferenceRow(
            references[index],
            m_itemSpacing,
            [this] { m_selection->startSelecting(); },
            [this, index] { m_selection->removeReference(index); },
            rowsContainer
        ));
    }

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("gsw_reference_list"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(rowsContainer);

    // Cap at 3.25 row strides so a 4th row peeks; below the cap the list sizes to content.
    const int stride = rowHeight() + m_itemSpacing;
    scroll->setMaximumHeight(qRound(3.25 * stride));
    return scroll;
}

int GeometrySelectorWidget::rowHeight() const
{
    if (Application::Instance) {
        const FreeCADStyle::BoxGeometryDefinition geometry
            = Application::Instance->freeCADStyle()->resolveBoxGeometry(FreeCADStyle::contextOf(this));
        if (geometry.height) {
            return *geometry.height;
        }
    }
    return qMax(IconSize, fontMetrics().height()) + 2 * FallbackPadding.top();
}

// ---------------------------------------------------------------------------
// Row management
// ---------------------------------------------------------------------------

void GeometrySelectorWidget::clearRows()
{
    m_placeholderButton = nullptr;
    while (QLayoutItem* item = m_contentLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            // Detach immediately so a rebuild triggered from a descendant's own event
            // handler (e.g. this row's remove button) never observes stale rows; the
            // actual C++ deletion is deferred to stay safe for that same reentrant case.
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
}

GeometrySelectorWidget::VisualState GeometrySelectorWidget::visualState() const
{
    const std::size_t count = m_selection->references().size();
    if (m_selection->isSelecting()) {
        return count >= 2 ? VisualState::SelectingOverlay : VisualState::SelectingInline;
    }
    return count == 0 ? VisualState::Empty : VisualState::ReferenceList;
}

void GeometrySelectorWidget::rebuildRows()
{
    clearRows();

    switch (visualState()) {
        case VisualState::Empty:
            m_contentLayout->addWidget(makeEmptyRow());
            break;
        case VisualState::SelectingInline:
        case VisualState::SelectingOverlay:
            m_contentLayout->addWidget(makeSelectingRow());  // replaced in Task 3
            break;
        case VisualState::ReferenceList:
            m_contentLayout->addWidget(makeReferenceList());
            break;
    }

    // Reflect the current pointer position if it is already over the widget.
    setHovered(underMouse());
    update();
}
