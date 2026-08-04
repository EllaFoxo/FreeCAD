// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2025 Kacper Donat <kacper@kadet.net>                     *
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

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <FCGlobal.h>
#include <Base/Color.h>
#include <QBrush>
#include <QColor>
#include <QLine>
#include <QMarginsF>
#include <QPainter>
#include <QProxyStyle>
#include <QAbstractScrollArea>
#include <QComboBox>
#include <QPushButton>
#include <QStyleOption>
#include <QStyleOptionHeader>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>

#include "StyleParameters/Value.h"
#include "StyleParameters/StyleContext.h"

class QListView;
class QTextDocument;
class QTreeView;

namespace Gui
{

class GuiExport FreeCADStyle: public QProxyStyle
{
    Q_OBJECT

public:
    FreeCADStyle();
    ~FreeCADStyle() override;

    /**
     * @brief Per-corner border radii, each stored as a Numeric (possibly with "%" unit).
     *
     * Percent values are resolved to absolute pixels at paint time via resolve().
     * Use setLeft/setRight/setTop/setBottom to zero out corners (e.g. for tab shapes).
     */
    struct CornerRadii
    {
        StyleParameters::Numeric topLeft = {.value = 0, .unit = "px"};
        StyleParameters::Numeric topRight = {.value = 0, .unit = "px"};
        StyleParameters::Numeric bottomRight = {.value = 0, .unit = "px"};
        StyleParameters::Numeric bottomLeft = {.value = 0, .unit = "px"};

        void setLeft(qreal left)
        {
            topLeft = bottomLeft = {.value = left, .unit = "px"};
        }

        void setRight(qreal right)
        {
            topRight = bottomRight = {.value = right, .unit = "px"};
        }

        void setTop(qreal top)
        {
            topLeft = topRight = {.value = top, .unit = "px"};
        }

        void setBottom(qreal bottom)
        {
            bottomLeft = bottomRight = {.value = bottom, .unit = "px"};
        }

        CornerRadii enlarged(qreal amount) const
        {
            CornerRadii result = *this;

            result.topLeft.value += amount;
            result.topRight.value += amount;
            result.bottomRight.value += amount;
            result.bottomLeft.value += amount;

            return result;
        }

        bool isRounded() const
        {
            return topLeft.value > 0 || topRight.value > 0 || bottomRight.value > 0
                || bottomLeft.value > 0;
        }

        /**
         * @brief Resolves any percent-unit radii to absolute pixel values.
         *
         * A "%" radius is resolved as value/100 * min(width, height), matching
         * the CSS border-radius convention for uniform corner shapes. Absolute
         * radii ("px" or dimensionless) are passed through unchanged.
         */
        CornerRadii resolve(QSizeF size) const;
    };

    /**
     * @brief Describes an inward shadow drawn on top of a box background.
     */
    struct InnerShadow
    {
        QColor color;
        qreal x = 0;
        qreal y = 0;
        qreal blur = 0;
    };

    /**
     * @brief Per-side border colors in CSS TRBL order.
     *
     * When all four sides are equal, isUniform() returns true and uniform()
     * gives the shared color, enabling a single-fill fast path in drawBoxBackground().
     */
    struct BorderColorsPerSide
    {
        QColor top;
        QColor right;
        QColor bottom;
        QColor left;

        bool isUniform() const
        {
            return top == right && right == bottom && bottom == left;
        }

        QColor uniform() const
        {
            return top;
        }
    };

    /**
     * @brief Describes the visual appearance of a painted background box.
     *
     * All border fields must be set together (borderColor + borderThickness)
     * for a border to be drawn; partial specification is silently ignored.
     */
    struct BoxStyleDefinition
    {
        QBrush background;
        std::optional<BorderColorsPerSide> borderColor;
        std::optional<QMarginsF> borderThickness;
        CornerRadii borderRadius;  // default: all zero (sharp corners)
        std::optional<InnerShadow> innerShadow;
    };

