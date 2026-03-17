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
#include <QMarginsF>
#include <QPainter>
#include <QProxyStyle>
#include <QComboBox>
#include <QPushButton>
#include <QStyleOption>
#include <QTabBar>
#include <QToolButton>
#include "StyleParameters/Value.h"
#include "StyleToken.h"

namespace Gui
{

class GuiExport FreeCADStyle: public QProxyStyle
{
    Q_OBJECT

public:
    FreeCADStyle();
    ~FreeCADStyle() override;

    /**
     * @brief Per-corner border radii in pixels.
     */
    struct CornerRadii
    {
        qreal topLeft = 0;
        qreal topRight = 0;
        qreal bottomRight = 0;
        qreal bottomLeft = 0;

        void setLeft(qreal left)
        {
            topLeft = left;
            bottomLeft = left;
        }

        void setRight(qreal right)
        {
            topRight = right;
            bottomRight = right;
        }

        void setTop(qreal top)
        {
            topLeft = top;
            topRight = top;
        }

        void setBottom(qreal bottom)
        {
            bottomLeft = bottom;
            bottomRight = bottom;
        }
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
     * @brief Describes the visual appearance of a painted background box.
     *
     * All border fields must be set together (borderColor + borderThickness)
     * for a border to be drawn; partial specification is silently ignored.
     */
    struct BoxStyleDefinition
    {
        QBrush background;
        std::optional<QColor> borderColor;
        std::optional<QMarginsF> borderThickness;
        CornerRadii borderRadius;  // default: all zero (sharp corners)
        std::optional<QColor> overlay;
        std::optional<QColor> borderOverlay;
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
            contentSize.rwidth() += paddingH();
            contentSize.rheight() += paddingV();
            return constrain(contentSize);
        }

        /** @brief Returns @p rect inset by this geometry's padding. */
        [[nodiscard]] QRect contentRect(const QRect& rect) const
        {
            return rect.marginsRemoved(padding.toMargins());
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
    };

    void polish(QPalette& palette) override;

    int pixelMetric(
        PixelMetric metric,
        const QStyleOption* option = nullptr,
        const QWidget* widget = nullptr
    ) const override;

    int styleHint(
        StyleHint hint,
        const QStyleOption* option,
        const QWidget* widget,
        QStyleHintReturn* returnData
    ) const override;

    void polish(QWidget* widget) override;
    void unpolish(QWidget* widget) override;

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

protected:
    /**
     * @brief Paints a background box with optional rounded corners and border.
     *
     * If borderColor + borderThickness are both set:
     *   1. Fill the outer rounded rect (borderRadius) with borderColor.
     *   2. Fill the inner rounded rect (inset by borderThickness, inner radii
     *      shrunk by thickness) with background.
     * Otherwise just fill the outer rounded rect with background.
     *
     * The painter state (pen, brush, render hints) is saved and restored.
     */
    static void drawBoxBackground(QPainter* painter, const QRect& rect, const BoxStyleDefinition& style);

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
        const StyleContext& context,
        StyleProperty property
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
    std::optional<T> resolve(const StyleContext& context, StyleProperty property) const
    {
        return StyleParameters::valueAs<T>(resolve(context, property));
    }

    /**
     * @brief Resolves a BoxStyleDefinition from a @p context using the token cache.
     *
     * Calls resolve(context, property) for each visual property, so all
     * per-property lookups are individually cached.
     */
    BoxStyleDefinition resolveBoxStyle(const StyleContext& context) const;

    /**
     * @brief Resolves a BoxGeometryDefinition from a @p context using the token cache.
     *
     * Calls resolve(context, property) for:
     *   Padding, Height, MinWidth, Width, MaxWidth, MinHeight, MaxHeight, IconSpacing.
     * All per-property lookups are individually cached.
     */
    BoxGeometryDefinition resolveBoxGeometry(const StyleContext& context) const;

