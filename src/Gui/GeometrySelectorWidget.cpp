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
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedLayout>
#include <QStyleOptionFrame>
#include <QStylePainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <App/Document.h>
#include <App/DocumentObject.h>

#include "Application.h"
#include "Document.h"
#include "ElideLabel.h"
#include "FreeCADStyle.h"
#include "IconManager.h"
#include "Selection/Selection.h"
#include "ViewProvider.h"

using namespace Gui;

// ---------------------------------------------------------------------------
// Local constants and helpers.
// ---------------------------------------------------------------------------

namespace
{
/// Standard glyph size for the inline action icons.
constexpr int IconSize = 16;

/// The reference list grows to this many row strides before it caps and scrolls; the
/// fractional part leaves the next row partially visible to signal more content.
constexpr double MaxVisibleRows = 3.25;

/// At or below this many rows the list fits without scrolling and is laid out directly;
/// beyond it the rows go into a height-capped QScrollArea.
constexpr int MaxRowsWithoutScroll = 3;

/// Opacity of the scrim that dims the reference list beneath the selecting overlay; high
/// enough that the committed references stay only barely visible through it.
constexpr double ScrimOpacity = 0.94;

/// Frame border thickness; the content is inset by this so the painted border stays fully
/// visible even when the selecting overlay's scrim is drawn on top of the rows.
constexpr int FrameBorderThickness = 1;

// Style-metric fallbacks used only when no Gui::Application (and thus no
// FreeCADStyle) is available, e.g. in the headless test harness. In the running
// application these are superseded by the resolved List item box geometry.
constexpr QMargins FallbackItemPadding {6, 4, 6, 4};
constexpr int FallbackSpacing = 6;

/// A compact, flat (auto-raise) icon button styled by the ambient QStyle.
QToolButton* makeActionButton(QWidget* parent, const QIcon& icon)
{
    auto* button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setIcon(icon);
    button->setIconSize(QSize(IconSize, IconSize));
    return button;
}

/// Renders @p button as a flat Link-variant tool button painted by FreeCADStyle — a
/// borderless control whose auto-raise state maps to the Link ButtonType variant.
void styleAsLinkButton(QToolButton* button)
{
    button->setAutoRaise(true);  // auto-raise resolves to the Link ButtonType variant
    if (Gui::Application::Instance) {
        button->setStyle(Gui::Application::Instance->freeCADStyle());
    }
}

/// A chromed (non-flat) text tool button at the Internal (18px) control size, painted by
/// FreeCADStyle. When @p primary it also carries the accent Primary ButtonType variant.
QToolButton* makeInternalTextButton(QWidget* parent, const QString& text, bool primary)
{
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setProperty("controlSize", "internal");
    if (primary) {
        button->setProperty("buttonType", "primary");
    }
    if (Gui::Application::Instance) {
        button->setStyle(Gui::Application::Instance->freeCADStyle());
    }
    return button;
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
    // Resolves the List token chain so the widget matches native lists.
    setProperty("component", "List");
    // Makes real keyboard focus show the focused style like any input.
    setFocusPolicy(Qt::StrongFocus);
    // Repaint on pointer enter/leave so the frame reflects its hovered background.
    setAttribute(Qt::WA_Hover);

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
    option.initFrom(this);  // carries real keyboard-focus and hover state
    option.state |= QStyle::State_Sunken;
    if (m_selection->isSelecting()) {
        option.state |= QStyle::State_HasFocus;  // stay "focused" through viewport picking
    }
    option.features = QStyleOptionFrame::None;
    option.lineWidth = FrameBorderThickness;
    painter.drawPrimitive(QStyle::PE_PanelLineEdit, option);
}

void GeometrySelectorWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    // Re-resolve token-driven metrics when the theme or style swaps out.
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) {
        applyStyleMetrics();
    }
}