    /**
     * @brief Describes the spatial layout properties of a box-shaped widget.
     *
     * Resolved from Design System tokens:
     *   Padding, Height, MinWidth, MaxWidth, Width, MinHeight, MaxHeight, IconSpacing.
     *
     * Constraint semantics (applied by constrain() and sizeFromContents()):
     *   1. Fixed overrides (width, height) are applied first — pin the dimension absolutely.
     *   2. min* clamps raise the result.
     *   3. max* clamps lower the result.
     *
     * Usage guidance:
     *   - Use sizeFromContents(contentSize) for components that own their size computation
     *     (PushButton, ToolButton, ItemViewItem): adds padding then constrains.
     *   - Use constrain(result) for components that delegate to the parent style first
     *     (ComboBox, LineEdit, SpinBox): only applies constraints to the delegated size.
     */
    struct BoxGeometryDefinition
    {
        QMarginsF padding;
        QMarginsF margin;

        std::optional<int> height;
        std::optional<int> minWidth;
        std::optional<int> width;      // fixed width override
        std::optional<int> maxWidth;   // maximum width clamp
        std::optional<int> minHeight;  // minimum height clamp
        std::optional<int> maxHeight;  // maximum height clamp
        /** Qt hardcodes this many pixels between an icon and its label text. */
        static constexpr int qtBuiltInIconGap = 4;

        int iconSpacing = qtBuiltInIconGap;  // fallback matches Qt's built-in

        /** Vertical gap reserved between consecutive item-view rows (ListItemSpacing). The gap
         *  is excluded from the content rect and the row highlight, so it renders as the list
         *  background between rows. 0 = rows abut. */
        int spacing = 0;

        /**
         * @brief Width delta to replace Qt's hardcoded icon–text gap with the token spacing.
         *
         * Add this to a width computed by Qt (sizeHint, sizeFromContents) when Qt has already
         * baked in qtBuiltInIconGap pixels for the icon–text gap and you want the token value.
         * Returns 0 when the token matches Qt's default, so it is always safe to apply.
         */
        [[nodiscard]] int iconGapDelta() const
        {
            return iconSpacing - qtBuiltInIconGap;
        }

        /** @brief Total horizontal padding (left + right), in pixels. */
        [[nodiscard]] int paddingH() const
        {
            return static_cast<int>(padding.left() + padding.right());
        }

        /** @brief Total vertical padding (top + bottom), in pixels. */
        [[nodiscard]] int paddingV() const
        {
            return static_cast<int>(padding.top() + padding.bottom());
        }

        /** @brief Applies all dimension constraints to a size. Fixed overrides first, then min
         * clamps up, max clamps down. */
        [[nodiscard]] QSize constrain(QSize size) const
        {
            if (width) {
                size.setWidth(*width);
            }
            if (height) {
                size.setHeight(*height);
            }
            if (minWidth) {
                size.setWidth(std::max(size.width(), *minWidth));
            }
            if (minHeight) {
                size.setHeight(std::max(size.height(), *minHeight));
            }
            if (maxWidth) {
                size.setWidth(std::min(size.width(), *maxWidth));
            }
            if (maxHeight) {
                size.setHeight(std::min(size.height(), *maxHeight));
            }
            return size;
        }

        /** @brief Applies all dimension constraints to a rect, preserving top-left position. */
        [[nodiscard]] QRect constrain(const QRect& rect) const
        {
            return {rect.topLeft(), constrain(rect.size())};
        }

        /** @brief Computes outer widget size: adds padding to content size, then constrains.
         *  Use for components that own their size computation (PushButton, ToolButton,
         * ItemViewItem). Use constrain(result) for components that delegate to the parent style
         * first (ComboBox, LineEdit). */
        [[nodiscard]] QSize sizeFromContents(QSize contentSize) const
        {
            return constrain(contentSize.grownBy(padding.toMargins()));
        }

        [[nodiscard]] QSize marginBox(QSize contentSize) const
        {
            return sizeFromContents(contentSize).grownBy(margin.toMargins());
        }

        /** @brief Returns @p rect inset by this geometry's padding. */
        [[nodiscard]] QRect contentRect(const QRect& rect) const
        {
            return borderRect(rect).marginsRemoved(padding.toMargins());
        }