    /**
     * @brief Builds a StyleContext from a widget and its current style option.
     *
     * Derives component from the widget type, variant slots from widget properties
     * (controlSize, isDefault, isFlat, autoRaise, property("flat")), and state
     * from option->state flags. Passing @p option as nullptr yields Normal state.
     */
    static StyleContext contextOf(
        const QWidget* widget,
        const QStyleOption* option = nullptr,
        const StyleComponentElement& element = StyleComponentElement::Root
    );

    /** @brief Maps a QTabBar::Shape to the canonical Position enum. */
    static Position tabPositionOf(QTabBar::Shape shape);

    /**
     * @brief Adjusts the trailing edge of a tab rect by the tab overlap amount.
     *
     * Both drawTabBarTab and drawTabBarTabLabel compute this identically: extend
     * (positive overlap) or shrink (negative overlap) the trailing edge so the
     * background paint rect and content geometry agree.
     */
    static QRect tabVisualRect(const QRect& rect, int tabOverlap, bool isVertical);

    /**
     * @brief Paints the background shape of a single tab.
     *
     * Handles geometric rotation (BorderRadius, BorderThickness) from the canonical
     * North definition to the actual tab position, while resolving visual tokens
     * (Background, TextColor, Overlay) with the full position context so per-position
     * colour overrides (e.g. TabBarTabSouthBackground) work naturally.
     * Called from drawControl when element == CE_TabBarTabShape.
     */
    void drawTabBarTab(QPainter* painter, const QStyleOptionTab* option, const QWidget* widget) const;

    /**
     * @brief Paints the label (icon + text) of a tab bar tab.
     *
     * Overrides CE_TabBarTabLabel to apply the TabBarTabIconSpacing token as the
     * icon-to-text gap (Qt Fusion hardcodes 4 px). Only active when both icon and
     * text are present and the tab is horizontal (North/South); vertical (East/West)
     * tabs delegate to the parent.
     */
    void drawTabBarTabLabel(QPainter* painter, const QStyleOptionTab* option, const QWidget* widget) const;

    /**
     * @brief Resolves a BoxStyleDefinition for the tab bar base strip from a position context.
     *
     * The background and border thickness are resolved from the canonical North position
     * and rotated to the actual position (via rotated()), while all other visual tokens
     * (BorderColor, Overlay, etc.) are resolved directly from the position context so that
     * per-position colour overrides work naturally.
     */
    BoxStyleDefinition resolveBaseStripStyle(const StyleContext& positionContext) const;

    /**
     * @brief Paints the decorative base strip between the tab bar and the page content area.
     *
     * Renders a 4px-tall strip at the attachment edge using a transparent→shadow gradient
     * and a 1px border on the attachment edge. Geometric tokens are resolved from the
     * canonical North position and rotated to the actual position; visual tokens are
     * resolved with the full position context so per-position overrides work naturally.
     * Called from drawPrimitive when element == PE_FrameTabBarBase.
     */
    void drawTabBarBase(
        QPainter* painter,
        const QStyleOptionTabBarBase* option,
        const QWidget* widget
    ) const;

    /**
     * @brief Draws the shadow strip at the tab attachment edge of a QTabWidget content frame.
     *
     * QTabWidget sets drawBase(false) on its internal QTabBar, suppressing PE_FrameTabBarBase.
     * This method replicates the same shadow strip by clipping the frame rect to the attachment
     * edge and drawing via resolveBaseStripStyle(). Called before falling through to the parent
     * style which draws the frame border.
     */
    void drawTabWidgetFrame(QPainter* painter, const QStyleOptionTabWidgetFrame* option) const;

    /**
     * @brief Draws the filled dot inside a checked radio button indicator.
     *
     * Resolves padding from the StyleProperty::Padding token; falls back to 20 % of
     * the rect width. Caller is responsible for having already painted the box background.
     */
    void drawRadioButtonDot(
        QPainter* painter,
        const QRect& rect,
        const StyleContext& context,
        const QPalette& palette
    ) const;