void GeometrySelectorWidget::mousePressEvent(QMouseEvent* event)
{
    // Accept the press only in the empty state so the matching release is delivered here;
    // in every other state the rows and their controls handle their own clicks.
    if (event->button() == Qt::LeftButton && visualState() == VisualState::Empty) {
        // Take real keyboard focus so the frame shows its focused ring; the early return
        // otherwise skips the base class's click-to-focus handling.
        setFocus(Qt::MouseFocusReason);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void GeometrySelectorWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && visualState() == VisualState::Empty) {
        m_selection->startSelecting();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

// ---------------------------------------------------------------------------
// Style metrics — margins, spacing and fixed height sourced from tokens.
// ---------------------------------------------------------------------------

void GeometrySelectorWidget::applyStyleMetrics()
{
    QMargins itemPadding = FallbackItemPadding;
    int iconSpacing = FallbackSpacing;

    // Row metrics come from the List *item* tokens (ListItemPadding / ListItemIconSpacing):
    // a list frame draws no container padding, so the per-row inset lives on each row.
    if (Application::Instance) {
        auto* fcStyle = Application::Instance->freeCADStyle();
        // contextOf only honours a non-Root element for recognised item-view widget types;
        // for this plain QWidget (tagged component="List") it leaves element=Root, so pin
        // Item explicitly to reach the ListItem* tokens instead of the empty List root.
        StyleParameters::StyleContext context = FreeCADStyle::contextOf(this);
        context.element = StyleParameters::StyleComponentElement::Item;
        const FreeCADStyle::BoxGeometryDefinition item = fcStyle->resolveBoxGeometry(context);
        itemPadding = item.padding.toMargins();
        iconSpacing = item.iconSpacing;
    }

    m_itemPadding = itemPadding;
    m_itemSpacing = iconSpacing;

    // Inset content by the frame border only; each row supplies its own padding via
    // m_itemPadding, and rows abut with no inter-row gap so the control reads as one
    // continuous list. The border inset keeps the painted frame clear of the overlay scrim.
    layout()->setContentsMargins(
        FrameBorderThickness,
        FrameBorderThickness,
        FrameBorderThickness,
        FrameBorderThickness
    );
    m_contentLayout->setSpacing(0);

    // A single-value selector is exactly one row tall; a multi-value one grows with its rows.
    // Either way the height is driven entirely by the current state's content, so a Fixed
    // vertical policy keeps the parent from stretching the widget when the task panel has
    // spare vertical space — it tracks the content's sizeHint like a line edit.
    QSizePolicy policy = sizePolicy();
    policy.setVerticalPolicy(QSizePolicy::Fixed);
    setSizePolicy(policy);
    if (m_selection->quantity() == GeometryQuantity::Single) {
        setFixedHeight(rowHeight());
    }
    else {
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
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

/// Builds a row's horizontal layout with the given item padding and icon spacing.
static QHBoxLayout* makeRowLayout(QWidget* container, QMargins padding, int spacing)
{
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(padding);
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

/// The selecting-state prompt: the "pick geometry" hint until something is committed, then a
/// running count so the user sees why the widget grows as references accumulate.
static QString selectingPromptText(int referenceCount)
{
    if (referenceCount == 0) {
        return QCoreApplication::translate("Gui::GeometrySelectorWidget", "Select sketch, face…");
    }
    // %n is Qt's numerus mechanism: a single source string that Qt Linguist expands into each
    // language's plural forms, picked at runtime from the count. Untranslated it renders the
    // source with %n substituted, hence the "(s)" placeholder for the source language.
    return QCoreApplication::translate(
        "Gui::GeometrySelectorWidget",
        "%n item(s) selected",
        nullptr,
        referenceCount
    );
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
        QMargins padding,
        int spacing,
        std::function<void()> onActivate,
        std::function<void()> onRemove,
        std::function<void()> onHoverEnter,
        std::function<void()> onHoverLeave,
        QWidget* parent
    )
        : QWidget(parent)
        , m_activate(std::move(onActivate))
        , m_hoverEnter(std::move(onHoverEnter))
        , m_hoverLeave(std::move(onHoverLeave))
    {
        setObjectName(QStringLiteral("gsw_reference_row"));
        // Tag the row as a List row so it can paint the ListRow* hovered background itself.
        setProperty("component", "List");
        auto* rowLayout = new QHBoxLayout(this);
        rowLayout->setContentsMargins(padding);
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
        styleAsLinkButton(m_remove);
        m_remove->hide();
        QObject::connect(m_remove, &QToolButton::clicked, this, [handler = std::move(onRemove)] {
            handler();
        });
        rowLayout->addWidget(m_remove);
    }

protected:
    void enterEvent(QEnterEvent* /*event*/) override
    {
        m_hovered = true;
        update();  // repaint with the hovered row background
        m_remove->show();
        if (m_hoverEnter) {
            m_hoverEnter();
        }
    }
    void leaveEvent(QEvent* /*event*/) override
    {
        m_hovered = false;
        update();
        m_remove->hide();
        if (m_hoverLeave) {
            m_hoverLeave();
        }
    }
    void paintEvent(QPaintEvent* /*event*/) override
    {
        // The row is transparent at rest; only the hovered state paints a background, drawn
        // through the shared List box painting so it matches the tree/list delegates.
        if (!m_hovered || !Application::Instance) {
            return;
        }
        StyleParameters::StyleContext context = FreeCADStyle::contextOf(this);
        context.element = StyleParameters::StyleComponentElement::Row;
        context.state |= StyleParameters::StyleState::Hovered;
        QPainter painter(this);
        Application::Instance->freeCADStyle()->paintBox(&painter, rect(), context);
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        // Accept the press so the matching release is delivered to this row; the child
        // labels do not accept events, so without this the release could be lost.
        if (event->button() == Qt::LeftButton) {
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
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
    std::function<void()> m_hoverEnter;
    std::function<void()> m_hoverLeave;
    bool m_hovered = false;
};

/// The selecting-state chrome: a scrim that dims whatever sits beneath it in a StackAll
/// QStackedLayout, with the italic prompt filling the padded area and the Done/Cancel action
/// buttons positioned absolutely over the right edge — vertically centred in the full overlay
/// height and outside the layout flow, so their taller internal size never dictates the
/// overlay's height. Opaque to mouse events so clicks never reach the dimmed content beneath.
class SelectingOverlay: public QWidget
{
public:
    SelectingOverlay(
        const QString& promptText,
        QMargins padding,
        int spacing,
        bool showConfirm,
        std::function<void()> onDone,
        std::function<void()> onCancel,
        QWidget* parent
    )
        : QWidget(parent)
        , m_padding(padding)
        , m_spacing(spacing)
    {
        setObjectName(QStringLiteral("gsw_overlay"));
        setAttribute(Qt::WA_NoSystemBackground);  // we paint our own scrim
        setAutoFillBackground(false);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(padding);
        layout->setSpacing(spacing);

        auto* prompt = new QLabel(promptText, this);
        QFont italicFont = prompt->font();
        italicFont.setItalic(true);
        prompt->setFont(italicFont);
        prompt->setForegroundRole(QPalette::PlaceholderText);
        prompt->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        prompt->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        layout->addWidget(prompt, 1);

        // Done commits an accumulated Ctrl-multiselect; only the multi-select mode can
        // accumulate, so the caller asks for it only then. The accent Primary variant is
        // carried via the buttonType property inside makeInternalTextButton.
        if (showConfirm) {
            addFloatingButton(
                QCoreApplication::translate("Gui::GeometrySelectorWidget", "Done"),
                /*primary=*/true,
                std::move(onDone),
                QStringLiteral("gsw_confirm")
            );
        }
        // Cancel reverts the session; a neutral chromed internal-size tool button beside Done.
        addFloatingButton(
            QCoreApplication::translate("Gui::GeometrySelectorWidget", "Cancel"),
            /*primary=*/false,
            std::move(onCancel),
            QStringLiteral("gsw_cancel")
        );
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        layoutFloatingButtons();
    }

    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        QColor scrim = palette().color(QPalette::Base);
        scrim.setAlphaF(static_cast<float>(ScrimOpacity));  // dim the list, keep it faintly visible
        painter.fillRect(rect(), scrim);
    }

private:
    /// Creates a chromed internal-size button that floats over the overlay instead of joining
    /// the layout, so its height never contributes to the overlay's size hint.
    void addFloatingButton(
        const QString& text,
        bool primary,
        std::function<void()> onClick,
        const QString& name
    )
    {
        auto* button = makeInternalTextButton(this, text, primary);
        button->setObjectName(name);
        QObject::connect(button, &QToolButton::clicked, this, [handler = std::move(onClick)] {
            handler();
        });
        m_buttons.push_back(button);
    }

    /// Right-aligns the floating buttons from the padded right edge inwards, each vertically
    /// centred in the overlay. The height is clamped to the overlay so a button taller than a
    /// single row is bounded by the frame instead of protruding past it — the buttons are
    /// positioned absolutely and never dictate (nor overflow) the widget height.
    void layoutFloatingButtons()
    {
        int rightEdge = width() - m_padding.right();
        for (std::size_t index = m_buttons.size(); index-- > 0;) {
            QToolButton* button = m_buttons[index];
            const QSize hint = button->sizeHint();
            const int buttonHeight = qMin(hint.height(), height());
            const int left = rightEdge - hint.width();
            const int top = (height() - buttonHeight) / 2;
            button->setGeometry(left, top, hint.width(), buttonHeight);
            rightEdge = left - m_spacing;
        }
    }

    QMargins m_padding;
    int m_spacing;
    std::vector<QToolButton*> m_buttons;
};
}  // namespace

QWidget* GeometrySelectorWidget::makeEmptyRow()
{
    auto* container = new QWidget(this);
    container->setFixedHeight(rowHeight());
    // Let hover and clicks fall through to the frame itself: the empty row carries no
    // interactive control, so the whole frame acts as one button. Without this the label
    // would swallow the mouse and the frame's WA_Hover would never see State_MouseOver.
    container->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* layout = makeRowLayout(container, m_itemPadding, m_itemSpacing);

    // Idle prompt: a plain placeholder label, drawn in the placeholder colour like a
    // native input. Clicking anywhere on the empty frame starts selecting (see
    // mouseReleaseEvent), so no button is needed here — a button would only inflate the row.
    auto* placeholder = new QLabel(tr("Select geometry"), container);
    placeholder->setForegroundRole(QPalette::PlaceholderText);
    placeholder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    placeholder->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(placeholder, 1);

    return container;
}

QWidget* GeometrySelectorWidget::makeReferenceList()
{
    const std::vector<GeometryReference>& references = m_selection->references();
    const int singleRowHeight = rowHeight();

    auto* rowsContainer = new QWidget;
    auto* rowsLayout = new QVBoxLayout(rowsContainer);
    rowsLayout->setContentsMargins(0, 0, 0, 0);
    // Rows abut with no gap; each row's own padding provides the vertical rhythm.
    rowsLayout->setSpacing(0);

    for (std::size_t index = 0; index < references.size(); ++index) {
        auto* referenceRow = new ReferenceRow(
            references[index],
            m_itemPadding,
            m_itemSpacing,
            [this] { m_selection->startSelecting(); },
            [this, index] { m_selection->removeReference(index); },
            [this, reference = references[index]] { previewReferenceInView(reference); },
            [this] { clearReferencePreview(); },
            rowsContainer
        );
        // Pin every row to the resolved row height so the list matches other list-like
        // controls and does not stretch its rows apart to fill spare vertical space.
        referenceRow->setFixedHeight(singleRowHeight);
        rowsLayout->addWidget(referenceRow);
    }

    // While the rows fit, lay them out directly: the container sizes exactly to its rows,
    // matching the neighbouring form controls. A QScrollArea is only introduced once the
    // rows overflow, because it defaults to Expanding and imposes a minimum-size floor
    // that would otherwise inflate a short list.
    if (static_cast<int>(references.size()) <= MaxRowsWithoutScroll) {
        return rowsContainer;
    }

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("gsw_reference_list"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(rowsContainer);

    // Cap the viewport so a partial row peeks and the list starts scrolling; QScrollArea
    // never collapses to its content on its own, so pin the height explicitly.
    scroll->setFixedHeight(referenceListHeight());
    return scroll;
}

QWidget* GeometrySelectorWidget::makeSelecting()
{
    auto* container = new QWidget(this);
    auto* stack = new QStackedLayout(container);
    stack->setStackingMode(QStackedLayout::StackAll);
    stack->setContentsMargins(0, 0, 0, 0);

    // The committed references sit dimmed beneath the overlay; reuse the list builder so the
    // rows are pixel-identical to the idle state. With nothing committed there is nothing to
    // dim, so the backdrop covers the bare frame rather than an idle placeholder row.
    const bool hasReferences = !m_selection->references().empty();
    if (hasReferences) {
        stack->addWidget(makeReferenceList());
    }

    auto* overlay = new SelectingOverlay(
        selectingPromptText(static_cast<int>(m_selection->references().size())),
        m_itemPadding,
        m_itemSpacing,
        /*showConfirm=*/m_selection->quantity() == GeometryQuantity::AllowMultiple,
        [this] { m_selection->stopSelecting(); },
        [this] { m_selection->cancelSelecting(); },
        container
    );
    stack->addWidget(overlay);
    stack->setCurrentWidget(overlay);  // topmost in StackAll

    // A QScrollArea reports its full content as its size hint regardless of its fixed height,
    // so the stack would otherwise grow to every row. Pin the container to the capped list
    // height (or a single row when there is nothing to list) so the selecting state keeps the
    // idle list's height.
    container->setFixedHeight(hasReferences ? referenceListHeight() : rowHeight());
    return container;
}

int GeometrySelectorWidget::rowHeight() const
{
    // One row is its content (icon or label, whichever is taller) plus the item's own
    // vertical padding. m_itemPadding is resolved in applyStyleMetrics from the List item
    // tokens, so a single-reference list stands one padded row tall.
    return qMax(IconSize, fontMetrics().height()) + m_itemPadding.top() + m_itemPadding.bottom();
}

int GeometrySelectorWidget::referenceListHeight() const
{
    const int rowCount = static_cast<int>(m_selection->references().size());
    const int cappedHeight = qRound(MaxVisibleRows * rowHeight());
    return qMin(rowCount * rowHeight(), cappedHeight);
}

// ---------------------------------------------------------------------------
// 3D-view preview — highlight a hovered reference through the shared
// preselection mechanism, so it looks identical to a live cursor highlight.
// ---------------------------------------------------------------------------

void GeometrySelectorWidget::previewReferenceInView(const GeometryReference& reference)
{
    if (!Application::Instance || !reference.object) {
        return;
    }
    App::Document* document = reference.object->getDocument();
    const char* objectName = reference.object->getNameInDocument();
    if (!document || !objectName) {
        return;
    }
    // TreeView source: a highlight requested from a list, matching how the tree preselects a
    // hovered item; the 3D view's unified selection node renders it like a cursor hover.
    Selection().setPreselect(
        document->getName(),
        objectName,
        reference.subName.c_str(),
        0,
        0,
        0,
        SelectionChanges::MsgSource::TreeView
    );
}

void GeometrySelectorWidget::clearReferencePreview()
{
    if (!Application::Instance) {
        return;
    }
    Selection().rmvPreselect();
}

// ---------------------------------------------------------------------------
// Row management
// ---------------------------------------------------------------------------

void GeometrySelectorWidget::clearRows()
{
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
    if (m_selection->isSelecting()) {
        return VisualState::Selecting;
    }
    return m_selection->references().empty() ? VisualState::Empty : VisualState::ReferenceList;
}

void GeometrySelectorWidget::rebuildRows()
{
    clearRows();

    switch (visualState()) {
        case VisualState::Empty:
            m_contentLayout->addWidget(makeEmptyRow());
            break;
        case VisualState::Selecting:
            m_contentLayout->addWidget(makeSelecting());
            break;
        case VisualState::ReferenceList:
            m_contentLayout->addWidget(makeReferenceList());
            break;
    }

    update();
}