        /** @brief Returns @p rect inset by this geometry's padding. */
        [[nodiscard]] QRect contentRect(const QRect& rect, const QSize& size) const
        {
            if (rect.width() <= size.width() && rect.height() <= size.height()) {
                return rect;
            }

            int availableWidth = rect.width() - size.width();
            int availableHeight = rect.height() - size.height();

            if (availableWidth > paddingH() && availableHeight > paddingV()) {
                return contentRect(rect);
            }

            double scaleHorizontal = qMin(static_cast<double>(availableWidth) / paddingH(), 1.0);
            double scaleVertical = qMin(static_cast<double>(availableHeight) / paddingV(), 1.0);

            return rect.adjusted(
                static_cast<int>(padding.left() * scaleHorizontal),
                static_cast<int>(padding.top() * scaleVertical),
                -static_cast<int>(padding.right() * scaleHorizontal),
                -static_cast<int>(padding.bottom() * scaleVertical)
            );
        }

        [[nodiscard]] QRect borderRect(QRect rect) const
        {
            return rect.marginsRemoved(margin.toMargins());
        }
    };

    void polish(QPalette& palette) override;

    int pixelMetric(
        PixelMetric metric,
        const QStyleOption* option = nullptr,
        const QWidget* widget = nullptr
    ) const override;

    std::optional<int> resolvePixelMetric(
        PixelMetric metric,
        const QStyleOption* option,
        const QWidget* widget
    ) const;

    int styleHint(
        StyleHint hint,
        const QStyleOption* option,
        const QWidget* widget,
        QStyleHintReturn* returnData
    ) const override;

    void polish(QWidget* widget) override;
    void unpolish(QWidget* widget) override;

    /**
     * @brief Resolves a BoxGeometryDefinition from a @p context using the token cache.
     */
    BoxGeometryDefinition resolveBoxGeometry(const StyleParameters::StyleContext& context) const;

    /**
     * @brief Resolves a BoxStyleDefinition from a @p context using the token cache.
     */
    BoxStyleDefinition resolveBoxStyle(const StyleParameters::StyleContext& context) const;

    /**
     * @brief Paints the themed box (fill, border, overlay) resolved from @p context into
     * @p rect, so a custom widget can reuse the same painting the delegates use — e.g. a
     * list row drawing its own hovered background from the ListRow* tokens.
     */
    void paintBox(QPainter* painter, const QRect& rect, const StyleParameters::StyleContext& context) const;

    /**
     * @brief Builds a StyleContext from a widget and its current style option.
     *
     * Derives component from the widget type, variant slots from widget properties
     * (controlSize, isDefault, isFlat, autoRaise, property("flat")), and state
     * from option->state flags. Passing @p option as nullptr yields Normal state.
     */
    static StyleParameters::StyleContext contextOf(
        const QWidget* widget,
        const QStyleOption* option = nullptr,
        const StyleParameters::StyleComponentElement& element
        = StyleParameters::StyleComponentElement::Root
    );

    /**
     * @brief Recomputes the inherited transparency of @p widget and everything below it.
     *
     * @param widget    Root of the subtree to update.
     * @param inherited Transparency of the surface behind @p widget. A widget carrying the
     *                  "transparent" property overrides this for itself, opening a root.
     */
    void updateTransparency(QWidget* widget, bool inherited);

    /**
     * @brief Whether @p widget is painted over a transparent surface.
     *
     * Returns false for a null widget and for any widget the propagator has not reached.
     */
    static bool isTransparent(const QWidget* widget);

    /**
     * @brief The connector strokes for one indent cell of a tree view's branch column.
     *
     * @param cell     One indentation step wide, one row tall, as Qt hands it to
     *                 PE_IndicatorBranch.
     * @param state    Qt's branch flags. State_Item marks the level owning the item;
     *                 State_Sibling means a sibling follows below at that level.
     * @param topLevel True for a cell belonging to a root item, which has no parent to
     *                 reach toward and so draws nothing.
     * @param direction Layout direction of the view. In a right-to-left layout the item's
     *                  own cell is the leftmost of the branch cells, so the elbow's stub
     *                  runs to the cell's left edge instead of its right edge.
     * @param leadingGap Inter-row gap reserved above this row, so the elbow meets the item
     *                   box rather than the taller cell. Guides still span the whole cell.
     *
     * A cell carrying an expand arrow leaves it clear: the strokes stop short of the centre
     * so the arrow occupies a gap rather than sitting on top of a line.
     */
    static QList<QLineF> branchSegments(
        const QRect& cell,
        QStyle::State state,
        bool topLevel,
        Qt::LayoutDirection direction,
        int leadingGap
    );