    /**
     * @brief Draws the check mark stroke inside a fully-checked checkbox indicator.
     *
     * Padding and pen width are resolved from design tokens. Caller is responsible for
     * having already painted the box background.
     */
    void drawCheckMark(
        QPainter* painter,
        const QRect& rect,
        const StyleContext& context,
        const QPalette& palette
    ) const;

    /**
     * @brief Draws the horizontal dash for an indeterminate checkbox indicator.
     *
     * Padding and pen width are resolved from design tokens. Caller is responsible for
     * having already painted the box background.
     */
    void drawIndeterminateMark(
        QPainter* painter,
        const QRect& rect,
        const StyleContext& context,
        const QPalette& palette
    ) const;

    /**
     * @brief Paints the label (icon + text) of a push button.
     *
     * Overrides CE_PushButtonLabel to apply the ButtonIconSpacing token as the
     * icon-to-text gap (Qt Fusion hardcodes 4 px). Only active when both icon and
     * text are present; icon-only or text-only buttons delegate to the parent.
     */
    void drawPushButtonLabel(
        QPainter* painter,
        const QStyleOptionButton* option,
        const QWidget* widget
    ) const;

    /**
     * @brief Paints the label (icon + text) of a tool button.
     *
     * Handles padding inset, icon/arrow pixmap selection, pressed-state shift,
     * and the two layout modes: TextBesideIcon and TextUnderIcon.
     * Called from drawControl when element == CE_ToolButtonLabel.
     */
    void drawToolButtonLabel(
        QPainter* painter,
        const QStyleOptionToolButton* option,
        const QWidget* widget
    ) const;

    /**
     * @brief Paints the label (icon + text) of a non-editable combo box.
     *
     * Overrides CE_ComboBoxLabel to apply the SelectIconSpacing token as the
     * icon-to-text gap (Qt hardcodes 4 px). Editable combos are delegated to
     * the parent because their icon–QLineEdit gap is fixed inside QComboBox
     * internals and cannot be overridden from a style.
     */
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
    void drawComponent(QPainter* painter, const QRect& rect, const StyleContext& context) const;

    /** @brief Convenience overload that builds the context from @p widget and @p option. */
    void drawComponent(
        QPainter* painter,
        const QRect& rect,
        const QWidget* widget,
        const QStyleOption* option = nullptr
    ) const;

    /**
     * @brief Draws a separator line (horizontal or vertical) using design tokens.
     *
     * Resolves SeparatorThickness and SeparatorColor tokens, then fills the
     * appropriate sub-rect of @p rect.
     */
    void drawSeparatorLine(QPainter* painter, const QRect& rect, bool isHorizontal) const;

    /** @brief Clears the token resolution cache; called from the ThemeReloadEvent handler. */
    void clearTokenCache();

    /**
     * @brief Resolves the icon color for @p context.
     *
     * Tries IconColor token, then TextColor token, then falls back to palette.buttonText().
     */
    QColor resolveIconColor(const StyleContext& context, const QPalette& palette) const;

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
        const StyleContext& context,
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
        const StyleContext& context
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

    // Cache for resolve(StyleContext, StyleProperty). Key is a bit-packed uint32_t;
    // value is the resolved result including nullopt for confirmed misses.
    // Mutable so const draw methods can populate the cache.
    mutable std::unordered_map<uint32_t, std::optional<StyleParameters::Value>> tokenCache;

    /**
     * @brief Interns a component override string and returns its stable uint8_t id.
     *
     * Id 0 is reserved for "no override". Each unique string is assigned a new id
     * starting from 1 on first encounter. The id is used in packCacheKey() so that
     * the override string participates in cache-key discrimination without requiring
     * a string hash in the hot path.
     *
     * The intern table is cleared together with tokenCache in clearTokenCache().
     */
    uint8_t internComponentOverride(const std::string& name) const;

    mutable std::unordered_map<std::string, uint8_t> componentOverrideIds;
    mutable uint8_t nextComponentOverrideId = 1;
};

}  // namespace Gui
