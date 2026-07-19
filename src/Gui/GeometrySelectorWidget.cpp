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

#include <iterator>

#include <QCoreApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QPaintEvent>
#include <QPixmap>
#include <QStringList>
#include <QStyleOptionFrame>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QToolButton>
#include <QVariant>
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

/// A tool button whose hover state mirrors another widget's, so a control filling a
/// larger interactive surface reflects that surface's hover — not merely its own rect.
/// This lets the change surface stay in its hover fill even while the pointer is over
/// the remove button layered on top of it.
class HostStateToolButton: public QToolButton
{
public:
    explicit HostStateToolButton(QWidget* parent)
        : QToolButton(parent)
    {}

    /// Widget whose hover state this button mirrors.
    QWidget* stateHost = nullptr;

    /// Reports a minimum width that swaps the full label for an ellipsis, so the row
    /// can shrink within the task panel; the label itself is elided in paintEvent.
    QSize minimumSizeHint() const override
    {
        QSize hint = QToolButton::minimumSizeHint();
        if (toolButtonStyle() == Qt::ToolButtonTextBesideIcon && !text().isEmpty()) {
            const int fullTextWidth = fontMetrics().horizontalAdvance(text());
            const int ellipsisWidth = fontMetrics().horizontalAdvance(QStringLiteral("…"));
            hint.setWidth(qMax(0, hint.width() - fullTextWidth + ellipsisWidth));
        }
        return hint;
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QStyleOptionToolButton option;
        initStyleOption(&option);
        if (stateHost != nullptr) {
            option.state.setFlag(QStyle::State_MouseOver, stateHost->underMouse());
        }
        elideLabel(option);
        QStylePainter(this).drawComplexControl(QStyle::CC_ToolButton, option);
    }

private:
    /// Shortens option.text with an ellipsis to fit the width the style leaves for the
    /// label (content rect minus icon + spacing), matching drawToolButtonLabel's layout.
    void elideLabel(QStyleOptionToolButton& option) const
    {
        if (option.text.isEmpty() || option.toolButtonStyle != Qt::ToolButtonTextBesideIcon
            || Gui::Application::Instance == nullptr) {
            return;
        }
        const FreeCADStyle::BoxGeometryDefinition geometry
            = Gui::Application::Instance->freeCADStyle()->resolveBoxGeometry(
                FreeCADStyle::contextOf(this)
            );
        const QRect contentRect = geometry.contentRect(option.rect);
        const int reserved = option.icon.isNull() ? 0
                                                  : option.iconSize.width() + geometry.iconSpacing;
        const int textWidth = contentRect.width() - reserved;
        option.text = option.fontMetrics.elidedText(option.text, Qt::ElideRight, qMax(0, textWidth));
    }
};
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
    for (QToolButton* surface : m_referenceSurfaces) {
        if (hovered) {
            surface->setText(tr("change"));
            surface->setIcon(IconManager::instance().icon(":/icons/tabler/outline/replace.svg"));
        }
        else {
            surface->setText(surface->property("restText").toString());
            surface->setIcon(surface->property("restIcon").value<QIcon>());
        }
        surface->update();
    }
    for (QWidget* control : m_hoverOnly) {
        control->setVisible(hovered);
    }
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

/// The icon shared by every reference, or a null QIcon when they differ or none is
/// available. Icons are compared by their rendered pixmap so distinct objects of the
/// same type (which produce separate QIcon instances) still count as sharing one icon.
static QIcon commonReferenceIcon(const std::vector<GeometryReference>& references)
{
    if (references.empty()) {
        return {};
    }
    QIcon firstIcon = viewProviderIconFor(references.front().object);
    if (firstIcon.isNull()) {
        return {};
    }
    const QImage reference = firstIcon.pixmap(IconSize, IconSize).toImage();
    for (auto it = std::next(references.begin()); it != references.end(); ++it) {
        const QIcon icon = viewProviderIconFor(it->object);
        if (icon.isNull() || icon.pixmap(IconSize, IconSize).toImage() != reference) {
            return {};
        }
    }
    return firstIcon;
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

QWidget* GeometrySelectorWidget::makeReferenceRow()
{
    const std::vector<GeometryReference>& refs = m_selection->references();

    auto* container = new QWidget(this);
    auto* layout = makeRowLayout(container, m_itemSpacing);

    // ---- Change surface: a wide button filling the row's free width --------
    // It mirrors the widget's hover state, so hovering anywhere over the input
    // (even over the clear button beside it) keeps it in its hover fill and
    // clicking it re-enters selection. Every reference is shown as one
    // comma-separated label; the type icon is shown when every reference shares it.
    const QString displayText = joinedReferenceText(refs);
    const QIcon typeIcon = commonReferenceIcon(refs);

    auto* change = new HostStateToolButton(container);
    change->stateHost = this;
    change->setIcon(typeIcon);
    change->setText(displayText);
    change->setIconSize(QSize(IconSize, IconSize));
    change->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    change->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    change->setProperty("restText", displayText);
    change->setProperty("restIcon", QVariant::fromValue(typeIcon));
    styleAsInternalButton(change);
    layout->addWidget(change, 1);
    connect(change, &QToolButton::clicked, this, [this] { m_selection->startSelecting(); });
    m_referenceSurfaces.push_back(change);

    // ---- Clear: a single control that drops every reference. Sits to the
    // right with its own hover, no fill at rest, revealed only while hovered.
    auto* clearButton = makeActionButton(
        container,
        IconManager::instance().icon(":/icons/tabler/outline/trash.svg")
    );
    clearButton->setToolTip(tr("Clear"));
    styleAsInternalButton(clearButton);
    connect(clearButton, &QToolButton::clicked, this, [this] { m_selection->clear(); });
    layout->addWidget(clearButton);

    // Shown only while hovered; registered so setHovered() can toggle it.
    clearButton->hide();
    m_hoverOnly.push_back(clearButton);

    return container;
}

// ---------------------------------------------------------------------------
// Row management
// ---------------------------------------------------------------------------

void GeometrySelectorWidget::clearRows()
{
    m_referenceSurfaces.clear();
    m_hoverOnly.clear();
    m_placeholderButton = nullptr;
    while (QLayoutItem* item = m_contentLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
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
            m_contentLayout->addWidget(makeReferenceRow());  // replaced in Task 2
            break;
    }

    // Reflect the current pointer position if it is already over the widget.
    setHovered(underMouse());
    update();
}