    /**
     * @brief The point a tree indent cell's connectors converge on, and where its arrow sits.
     */
    static QPointF branchCenter(const QRect& cell, int leadingGap);

protected:
    void drawPrimitive(
        PrimitiveElement element,
        const QStyleOption* option,
        QPainter* painter,
        const QWidget* widget = nullptr
    ) const override;

    void drawComplexControl(
        ComplexControl control,
        const QStyleOptionComplex* option,
        QPainter* painter,
        const QWidget* widget = nullptr
    ) const override;

    void drawControl(
        ControlElement element,
        const QStyleOption* option,
        QPainter* painter,
        const QWidget* widget = nullptr
    ) const override;

    QSize sizeFromContents(
        ContentsType type,
        const QStyleOption* option,
        const QSize& size,
        const QWidget* widget = nullptr
    ) const override;

    QRect subElementRect(
        SubElement element,
        const QStyleOption* option,
        const QWidget* widget = nullptr
    ) const override;

    QRect subControlRect(
        ComplexControl complexControl,
        const QStyleOptionComplex* option,
        SubControl subControl,
        const QWidget* widget = nullptr
    ) const override;

    /**
     * @brief Paints a background box with optional rounded corners and border.
     */
    static void drawBoxBackground(QPainter* painter, const QRect& rect, const BoxStyleDefinition& style);

    /**
     * @brief Which layer of a row's background a paint call is responsible for.
     *
     * Surface is the row at rest and is painted before the branch column and the cells, so
     * both sit on top of it. Interaction is the hover / pressed / selected fill, painted after
     * them so it reads as an overlay. Both stay inside the rect Qt hands them: a view emits
     * these once per column, so a fill reaching past its own rect would be repainted by the
     * next column's surface.
     */
    enum class RowLayer : std::uint8_t
    {
        Surface,
        Interaction,
    };

    /// Whether a cell is the one nearest its view's leading edge.
    static bool isLeadingCell(const QStyleOptionViewItem* vopt);

    /// Grows a leading cell's rect over the branch gutter that precedes it.
    static void reachToLeadingEdge(QRect& rect, const QStyleOptionViewItem* vopt, const QWidget* widget);

    void drawItemViewRow(
        QPainter* painter,
        const QStyleOptionViewItem* vopt,
        const QWidget* widget,
        RowLayer layer
    ) const;

    /**
     * @brief Paints the connector lines and expand arrow for one tree indent cell.
     *
     * Connectors are suppressed for a widget carrying `branches == false`, and for any
     * component whose branch colour does not resolve. The expand arrow is always drawn.
     */
    void drawItemViewBranch(QPainter* painter, const QStyleOption* option, const QWidget* widget) const;

    /**
     * @brief Paints the expand indicator of a tree item, centred in the gap its connectors leave.
     *
     * Uses the same chevron the style draws for combo boxes and spin boxes, so every arrow in
     * the application is one shape.
     */
    void drawBranchArrow(QPainter* painter, const QStyleOption* option, const QWidget* widget) const;

    /**
     * @brief The inter-row gap reserved above a row, or zero for the topmost one.
     */
    int leadingRowGap(const QStyleOption* option, const QWidget* widget) const;

    /**
     * @brief Whether a branch cell sits at the leading edge of the tree's own column.
     *
     * Reads the tree column's own viewport position rather than assuming it starts at
     * x == 0, so a horizontally scrolled view or a tree column relocated by
     * QTreeView::setTreePosition() still identifies its root cells correctly. Mirrors to
     * the column's trailing edge in a right-to-left layout.
     */
    static bool atTreeColumnLeadingEdge(
        const QTreeView* view,
        const QRect& cellRect,
        Qt::LayoutDirection direction
    );

    /**
     * @brief Placement of the three parts of an item-view cell: check indicator, icon and text.
     *
     * Parts that the item does not have are left as null rects.
     */
    struct ItemViewLayout
    {
        QRect checkIndicator;
        QRect decoration;
        QRect text;
    };

