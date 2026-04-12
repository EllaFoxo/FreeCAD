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
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <FCGlobal.h>
#include <Base/Color.h>
#include <QBrush>
#include <QColor>
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

class QTextDocument;

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
     * @brief Paints the full-width row background for an item view row.
     *
     * Expands the rect to the full viewport width so the branch and indent areas of
     * QTreeView receive the same background as the cell columns. The painter clip is
     * temporarily replaced with Qt::ReplaceClip to escape the per-cell clip that
     * CE_ItemViewItem installs before calling PE_PanelItemViewItem.
     *
     * Called from PE_PanelItemViewItem for the first cell of each row. The context for
     * the Row element (including the RowType::Alternate variant when applicable) is
     * built inside this function from the provided option and widget.
     */
    void drawItemViewRow(QPainter* painter, const QStyleOptionViewItem* vopt, const QWidget* widget) const;

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

    /**
     * @brief Resolves a BoxStyleDefinition from a @p context using the token cache.
     */
    BoxStyleDefinition resolveBoxStyle(const StyleParameters::StyleContext& context) const;

private:
    static StyleParameters::StyleContext withNorthPosition(
        const StyleParameters::StyleContext& context
    );

    static StyleParameters::Position tabPositionOf(QTabBar::Shape shape);
    static StyleParameters::Position toolbarPositionOf(const QToolBar* toolbar);
    int tabOverlapOf(const QStyleOptionTab* option, const QWidget* widget) const;


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

    void drawTabWidgetFrame(QPainter* painter, const QStyleOptionTabWidgetFrame* option) const;

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

    /** @brief Clears the token resolution cache; called from the ThemeReloadEvent handler. */
    void clearTokenCache();

    /** @brief Returns the cache bin for a widget */
    static const QWidget* cacheBinFor(const QWidget* widget);

    // Dynamic widget property names used to tag combo box internals.
    // Defined here so both FreeCADStyle.cpp and its helpers can share them.
    // clang-format off
    static constexpr const char* comboDropdownProperty         = "_fc_comboDropdown";
    static constexpr const char* comboContainerProperty        = "_fc_comboContainer";
    static constexpr const char* viewportMaskInstalledProperty = "_fc_viewportMask";
    // clang-format on

    void constrainComboDropdown(QComboBox* comboBox);
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
        using ContextCache = std::unordered_map<uint64_t, T>;

        mutable std::unordered_map<const QWidget*, ContextCache> bins;

    public:
        const T* find(const QWidget* bin, uint64_t key) const
        {
            const auto binIt = bins.find(bin);
            if (binIt == bins.end()) {
                return nullptr;
            }
            const auto found = binIt->second.find(key);
            return found != binIt->second.end() ? &found->second : nullptr;
        }

        void store(const QWidget* bin, uint64_t key, T value) const
        {
            bins[bin].emplace(key, std::move(value));
        }

        void clear(const QWidget* bin)
        {
            bins.erase(bin);
        }

        void clear()
        {
            bins.clear();
        }
    };

    // Mutable so const draw methods can populate the cache.
    mutable StyleContextCache<std::optional<StyleParameters::Value>> tokenCache;

    // Aggregate caches for resolveBoxStyle / resolveBoxGeometry.
    mutable StyleContextCache<BoxStyleDefinition> boxStyleCache;
    mutable StyleContextCache<BoxGeometryDefinition> boxGeometryCache;
};

}  // namespace Gui