    /**
     * @brief Lays out the parts of an item-view cell from the Item token geometry.
     *
     * The outer inset comes from Padding and every gap between parts from IconSpacing, so
     * item views space their icons on the same scale as the rest of the design system.
     *
     * Returns nullopt for cells this style does not describe — anything but a recognised
     * item view, and the stacked (icon-mode) or word-wrapped arrangements, which keep
     * Qt's own layout.
     */
    std::optional<ItemViewLayout> itemViewLayout(
        const QStyleOptionViewItem* option,
        const QWidget* widget
    ) const;

    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    /**
     * @brief Resolves a single named parameter from the application's StyleParameterManager.
     *
     * Returns nullopt if the manager is unavailable or the parameter is not defined.
     */
    std::optional<StyleParameters::Value> resolve(std::string_view name) const;

    /**
     * @brief Tries each name in order and returns the first match.
     *
     * Useful for resolved-with-fallback patterns, e.g.:
     * @code{.cpp}
     * resolve({"ToolButtonSmallPadding", "ToolButtonPadding"})
     * @endcode
     */
    std::optional<StyleParameters::Value> resolve(std::initializer_list<std::string_view> names) const;

    /**
     * @brief Tries resolving each @p prefix concatenated with @p suffix, in order.
     *
     * Useful for the prefix-fallback pattern used in resolveBoxBackground:
     * @code{.cpp}
     * resolve({"ButtonHoverPrimary", "ButtonHover", "Button"}, "Background")
     * @endcode
     */
    std::optional<StyleParameters::Value> resolve(
        std::initializer_list<std::string_view> prefixes,
        std::string_view suffix
    ) const;

    /**
     * @brief Resolves a style token from a @p context and @p property with caching.
     *
     * Builds the token prefix fallback chain from the context (component, variant,
     * active state flags in priority order) and caches the result so subsequent
     * calls for the same (context, property) tuple avoid all string operations.
     *
     * The cache is invalidated by calling clearTokenCache(), which should be done
     * whenever the active theme changes.
     */
    std::optional<StyleParameters::Value> resolve(
        const StyleParameters::StyleContext& context,
        StyleParameters::StyleProperty property
    ) const;

    /**
     * @brief Typed variants of each resolve() overload.
     *
     * Wraps the corresponding untyped resolve() with StyleParameters::valueAs<T>,
     * so call sites obtain a specific domain type (Numeric, Insets, Corners, …)
     * directly as an optional without explicit holds<>/get<> checks or try/catch:
     *
     * @code{.cpp}
     * if (const auto height = resolve<Numeric>(context, StyleProperty::Height)) {
     *     widget->setFixedHeight(static_cast<int>(height->value));
     * }
     * if (const auto padding = resolve<Insets>(context, StyleProperty::Padding)) {
     *     paddingF = Base::convertTo<QMarginsF>(*padding);
     * }
     * @endcode
     *
     * For variant member types (Numeric, Base::Color, std::string, Tuple) the
     * value is returned only when it holds exactly T.  For domain wrapper types
     * constructible from Value (Insets, Corners, InnerShadow, …) construction is
     * attempted and nullopt is returned on failure.
     */
    template<typename T>
    std::optional<T> resolve(std::string_view name) const
    {
        return StyleParameters::valueAs<T>(resolve(name));
    }

    template<typename T>
    std::optional<T> resolve(std::initializer_list<std::string_view> names) const
    {
        return StyleParameters::valueAs<T>(resolve(names));
    }

    template<typename T>
    std::optional<T> resolve(std::initializer_list<std::string_view> prefixes, std::string_view suffix) const
    {
        return StyleParameters::valueAs<T>(resolve(prefixes, suffix));
    }

    template<typename T>
    std::optional<T> resolve(
        const StyleParameters::StyleContext& context,
        StyleParameters::StyleProperty property
    ) const
    {
        return StyleParameters::valueAs<T>(resolve(context, property));
    }

private:
    static StyleParameters::StyleContext withNorthPosition(
        const StyleParameters::StyleContext& context
    );

    static StyleParameters::Position tabPositionOf(QTabBar::Shape shape);
    static StyleParameters::Position toolbarPositionOf(const QToolBar* toolbar);
    int tabOverlapOf(const QStyleOptionTab* option, const QWidget* widget) const;

    /**
     * @brief Frame width for an item view widget, expanded by the root list container padding.
     *
     * Returns nullopt for widgets other than QAbstractItemView, or when no positive
     * container padding is defined.
     */
    std::optional<int> resolveItemViewFrameWidth(const QStyleOption* option, const QWidget* widget) const;


    static QRect tabVisualRect(const QRect& rect, int tabOverlap, bool isVertical);

    void drawMenuBarItem(QPainter* painter, const QStyleOptionMenuItem* option, const QWidget* widget) const;

    void drawHeaderSection(QPainter* painter, const QStyleOptionHeader* option, const QWidget* widget) const;

    void drawTabCloseButton(QPainter* painter, const QStyleOption* option, const QWidget* widget) const;

    void drawTabBarTab(QPainter* painter, const QStyleOptionTab* option, const QWidget* widget) const;

    void drawTabBarTabLabel(QPainter* painter, const QStyleOptionTab* option, const QWidget* widget) const;

    void drawTabBarBase(
        QPainter* painter,
        const QStyleOptionTabBarBase* option,
        const QWidget* widget
    ) const;

    void drawTabWidgetFrame(
        QPainter* painter,
        const QStyleOptionTabWidgetFrame* option,
        const QWidget* widget
    ) const;

    void drawRadioButtonDot(
        QPainter* painter,
        const QRect& rect,
        const StyleParameters::StyleContext& context,
        const QPalette& palette
    ) const;

    void drawCheckMark(
        QPainter* painter,
        const QRect& rect,
        const StyleParameters::StyleContext& context,
        const QPalette& palette
    ) const;

    void drawIndeterminateMark(
        QPainter* painter,
        const QRect& rect,
        const StyleParameters::StyleContext& context,
        const QPalette& palette
    ) const;

    void drawChevronArrow(
        QPainter* painter,
        const QRect& rect,
        Qt::ArrowType direction,
        const QColor& color
    ) const;

    void drawPushButtonLabel(
        QPainter* painter,
        const QStyleOptionButton* option,
        const QWidget* widget
    ) const;

    void drawToolButtonLabel(
        QPainter* painter,
        const QStyleOptionToolButton* option,
        const QWidget* widget
    ) const;

    void drawComboBoxLabel(
        QPainter* painter,
        const QStyleOptionComboBox* option,
        const QWidget* widget
    ) const;

    /**
     * @brief Draws a component background using the token-based box model.
     *
     * Resolves BoxStyleDefinition from @p context and paints via drawBoxBackground().
     * This is the primary entry point for painting any box-model component — both
     * Qt-native widgets overridden here and custom components outside the Qt style system.
     */
    void drawComponent(
        QPainter* painter,
        const QRect& rect,
        const StyleParameters::StyleContext& context
    ) const;

    void drawComponent(
        QPainter* painter,
        const QRect& rect,
        const QWidget* widget,
        const QStyleOption* option = nullptr
    ) const;

    void drawSeparatorLine(QPainter* painter, const QRect& rect, bool isHorizontal) const;

    /// Recomputes and applies the rounded-rect clip mask on a scroll area's viewport.
    /// Must be called after polish and on every viewport resize.
    void updateScrollAreaMask(QAbstractScrollArea* scrollArea) const;
    void drawSpinBox(const QStyleOptionSpinBox* option, QPainter* painter, const QWidget* widget) const;
    void drawComboBox(const QStyleOptionComboBox* option, QPainter* painter, const QWidget* widget) const;
    void drawToolButton(
        const QStyleOptionToolButton* option,
        QPainter* painter,
        const QWidget* widget
    ) const;

    /**
     * @brief Resolves the BoxStyleDefinition for a tool button half, zeroing seam borders/radii.
     *
     * For MenuButtonPopup buttons the two halves (Root = main button, Menu = dropdown strip)
     * share an edge. This method removes the border on the Menu half's joining edge to avoid
     * a double border, and zeroes the corner radii of both halves at the seam so the join
     * looks flat.  For non-split buttons (hasMenuButton == false) the style is returned
     * unchanged.
     */
    BoxStyleDefinition seamedBoxStyle(
        const StyleParameters::StyleContext& context,
        StyleParameters::StyleComponentElement element,
        bool hasMenuButton,
        bool isVertical
    ) const;

    QRect comboBoxSubControlRect(
        const QStyleOptionComboBox* option,
        SubControl subControl,
        const QWidget* widget
    ) const;

    QRect spinBoxSubControlRect(
        const QStyleOptionSpinBox* option,
        SubControl subControl,
        const QWidget* widget
    ) const;

    QRect toolButtonSubControlRect(
        const QStyleOptionToolButton* option,
        SubControl subControl,
        const QWidget* widget
    ) const;

    QSize tabBarTabSizeFromContents(
        const QStyleOption* option,
        const QSize& size,
        const QWidget* widget
    ) const;

    QSize toolButtonSizeFromContents(
        const QStyleOptionToolButton* option,
        const QSize& size,
        const QWidget* widget
    ) const;

    QSize itemViewItemSizeFromContents(
        const QStyleOption* option,
        const QSize& size,
        const QWidget* widget
    ) const;

    /** @brief Whether @p option describes a cell laid out by itemViewLayout(). */
    bool ownsItemViewLayout(const QStyleOptionViewItem* option, const QWidget* widget) const;

    /**
     * @brief The gutter QCommonStyle puts around each part of an item-view cell.
     *
     * It insets the text rect by this before drawing and charges it per part in the cell's
     * size hint, so both must be accounted for rather than suppressed — other styles in the
     * chain may own a cell's layout and still rely on the metric.
     */
    int itemViewTextGutter(const QStyleOption* option, const QWidget* widget) const;

    /** @brief Serves the SE_ItemViewItem* sub-elements from itemViewLayout(). */
    QRect itemViewSubElementRect(
        SubElement element,
        const QStyleOption* option,
        const QWidget* widget
    ) const;

    /** @brief Clears the token resolution cache; called from the ThemeReloadEvent handler. */
    void clearTokenCache();

    // Dynamic widget property names used to tag combo box internals.
    // Defined here so both FreeCADStyle.cpp and its helpers can share them.
    // clang-format off
    static constexpr const char* comboDropdownProperty         = "_fc_comboDropdown";
    static constexpr const char* comboContainerProperty        = "_fc_comboContainer";
    static constexpr const char* viewportMaskInstalledProperty = "_fc_viewportMask";
    static constexpr const char* transparencyProperty          = "_fc_transparent";
    static constexpr const char* transparencyOverrideProperty  = "transparent";
    // clang-format on

    /**
     * @brief Applies the token-driven dropdown metrics to a combo box's popup list.
     *
     * A combo box may name the component its popup resolves against by carrying a
     * "dropdownComponent" property; the name is given the usual override treatment, so the
     * dropdown can take a height of its own without moving every other dropdown. The property
     * is read once, when the combo box is polished.
     */
    void constrainComboDropdown(QComboBox* comboBox);

    /**
     * @brief Bounds a popup list, and the container holding it, to the resolved MaxHeight.
     *
     * With no MaxHeight the dropdown is left to Qt, which keeps it on screen and shows as many
     * rows as maxVisibleItems allows.
     */
    void applyComboDropdownMaxHeight(QListView* listView) const;

    void restoreComboDropdownDefaults(QComboBox* comboBox);
    static void hideScrollerButtons(QWidget* container);
    static void restoreScrollerButtons(QWidget* container);
    void correctComboPopupPlacement(QWidget* container);

    // ── eventFilter helpers ────────────────────────────────────────────────
    // Each handles one concern from eventFilter(); see implementations for details.

    /** Zeroes layout margins on GroupBox/TaskHeader/TaskGroup and applies token
     *  padding to QTextEdit / QPlainTextEdit via document margin. */
    void resetTaskPanelMargins(QObject* obj);

    /** Forces a QTabBar repaint on mouse-move/leave events so hover highlighting
     *  is always up to date. */
    void forceTabBarRepaint(QObject* obj, QEvent* event);

    /** Defers correctComboPopupPlacement() via QTimer::singleShot(0) so Qt
     *  finishes its own screen-edge clamping before we adjust the popup. */
    void scheduleComboPopupCorrection(QObject* obj);

    /** Resolves token padding and applies it to @p document's document margin. */
    void applyTextEditDocumentPadding(QWidget* widget, QTextDocument* document) const;

    /**
     * @brief The transparency @p widget presents to its children.
     *
     * Answers a different question from isTransparent(): a widget may itself be painted over
     * the 3D view and still put an opaque surface under its children.
     */
    bool transparencyBelow(const QWidget* widget) const;

    /**
     * @brief Applies @p widget's transparency to a context that was not built by contextOf().
     *
     * Hand-built contexts must call this or they resolve the opaque tokens regardless of where
     * the widget is painted.
     */
    static void applyTransparency(StyleParameters::StyleContext& context, const QWidget* widget);

    /**
     * @brief Tags @p widget itself with @p surface and notifies it, without touching children.
     *
     * The single implementation of the tag write, shared by updateTransparency() and polish().
     */
    void tagWidgetTransparency(QWidget* widget, bool surface) const;

    /**
     * @brief The transparency @p widget itself renders with, given @p inherited as the fallback.
     *
     * An explicit "transparent" property always wins over @p inherited — even for a widget that
     * is itself a window, which is how such a widget can still declare itself a root. The single
     * implementation of a widget's own seed, shared by updateTransparency() and polish() so the
     * check cannot be present in one and silently missing from the other.
     */
    static bool ownSurface(const QWidget* widget, bool inherited);

    /**
     * @brief Whether @p widget may inherit transparency through its QObject parent/child link.
     *
     * A popup, menu, tooltip or dialog is a separate top-level surface over the desktop, not
     * over the 3D view, even when constructed as a child of a transparent widget purely for
     * lifetime management. False here does not block an explicit "transparent" property, which
     * ownSurface() still honours regardless. Shared by updateTransparency()'s recursion and
     * polish()'s parent lookup, so the two cannot disagree on the rule.
     */
    static bool canInheritTransparency(const QWidget* widget);

    /**
     * @brief Resolves the icon color for @p context.
     *
     * Tries IconColor token, then TextColor token, then falls back to palette.buttonText().
     */
    QColor resolveIconColor(const StyleParameters::StyleContext& context, const QPalette& palette) const;

    /**
     * @brief Renders @p icon via IconManager with the resolved color.
     *
     * Combines resolveIconColor() with IconManager::render() so each draw site
     * needs one call instead of an inline color-resolve + render block.
     */
    QPixmap renderStyledIcon(
        QPainter* painter,
        const QIcon& icon,
        const QSize& maxSize,
        QIcon::Mode mode,
        QIcon::State state,
        const StyleParameters::StyleContext& context,
        const QPalette& palette
    ) const;

    /**
     * @brief Convenience overload — derives mode, state, and palette from @p option.
     */
    QPixmap renderStyledIcon(
        QPainter* painter,
        const QIcon& icon,
        const QSize& maxSize,
        const QStyleOption* option,
        const StyleParameters::StyleContext& context
    ) const;

    /**
     * @brief Returns Qt::TextShowMnemonic, optionally OR'd with Qt::TextHideMnemonic.
     *
     * Queries SH_UnderlineShortcut so all draw methods respect the same style hint
     * without duplicating the three-line pattern.
     */
    int mnemonicTextFlags(const QStyleOption* option, const QWidget* widget) const;

    /**
     * @brief Shifts @p rect by PM_ButtonShift{Horizontal,Vertical} when sunken/checked.
     *
     * Returns @p rect unchanged when the option state has neither State_Sunken nor
     * State_On set.
     */
    QRect applyButtonShift(const QRect& rect, const QStyleOption* option, const QWidget* widget) const;

    // ── Cache helpers ─────────────────────────────────────────────────────────
    //
    // StyleContextCache<T> wraps an unordered_map<uint64_t, T> so all three caches
    // (token, box-style, box-geometry) share the same find/store/clear API.
    // All operations are const-qualified so they can be used from const draw methods.
    template<typename T>
    class StyleContextCache
    {
        mutable std::unordered_map<uint64_t, T> entries;

    public:
        const T* find(uint64_t key) const
        {
            const auto found = entries.find(key);
            return found != entries.end() ? &found->second : nullptr;
        }

        void store(uint64_t key, T value) const
        {
            entries.emplace(key, std::move(value));
        }

        void clear()
        {
            entries.clear();
        }
    };

    // tokenCache: key is a bit-packed uint64_t; value includes nullopt for confirmed misses.
    // Mutable so const draw methods can populate the cache.
    mutable StyleContextCache<std::optional<StyleParameters::Value>> tokenCache;

    // Aggregate caches for resolveBoxStyle / resolveBoxGeometry.
    // Keyed by context-only uint64_t (property bits left zero).
    mutable StyleContextCache<BoxStyleDefinition> boxStyleCache;
    mutable StyleContextCache<BoxGeometryDefinition> boxGeometryCache;
};

}  // namespace Gui
