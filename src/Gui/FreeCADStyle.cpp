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
 *   FreeCAD is distributed in the hope that it will be us
 *   eful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include "FreeCADStyle.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <QApplication>
#include <QCoreApplication>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QFrame>
#include <QGroupBox>
#include <QImage>
#include <QLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QTextEdit>
#include <QPainterPath>
#include <QPen>
#include <QStyleOption>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QHeaderView>
#include <QRadioButton>
#include <QListView>
#include <QStyleOptionGroupBox>
#include <QStyleOptionHeader>
#include <QStyleOptionViewItem>
#include <QTreeView>
#include <QPointer>
#include <QScrollBar>
#include <QScreen>
#include <QCursor>
#include <QMenuBar>
#include <QMainWindow>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>

#include <Base/Color.h>
#include <Base/Converter.h>

#include "Application.h"
#include "QuantitySpinBox_p.h"
#include "ThemeReloadEvent.h"
#include "Utilities.h"
#include "IconManager.h"

#include "QSint/actionpanel/taskgroup_p.h"
#include "QSint/actionpanel/taskheader_p.h"

#include "StyleParameters/ColorEffect.h"
#include "StyleParameters/Corners.h"
#include "StyleParameters/InnerShadow.h"
#include "StyleParameters/Insets.h"
#include "StyleParameters/ParameterManager.h"
#include "StyleParameters/StyleOverrides.h"
#include "TaskView/TaskView.h"


QT_BEGIN_NAMESPACE
extern Q_WIDGETS_EXPORT void qt_blurImage(
    QPainter* painter,
    QImage& blurImage,
    qreal radius,
    bool quality,
    bool alphaOnly,
    int transposed = 0
);
QT_END_NAMESPACE

using namespace Gui;
using namespace Gui::StyleParameters;

// ─── FreeCADStyle constructor / destructor ────────────────────────────────

FreeCADStyle::FreeCADStyle()
    : QProxyStyle(QStringLiteral("Fusion"))
{
    IconManager::instance().setIconColorProvider(
        [](const IconManager::IconMeta&, QIcon::Mode, QIcon::State) -> QColor {
            return qApp->palette().buttonText().color();
        }
    );
}

FreeCADStyle::~FreeCADStyle()
{
    IconManager::instance().setIconColorProvider({});
}

// ─── Base::convertTo specializations ──────────────────────────────────────
// Conversions for FreeCADStyle-specific types (CornerRadii, InnerShadow) that
// cannot live in Utilities.h because they depend on types declared in this
// header. General-purpose StyleParameters↔Qt conversions (QMarginsF, QBrush,
// QLinearGradient, QRadialGradient) are defined in Utilities.h.

namespace Base
{

template<>
FreeCADStyle::CornerRadii convertTo<FreeCADStyle::CornerRadii, Corners>(const Corners& corners)
{
    return {
        .topLeft = corners.topLeft(),
        .topRight = corners.topRight(),
        .bottomRight = corners.bottomRight(),
        .bottomLeft = corners.bottomLeft(),
    };
}

template<>
FreeCADStyle::InnerShadow convertTo<FreeCADStyle::InnerShadow, InnerShadow>(const InnerShadow& shadow)
{
    return {
        .color = shadow.color().asValue<QColor>(),
        .x = shadow.x(),
        .y = shadow.y(),
        .blur = shadow.blur(),
    };
}

template<>
FreeCADStyle::BorderColorsPerSide convertTo<FreeCADStyle::BorderColorsPerSide, BorderColors>(
    const BorderColors& borderColors
)
{
    return {
        .top = borderColors.top().asValue<QColor>(),
        .right = borderColors.right().asValue<QColor>(),
        .bottom = borderColors.bottom().asValue<QColor>(),
        .left = borderColors.left().asValue<QColor>(),
    };
}

}  // namespace Base

FreeCADStyle::CornerRadii FreeCADStyle::CornerRadii::resolve(QSizeF size) const
{
    const qreal minDimension = std::min(size.width(), size.height());
    auto resolveOne =
        [minDimension](const StyleParameters::Numeric& radius) -> StyleParameters::Numeric {
        if (radius.unit == "%") {
            return {.value = radius.value / 100.0 * minDimension, .unit = "px"};
        }
        return radius;
    };
    return {
        .topLeft = resolveOne(topLeft),
        .topRight = resolveOne(topRight),
        .bottomRight = resolveOne(bottomRight),
        .bottomLeft = resolveOne(bottomLeft),
    };
}

namespace
{

// Arc start angles (in degrees) for each corner of a clockwise rounded rectangle.
constexpr qreal arcStartTopRight = 90;
constexpr qreal arcStartBottomRight = 0;
constexpr qreal arcStartBottomLeft = 270;
constexpr qreal arcStartTopLeft = 180;
constexpr qreal arcSweepClockwise = -90;

QPainterPath roundedRectPath(const QRectF& rect, const FreeCADStyle::CornerRadii& radii)
{
    // Resolve percent radii then clamp to at most half the shorter side.
    const FreeCADStyle::CornerRadii resolved = radii.resolve(rect.size());
    const qreal maxRadius = std::min(rect.width(), rect.height()) / 2.0;
    const qreal topLeft = std::min(resolved.topLeft.value, maxRadius);
    const qreal topRight = std::min(resolved.topRight.value, maxRadius);
    const qreal bottomRight = std::min(resolved.bottomRight.value, maxRadius);
    const qreal bottomLeft = std::min(resolved.bottomLeft.value, maxRadius);

    QPainterPath path;
    path.moveTo(rect.left() + topLeft, rect.top());
    path.lineTo(rect.right() - topRight, rect.top());
    path.arcTo(
        rect.right() - (2 * topRight),
        rect.top(),
        2 * topRight,
        2 * topRight,
        arcStartTopRight,
        arcSweepClockwise
    );
    path.lineTo(rect.right(), rect.bottom() - bottomRight);
    path.arcTo(
        rect.right() - (2 * bottomRight),
        rect.bottom() - (2 * bottomRight),
        2 * bottomRight,
        2 * bottomRight,
        arcStartBottomRight,
        arcSweepClockwise
    );
    path.lineTo(rect.left() + bottomLeft, rect.bottom());
    path.arcTo(
        rect.left(),
        rect.bottom() - (2 * bottomLeft),
        2 * bottomLeft,
        2 * bottomLeft,
        arcStartBottomLeft,
        arcSweepClockwise
    );
    path.lineTo(rect.left(), rect.top() + topLeft);
    path.arcTo(rect.left(), rect.top(), 2 * topLeft, 2 * topLeft, arcStartTopLeft, arcSweepClockwise);
    path.closeSubpath();
    return path;
}

// Computes inner corner radii after subtracting border thickness.
// Expects outer to already be resolved to absolute pixels (no "%" unit).
FreeCADStyle::CornerRadii innerRadii(const FreeCADStyle::CornerRadii& outer, const QMarginsF& thickness)
{
    auto shrink = [](qreal radius, qreal a, qreal b) -> qreal {
        return std::max(0.0, radius - std::max(a, b));
    };
    return {
        .topLeft
        = {.value = shrink(outer.topLeft.value, thickness.top(), thickness.left()), .unit = "px"},
        .topRight
        = {.value = shrink(outer.topRight.value, thickness.top(), thickness.right()), .unit = "px"},
        .bottomRight
        = {.value = shrink(outer.bottomRight.value, thickness.bottom(), thickness.right()),
           .unit = "px"},
        .bottomLeft
        = {.value = shrink(outer.bottomLeft.value, thickness.bottom(), thickness.left()),
           .unit = "px"},
    };
}

struct ShadowCacheKey
{
    int width, height;
    qreal x, y, blur;
    QRgb color;
    qreal radiusTopLeft, radiusTopRight, radiusBottomRight, radiusBottomLeft;

    auto operator<=>(const ShadowCacheKey&) const = default;
};

QImage buildShadowImage(
    const QRect& rect,
    const FreeCADStyle::CornerRadii& radii,
    const FreeCADStyle::InnerShadow& shadow
)
{
    const int padding = static_cast<int>(std::ceil(shadow.blur)) + 1;
    const QSize imageSize = rect.size() + QSize(2 * padding, 2 * padding);

    // Create a fully opaque black image and punch a transparent hole in the shape.
    // The opaque ring that remains around the hole produces the shadow after blurring.
    QImage mask(imageSize, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::black);

    {
        QPainter maskPainter(&mask);
        maskPainter.setRenderHint(QPainter::Antialiasing);
        maskPainter.setCompositionMode(QPainter::CompositionMode_Clear);
        maskPainter.fillPath(
            roundedRectPath(QRectF(padding, padding, rect.width(), rect.height()), radii),
            Qt::transparent
        );
    }

    QImage blurred(imageSize, QImage::Format_ARGB32_Premultiplied);
    blurred.fill(Qt::transparent);
    {
        QPainter blurPainter(&blurred);
        qt_blurImage(&blurPainter, mask, shadow.blur, false, false);
    }

    // Tint the blurred image with the shadow color.
    {
        QPainter tintPainter(&blurred);
        tintPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tintPainter.fillRect(blurred.rect(), shadow.color);
    }

    return blurred;
}

const QImage& getCachedShadowImage(
    const QRect& rect,
    const FreeCADStyle::CornerRadii& radii,
    const FreeCADStyle::InnerShadow& shadow
)
{
    constexpr int maxCacheEntries = 32;
    static std::map<ShadowCacheKey, QImage> cache;

    const ShadowCacheKey key {
        .width = rect.width(),
        .height = rect.height(),
        .x = shadow.x,
        .y = shadow.y,
        .blur = shadow.blur,
        .color = shadow.color.rgba(),
        .radiusTopLeft = radii.topLeft.value,
        .radiusTopRight = radii.topRight.value,
        .radiusBottomRight = radii.bottomRight.value,
        .radiusBottomLeft = radii.bottomLeft.value,
    };

    if (auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }
    if (static_cast<int>(cache.size()) >= maxCacheEntries) {
        cache.erase(cache.begin());
    }
    return cache.emplace(key, buildShadowImage(rect, radii, shadow)).first->second;
}


/**
 * @brief Returns the orientation of the nearest ancestor QToolBar, or nullopt if the widget is
 *        not inside a toolbar.
 */
std::optional<Qt::Orientation> toolbarOrientationOf(const QWidget* widget)
{
    const QWidget* ancestor = widget ? widget->parentWidget() : nullptr;

    if (const auto* toolbar = qobject_cast<const QToolBar*>(ancestor)) {
        return toolbar->orientation();
    }

    return std::nullopt;
}


// ─── Icon helpers ────────────────────────────────────────────────────────────

// QIcon::Mode from option state — full check including AutoRaise → Active.
QIcon::Mode iconModeOf(const QStyleOption* option)
{
    if (!(option->state & QStyle::State_Enabled)) {
        return QIcon::Disabled;
    }
    if ((option->state & QStyle::State_MouseOver) && (option->state & QStyle::State_AutoRaise)) {
        return QIcon::Active;
    }
    return QIcon::Normal;
}

// QIcon::State from option state — State_On → On, else Off.
QIcon::State iconStateOf(const QStyleOption* option)
{
    return (option->state & QStyle::State_On) ? QIcon::On : QIcon::Off;
}

// The view an item-view style option belongs to; widget can be the viewport.
const QWidget* itemViewOf(const QStyleOptionViewItem* option, const QWidget* widget)
{
    return option && option->widget ? option->widget : widget;
}

// How far a branch connector stops short of the centre when an expand arrow sits there.
// Sized against the chevron drawChevronArrow paints, so the two cannot drift apart.
constexpr qreal arrowClearance = 5.0;

// Odd, so a box centred on a half-pixel point keeps that point as its own centre.
constexpr int arrowBoxSize = 13;

// Matches the chevron elsewhere in the style, which is drawn slightly softer than body text.
constexpr int arrowAlpha = 160;

// Whether this row is the topmost one on screen, and so carries no gap above it.
bool isFirstVisibleRow(const QStyleOption* option, const QWidget* widget)
{
    const auto* viewOption = qstyleoption_cast<const QStyleOptionViewItem*>(option);
    if (viewOption == nullptr || !viewOption->index.isValid()) {
        return false;
    }

    if (const auto* tree = qobject_cast<const QTreeView*>(itemViewOf(viewOption, widget))) {
        return !tree->indexAbove(viewOption->index).isValid();
    }

    return viewOption->index.row() == 0 && !viewOption->index.parent().isValid();
}

// How many of the three cell parts — check indicator, icon, text — this cell actually has.
int itemViewPartCount(const QStyleOptionViewItem& option)
{
    return ((option.features & QStyleOptionViewItem::HasCheckIndicator) ? 1 : 0)
        + ((option.features & QStyleOptionViewItem::HasDecoration) ? 1 : 0)
        + ((option.features & QStyleOptionViewItem::HasDisplay) ? 1 : 0);
}

}  // namespace

// Fills each side of a border ring with its own color using CSS-style diagonal corner splits.
// Four non-overlapping trapezoids partition the border ring; corners are split diagonally,
// matching CSS border-color behaviour.
static void drawBorderRingSided(
    QPainter* painter,
    const QRect& rect,
    const QPainterPath& borderRingPath,
    const QMarginsF& thickness,
    const FreeCADStyle::BorderColorsPerSide& colors
)
{
    const auto fillSide = [&](const QPolygonF& trapezoid, QColor color) {
        QPainterPath clip;
        clip.addPolygon(trapezoid);
        clip.closeSubpath();
        painter->fillPath(borderRingPath.intersected(clip), QBrush(color));
    };

    // Use QRectF to match the exclusive right/bottom used in borderRingPath (built from
    // QRectF(rect)). QRect::right() = left() + width() - 1 (inclusive), QRectF::right() = left() +
    // width() (exclusive).
    const QRectF rectF(rect);
    const qreal rectLeft = rectF.left();
    const qreal rectRight = rectF.right();
    const qreal rectTop = rectF.top();
    const qreal rectBottom = rectF.bottom();

    const qreal innerLeft = rectLeft + thickness.left();
    const qreal innerRight = rectRight - thickness.right();
    const qreal innerTop = rectTop + thickness.top();
    const qreal innerBottom = rectBottom - thickness.bottom();

    // Fill the entire ring with the top color first.
    // This acts as a base so that anti-aliased pixels at the diagonal corner boundaries blend
    // between the two adjacent side colors rather than exposing the painter background.
    // Without this, intersected() produces semi-transparent edges on both trapezoids at the
    // shared diagonal, leaving a gap where the background shows through.
    painter->fillPath(borderRingPath, QBrush(colors.top));

    // Overwrite right, bottom, left with their own colors.
    // Anti-aliasing at each diagonal blends the overdraw color into the top base — no gap.
    // clang-format off
    fillSide({{rectRight, rectTop},    {rectRight,  rectBottom}, {innerRight, innerBottom}, {innerRight, innerTop}},    colors.right);
    fillSide({{rectRight, rectBottom}, {rectLeft,   rectBottom}, {innerLeft,  innerBottom}, {innerRight, innerBottom}}, colors.bottom);
    fillSide({{rectLeft,  rectBottom}, {rectLeft,   rectTop},   {innerLeft,  innerTop},    {innerLeft,  innerBottom}},  colors.left);
    // clang-format on
}

void FreeCADStyle::drawBoxBackground(
    QPainter* painter,
    const QRect& rect,
    const BoxStyleDefinition& rule,
    const QPainterPath& borderMask
)
{
    const bool hasBorder = rule.borderColor.has_value() && rule.borderThickness.has_value();
    const bool hasBackground = rule.background.style() != Qt::NoBrush;
    const bool hasInnerShadow = rule.innerShadow.has_value();

    if (!hasBackground && !hasBorder && !hasInnerShadow) {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setClipRect(rect, Qt::IntersectClip);

    // Resolve percent radii once against the rect size; all downstream helpers expect absolute px.
    const CornerRadii resolvedBorderRadius = rule.borderRadius.resolve(rect.size());
    const QRect backgroundRect = rect;

    if (hasBackground) {
        QRectF backgroundInnerRect = backgroundRect;

        // In case of border being applied, shrink the background to half of the border thickness
        // so it does not bleed out to outer background.
        if (rule.borderThickness) {
            backgroundInnerRect = backgroundInnerRect.marginsRemoved(*rule.borderThickness / 2);
        }

        painter->fillPath(
            roundedRectPath(QRectF(backgroundInnerRect), resolvedBorderRadius),
            rule.background
        );
    }

    if (hasInnerShadow) {
        const int padding = static_cast<int>(std::ceil(rule.innerShadow->blur)) + 1;
        const QImage& shadowImage = getCachedShadowImage(rect, resolvedBorderRadius, *rule.innerShadow);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setClipPath(roundedRectPath(QRectF(rect), resolvedBorderRadius), Qt::IntersectClip);
        painter->drawImage(
            QPointF(
                rect.left() - padding + rule.innerShadow->x,
                rect.top() - padding + rule.innerShadow->y
            ),
            shadowImage
        );
        painter->restore();
    }

    if (hasBorder) {
        const QMarginsF& thickness = *rule.borderThickness;

        // Snap each border side to the nearest integer pixel.
        const QMarginsF snappedThickness(
            qRound(thickness.left()),
            qRound(thickness.top()),
            qRound(thickness.right()),
            qRound(thickness.bottom())
        );

        const QRect innerRect = backgroundRect.marginsRemoved(thickness.toMargins());

        // Subtract inner from outer path to fill only the border ring, preserving transparency.
        const QPainterPath outerPath = roundedRectPath(QRectF(rect), resolvedBorderRadius);
        const QPainterPath innerPath
            = roundedRectPath(QRectF(innerRect), innerRadii(resolvedBorderRadius, snappedThickness));
        const QPainterPath fullRing = outerPath.subtracted(innerPath);
        const QPainterPath borderRingPath = borderMask.isEmpty() ? fullRing
                                                                 : fullRing.intersected(borderMask);

        const BorderColorsPerSide& colors = *rule.borderColor;
        if (colors.isUniform()) {
            painter->fillPath(borderRingPath, QBrush(colors.uniform()));
        }
        else {
            drawBorderRingSided(painter, rect, borderRingPath, snappedThickness, colors);
        }
    }

    painter->restore();
}

void FreeCADStyle::polish(QPalette& palette)
{
    QProxyStyle::polish(palette);

    // Sets role in both Active and Inactive groups; leaves Disabled unchanged.
    const auto set = [&](QPalette::ColorRole role, std::string_view token) {
        if (const auto color = resolve<Base::Color>(token)) {
            palette.setColor(QPalette::Active, role, color->asValue<QColor>());
            palette.setColor(QPalette::Inactive, role, color->asValue<QColor>());
        }
    };

    // Sets role only in the Disabled group.
    const auto setDisabled = [&](QPalette::ColorRole role, std::string_view token) {
        if (const auto color = resolve<Base::Color>(token)) {
            palette.setColor(QPalette::Disabled, role, color->asValue<QColor>());
        }
    };

    // ── Active / Inactive ────────────────────────────────────────────────────

    // Window surfaces
    set(QPalette::Window, "BaseWindowBackground");
    set(QPalette::WindowText, "BaseTextColor");

    // Input / item-view surfaces
    set(QPalette::Base, "BaseInputBackground");
    set(QPalette::AlternateBase, "BaseAlternateBackground");
    set(QPalette::Text, "BaseTextColor");
    set(QPalette::PlaceholderText, "BasePlaceholderTextColor");

    // Buttons
    set(QPalette::Button, "BaseButtonBackground");
    set(QPalette::ButtonText, "ButtonTextColor");

    // Selection
    set(QPalette::Highlight, "BaseHighlightBackground");
    set(QPalette::HighlightedText, "BaseHighlightTextColor");
    set(QPalette::BrightText, "BaseHighlightTextColor");

    // Links
    set(QPalette::Link, "BaseLinkColor");
    set(QPalette::LinkVisited, "BaseLinkColor");  // themes can override if needed

    // Tooltips
    set(QPalette::ToolTipBase, "BaseTooltipBackground");
    set(QPalette::ToolTipText, "BaseTooltipTextColor");

    // 3D shading (used by non-custom widgets for borders and shadows)
    set(QPalette::Light, "BaseShadingLight");
    set(QPalette::Midlight, "BaseShadingMidlight");
    set(QPalette::Mid, "BaseShadingMid");
    set(QPalette::Dark, "BaseShadingDark");
    set(QPalette::Shadow, "BaseShadingShadow");

    // ── Disabled ─────────────────────────────────────────────────────────────

    setDisabled(QPalette::WindowText, "BaseDisabledTextColor");
    setDisabled(QPalette::Text, "BaseDisabledTextColor");
    setDisabled(QPalette::ButtonText, "BaseDisabledTextColor");
    setDisabled(QPalette::PlaceholderText, "BaseDisabledTextColor");
    setDisabled(QPalette::Base, "BaseWindowBackground");
    setDisabled(QPalette::Button, "BaseDisabledBackground");
    setDisabled(QPalette::Highlight, "BaseShadingMid");
    setDisabled(QPalette::HighlightedText, "BaseDisabledTextColor");
}

std::optional<int> FreeCADStyle::resolvePixelMetric(
    PixelMetric metric,
    const QStyleOption* option,
    const QWidget* widget
) const
{
    using enum StyleProperty;

    const auto element = [&widget, &option](StyleComponentElement element) {
        return contextOf(widget, option, element);
    };

    const StyleContext context = element(StyleComponentElement::Root);

    static const std::map<PixelMetric, std::pair<StyleComponentElement, StyleProperty>> metrics = {
        {PM_ExclusiveIndicatorWidth, {StyleComponentElement::Indicator, Width}},
        {PM_ExclusiveIndicatorHeight, {StyleComponentElement::Indicator, Height}},
        {PM_IndicatorWidth, {StyleComponentElement::Indicator, Width}},
        {PM_IndicatorHeight, {StyleComponentElement::Indicator, Height}},
        {PM_CheckBoxLabelSpacing, {StyleComponentElement::Indicator, Spacing}},
        {PM_RadioButtonLabelSpacing, {StyleComponentElement::Indicator, Spacing}},
        {PM_MenuButtonIndicator, {StyleComponentElement::Menu, Width}},
        {PM_ToolBarItemMargin, {StyleComponentElement::Item, Margin}},
        {PM_ToolBarFrameWidth, {StyleComponentElement::Root, FrameWidth}},
        {PM_ToolBarItemSpacing, {StyleComponentElement::Item, Spacing}},
        {PM_MenuBarItemSpacing, {StyleComponentElement::Item, Spacing}},
        {PM_TabCloseIndicatorWidth, {StyleComponentElement::CloseButton, Width}},
        {PM_TabCloseIndicatorHeight, {StyleComponentElement::CloseButton, Height}},
    };

    switch (metric) {
        // Reset FrameWidth applied to SpinBoxes on some platforms. Using the default can cause the
        // SpinBox to be inflated, which is not intended as the style handles all the necessary
        // size calculations.
        case PM_SpinBoxFrameWidth:
            if (qobject_cast<const QAbstractSpinBox*>(widget)) {
                return 0;
            }
            return {};

        case PM_DefaultFrameWidth: {
            if (qobject_cast<const QAbstractSpinBox*>(widget)) {
                return 0;
            }
            if (const auto itemViewFrameWidth = resolveItemViewFrameWidth(option, widget)) {
                return itemViewFrameWidth;
            }
            return {};
        }

        // PM_TabBarTabOverlap is a pure painting hint: it tells CE_TabBarTabShape how many pixels
        // to extend (positive) or shrink (negative) the trailing edge of each non-last tab's paint
        // rect. QTabBar's layoutTabs() does NOT query this metric; the visual spacing is achieved
        // entirely by adjusting the paint rect in drawTabBarTab.
        //
        // TabBarTabSpacing uses gap semantics (positive = gap, negative = overlap), so
        // overlap = -spacing. Default -1px → overlap 1 → 1px trailing extension hides shared border.
        case PM_TabBarTabOverlap:
            if (const auto spacing = resolve<int>(element(StyleComponentElement::Tab), Spacing)) {
                return -*spacing;
            }
            return {};

        // PM_TabBarTabHSpace / PM_TabBarTabVSpace feed into
        // QCommonStyle::sizeFromContents(CT_TabBarTab). Driving padding through these metrics
        // preserves Qt's close-button width, minimum-size constraints, and all other CT_TabBarTab
        // logic. North position is used because QTabBar::tabSizeHint() transposes the returned size
        // for East/West tabs itself. State is preserved (e.g. checked tabs use
        // TabBarTabCheckedPadding).
        case PM_TabBarTabHSpace:
        case PM_TabBarTabVSpace: {
            const StyleContext tabContext = withNorthPosition(element(StyleComponentElement::Tab));
            if (const auto padding = resolve<Insets>(tabContext, Padding)) {
                return static_cast<int>(
                    metric == PM_TabBarTabHSpace ? padding->horizontal() : padding->vertical()
                );
            }
            return {};
        }

        // PM_TabBarBaseHeight / PM_TabBarBaseOverlap are only meaningful for a standalone QTabBar
        // that actually draws its base strip (PE_FrameTabBarBase). QCommonStyle also queries
        // PM_TabBarBaseOverlap via SE_TabWidgetTabPane with widget = QTabWidget to compute the
        // pane inset — returning our large overlap there would push the frame into the tab row.
        // Guard on widget being a QTabBar so the QTabWidget pane calculation gets 0 (flush).
        case PM_TabBarBaseHeight:
        case PM_TabBarBaseOverlap:
            if (qobject_cast<const QTabBar*>(widget)) {
                const StyleContext baseContext = element(StyleComponentElement::Base);
                const auto height = resolve<int>(baseContext, Height);
                const auto overlap = resolve<int>(baseContext, Overlap);

                if (metric == PM_TabBarBaseHeight && height) {
                    return *height + overlap.value_or(0);
                }
                if (metric == PM_TabBarBaseOverlap && overlap) {
                    return overlap;
                }

                return {};
            }

            if (metric == PM_TabBarBaseOverlap) {
                return 1;
            }

            return {};

        default: {
            if (const auto it = metrics.find(metric); it != metrics.end()) {
                const auto& [component, property] = it->second;

                return resolve<int>(element(component), property);
            }

            return {};
        }
    }
}

std::optional<int> FreeCADStyle::resolveItemViewFrameWidth(
    const QStyleOption* option,
    const QWidget* widget
) const
{
    if (!qobject_cast<const QAbstractItemView*>(widget)) {
        return {};
    }

    const BoxGeometryDefinition geometry = resolveBoxGeometry(
        contextOf(widget, option, StyleComponentElement::Root)
    );
    // Symmetric only: a scalar frame width expresses one inset, so item-view
    // container padding uses the top inset for all four sides (see design doc).
    const int containerPadding = static_cast<int>(geometry.padding.top());
    if (containerPadding > 0) {
        return QProxyStyle::pixelMetric(PM_DefaultFrameWidth, option, widget) + containerPadding;
    }

    return {};
}

int FreeCADStyle::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const
{
    if (const auto value = resolvePixelMetric(metric, option, widget)) {
        return *value;
    }
    return QProxyStyle::pixelMetric(metric, option, widget);
}

int FreeCADStyle::styleHint(
    StyleHint hint,
    const QStyleOption* option,
    const QWidget* widget,
    QStyleHintReturn* returnData
) const
{
    if (hint == SH_DialogButtonBox_ButtonsHaveIcons) {
        return 0;
    }

    if (hint == SH_GroupBox_TextLabelVerticalAlignment) {
        // Fusion answers AlignTop, which places the frame's top edge below the whole title band.
        // Our group box puts the title *on* that edge so the border can be notched out from
        // under it, and this hint is the single value both the frame geometry and the label
        // placement are derived from. A vertical flag set explicitly on the option still wins.
        if (const auto* groupBoxOption = qstyleoption_cast<const QStyleOptionGroupBox*>(option)) {
            const int requested = (groupBoxOption->textAlignment & Qt::AlignVertical_Mask).toInt();
            if (requested != 0) {
                return requested;
            }
        }
        return Qt::AlignVCenter;
    }

    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

void FreeCADStyle::drawRadioButtonDot(
    QPainter* painter,
    const QRect& rect,
    const StyleContext& context,
    const QPalette& palette
) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);

    constexpr qreal dotPaddingRatio = 0.2;  // fallback: fraction of indicator width
    qreal padding = static_cast<qreal>(rect.width()) * dotPaddingRatio;
    if (const auto paddings = resolve<Insets>(context, StyleProperty::Padding)) {
        padding = paddings->left().value;
    }

    painter->setBrush(resolveIconColor(context, palette));
    painter->drawEllipse(QRectF(rect).adjusted(padding, padding, -padding, -padding));
    painter->restore();
}

void FreeCADStyle::drawCheckMark(
    QPainter* painter,
    const QRect& rect,
    const StyleContext& context,
    const QPalette& palette
) const
{
    constexpr qreal checkPaddingRatio = 0.2;    // fallback: fraction of box width
    constexpr qreal checkPenWidthRatio = 0.15;  // stroke width as fraction of inner rect width
    constexpr qreal checkMinPenWidth = 1.5;     // minimum stroke width in pixels

    qreal padding = static_cast<qreal>(rect.width()) * checkPaddingRatio;
    if (const auto paddings = resolve<Insets>(context, StyleProperty::Padding)) {
        padding = paddings->left().value;
    }

    const QRectF innerRect = QRectF(rect).adjusted(padding, padding, -padding, -padding);
    const qreal penWidth = qMax(checkMinPenWidth, innerRect.width() * checkPenWidthRatio);

    // Proportional anchor points for the check mark path (relative to inner rect).
    constexpr qreal checkMidY = 0.5;   // vertical mid-point of the left arm
    constexpr qreal checkKneeX = 0.4;  // horizontal position of the knee (valley)

    QPainterPath checkPath;
    checkPath.moveTo(innerRect.left(), innerRect.top() + (innerRect.height() * checkMidY));
    checkPath.lineTo(innerRect.left() + (innerRect.width() * checkKneeX), innerRect.bottom());
    checkPath.lineTo(innerRect.right(), innerRect.top());

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->strokePath(
        checkPath,
        QPen(resolveIconColor(context, palette), penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)
    );
    painter->restore();
}

void FreeCADStyle::drawIndeterminateMark(
    QPainter* painter,
    const QRect& rect,
    const StyleContext& context,
    const QPalette& palette
) const
{
    constexpr qreal checkPaddingRatio = 0.2;    // fallback: fraction of box width
    constexpr qreal checkPenWidthRatio = 0.15;  // stroke width as fraction of inner rect width
    constexpr qreal checkMinPenWidth = 1.5;     // minimum stroke width in pixels

    qreal padding = static_cast<qreal>(rect.width()) * checkPaddingRatio;
    if (const auto paddings = resolve<Insets>(context, StyleProperty::Padding)) {
        padding = paddings->left().value;
    }

    const QRectF innerRect = QRectF(rect).adjusted(padding, padding, -padding, -padding);
    const qreal penWidth = qMax(checkMinPenWidth, innerRect.width() * checkPenWidthRatio);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setPen(QPen(resolveIconColor(context, palette), penWidth, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(
        QPointF(innerRect.left(), innerRect.center().y()),
        QPointF(innerRect.right(), innerRect.center().y())
    );
    painter->restore();
}


void FreeCADStyle::drawChevronArrow(
    QPainter* painter,
    const QRect& rect,
    Qt::ArrowType direction,
    const QColor& color
) const
{
    constexpr qreal arrowMaxSize = 6.0;
    constexpr qreal arrowPenWidthRatio = 0.18;
    constexpr qreal arrowMinPenWidth = 1.0;
    // For down/up: width=side, height=side*ratio. For left/right: proportions swap.
    constexpr qreal arrowHeightRatio = 0.5;

    const qreal side = qMin(static_cast<qreal>(qMin(rect.width(), rect.height())), arrowMaxSize);
    const qreal penWidth = qMax(arrowMinPenWidth, side * arrowPenWidthRatio);
    const qreal halfW = side / 2.0;
    const qreal halfH = side * arrowHeightRatio / 2.0;

    // Use QRectF centre to avoid the 0.5 px truncation of QRect::center().
    const QPointF center = QRectF(rect).center();

    // Each direction is computed directly — no painter rotation, no rounding accumulation.
    QPainterPath chevronPath;
    // clang-format off
    switch (direction) {
        case Qt::UpArrow:
            chevronPath.moveTo(center.x() - halfW, center.y() + halfH);
            chevronPath.lineTo(center.x(),         center.y() - halfH);
            chevronPath.lineTo(center.x() + halfW, center.y() + halfH);
            break;
        case Qt::LeftArrow:
            chevronPath.moveTo(center.x() + halfH, center.y() - halfW);
            chevronPath.lineTo(center.x() - halfH, center.y());
            chevronPath.lineTo(center.x() + halfH, center.y() + halfW);
            break;
        case Qt::RightArrow:
            chevronPath.moveTo(center.x() - halfH, center.y() - halfW);
            chevronPath.lineTo(center.x() + halfH, center.y());
            chevronPath.lineTo(center.x() - halfH, center.y() + halfW);
            break;
        default:  // Qt::DownArrow
            chevronPath.moveTo(center.x() - halfW, center.y() - halfH);
            chevronPath.lineTo(center.x(),         center.y() + halfH);
            chevronPath.lineTo(center.x() + halfW, center.y() - halfH);
            break;
    }
    // clang-format on

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->strokePath(chevronPath, QPen(color, penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->restore();
}

void FreeCADStyle::drawTabCloseButton(
    QPainter* painter,
    const QStyleOption* option,
    const QWidget* widget
) const
{
    // Explicitly request CloseButton element so token lookup works whether widget is the
    // close button (QAbstractButton child of QTabBar) or the QTabBar itself.
    const StyleContext context = contextOf(widget, option, StyleComponentElement::CloseButton);
    const BoxGeometryDefinition geometry = resolveBoxGeometry(context);
    const BoxStyleDefinition style = resolveBoxStyle(context);
    const QRect borderRect = geometry.borderRect(option->rect);

    drawBoxBackground(painter, borderRect, style);

    // X mark — two diagonal lines crossing at center, inset by the box padding.
    constexpr qreal xPenWidthRatio = 0.12;
    constexpr qreal xMinPenWidth = 1.2;

    const QRectF innerRect = QRectF(geometry.contentRect(option->rect));
    const qreal penWidth = qMax(xMinPenWidth, innerRect.width() * xPenWidthRatio);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(
        QPen(resolveIconColor(context, option->palette), penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)
    );
    painter->drawLine(innerRect.topLeft(), innerRect.bottomRight());
    painter->drawLine(innerRect.topRight(), innerRect.bottomLeft());
    painter->restore();
}

void FreeCADStyle::paintBox(
    QPainter* painter,
    const QRect& rect,
    const StyleParameters::StyleContext& context
) const
{
    // Inset by the resolved Margin before painting, matching drawComponent — the margin is a
    // BoxGeometry property, so a box painted from a bare rect would otherwise ignore it.
    const QRect borderRect = resolveBoxGeometry(context).borderRect(rect);
    drawBoxBackground(painter, borderRect, resolveBoxStyle(context));
}

int FreeCADStyle::leadingRowGap(const QStyleOption* option, const QWidget* widget) const
{
    if (isFirstVisibleRow(option, widget)) {
        return 0;
    }

    const StyleContext itemContext = contextOf(widget, option, StyleComponentElement::Item);
    return resolveBoxGeometry(itemContext).spacing;
}

QPointF FreeCADStyle::branchCenter(const QRect& cell, int leadingGap)
{
    // Half-pixel centres keep an odd-width stroke on a single pixel row. The leading gap is
    // excluded because it belongs to the row above, so the centre tracks the item box rather
    // than the taller cell Qt hands over.
    return {
        std::floor(cell.x() + (cell.width() / 2.0)) + 0.5,
        std::floor(cell.y() + leadingGap + ((cell.height() - leadingGap) / 2.0)) + 0.5,
    };
}

QList<QLineF> FreeCADStyle::branchSegments(
    const QRect& cell,
    QStyle::State state,
    bool topLevel,
    Qt::LayoutDirection direction,
    int leadingGap
)
{
    if (topLevel) {
        return {};
    }

    const bool ownsItem = state.testFlag(QStyle::State_Item);
    const bool siblingFollows = state.testFlag(QStyle::State_Sibling);
    const bool hasArrow = state.testFlag(QStyle::State_Children);

    const QPointF center = branchCenter(cell, leadingGap);

    // An expand arrow occupies the centre of its own cell. The connectors stop short of it so
    // the glyph reads as a symbol rather than as a bead threaded onto a wire.
    const qreal clearance = hasArrow ? arrowClearance : 0.0;

    QList<QLineF> segments;

    if (siblingFollows && !hasArrow) {
        segments.append(QLineF(center.x(), cell.top(), center.x(), cell.bottom() + 1));
    }
    else {
        if (siblingFollows || ownsItem) {
            segments.append(QLineF(center.x(), cell.top(), center.x(), center.y() - clearance));
        }
        if (siblingFollows) {
            segments.append(QLineF(center.x(), center.y() + clearance, center.x(), cell.bottom() + 1));
        }
    }

    if (ownsItem) {
        // In a right-to-left layout the item's own cell is the leftmost of the branch
        // cells and the label sits to its left, so the stub must reach toward the left
        // edge rather than the right edge it uses in left-to-right layouts.
        const bool rightToLeft = direction == Qt::RightToLeft;
        const qreal stubEnd = rightToLeft ? cell.left() : cell.right() + 1;
        const qreal stubStart = rightToLeft ? center.x() - clearance : center.x() + clearance;
        segments.append(QLineF(stubStart, center.y(), stubEnd, center.y()));
    }

    return segments;
}

bool FreeCADStyle::isLeadingCell(const QStyleOptionViewItem* vopt)
{
    return vopt->viewItemPosition == QStyleOptionViewItem::Beginning
        || vopt->viewItemPosition == QStyleOptionViewItem::OnlyOne
        || vopt->viewItemPosition == QStyleOptionViewItem::Invalid;
}

void FreeCADStyle::reachToLeadingEdge(QRect& rect, const QStyleOptionViewItem* vopt, const QWidget* widget)
{
    if (vopt->direction != Qt::RightToLeft) {
        rect.setLeft(0);
        return;
    }

    const auto* view = qobject_cast<const QAbstractItemView*>(widget);
    if (view != nullptr && view->viewport() != nullptr) {
        rect.setRight(view->viewport()->width() - 1);
    }
}

void FreeCADStyle::drawItemViewRow(
    QPainter* painter,
    const QStyleOptionViewItem* vopt,
    const QWidget* widget,
    RowLayer layer
) const
{
    StyleContext rowContext = contextOf(widget, vopt, StyleComponentElement::Row);

    const bool interactive = rowContext.state.testFlag(StyleState::Hovered)
        || rowContext.state.testFlag(StyleState::Pressed)
        || rowContext.state.testFlag(StyleState::Checked);

    if (layer == RowLayer::Surface) {
        // The surface is what the row looks like at rest — its own background, or the
        // alternating one. Interaction belongs to the layer above.
        //
        // contextOf only marks a row alternate when the option carries no state at all, so that
        // an interaction resolves through ListRowHovered* rather than ListRowAlternateHovered*.
        // At rest that rule has nothing to protect and everything to lose: State_HasFocus sits
        // on whichever cell is current and State_MouseOver on a hovered row, and either one
        // would drop the alternating background from that cell alone.
        rowContext.state = {};
        if (vopt->features & QStyleOptionViewItem::Alternate) {
            rowContext.variant.set(VariantSlot::RowType, RowType::Alternate);
        }
    }
    else if (!interactive) {
        return;
    }

    const BoxGeometryDefinition itemGeometry = resolveBoxGeometry(
        contextOf(widget, vopt, StyleComponentElement::Item)
    );
    // Exclude the reserved inter-row gap so the highlight floats below the background gap.
    QRect rowRect = vopt->rect;
    rowRect.adjust(0, isFirstVisibleRow(vopt, widget) ? 0 : itemGeometry.spacing, 0, 0);

    // A tree indents its leading cell past the branch column, which belongs to no cell and so
    // would stay unhighlighted. The leading cell reaches back over it. Only backwards: nothing
    // paints there after this call, whereas a fill running the other way would be buried by the
    // next column's surface.
    const bool coversBranchGutter = layer == RowLayer::Interaction && isLeadingCell(vopt);
    if (coversBranchGutter) {
        reachToLeadingEdge(rowRect, vopt, widget);
    }

    // Qt clips to the cell before calling PE_PanelItemViewItem, which the reach past its
    // leading edge has to escape.
    painter->save();
    if (coversBranchGutter) {
        painter->setClipRect(rowRect, Qt::ReplaceClip);
    }
    paintBox(painter, rowRect, rowContext);
    painter->restore();
}

bool FreeCADStyle::atTreeColumnLeadingEdge(
    const QTreeView* view,
    const QRect& cellRect,
    Qt::LayoutDirection direction
)
{
    if (view == nullptr || view->header() == nullptr) {
        return false;
    }

    // treePosition() names a logical column and defaults to 0; QTreeViewPrivate::
    // logicalIndexForTree() only consults header->logicalIndex(0) once a caller sets it
    // negative via setTreePosition(). A header with no sections yet (no model attached)
    // leaves no real column to measure against; fall back to the old absolute-zero test.
    const int treeColumn = view->treePosition() >= 0 ? view->treePosition()
                                                     : view->header()->logicalIndex(0);
    if (treeColumn < 0 || treeColumn >= view->header()->count()) {
        return direction == Qt::RightToLeft ? false : cellRect.left() <= 0;
    }

    // columnViewportPosition() already accounts for horizontal scrolling, so comparing
    // against it (rather than absolute zero) keeps the root cell test correct while a
    // scrolled view or a relocated tree column moves the branch column's leading edge
    // away from x == 0.
    const int columnPosition = view->columnViewportPosition(treeColumn);

    if (direction == Qt::RightToLeft) {
        // A right-to-left layout mirrors the column so its leading edge is on the right.
        const int trailingEdge = columnPosition + view->columnWidth(treeColumn);
        return cellRect.right() >= trailingEdge - 1;
    }

    return cellRect.left() <= columnPosition;
}

void FreeCADStyle::drawItemViewBranch(
    QPainter* painter,
    const QStyleOption* option,
    const QWidget* widget
) const
{
    const auto* view = qobject_cast<const QTreeView*>(widget);
    const QVariant enabled = widget != nullptr ? widget->property("branches") : QVariant();
    const bool suppressed = enabled.isValid() && !enabled.toBool();

    if (!suppressed) {
        const bool topLevel = view != nullptr && view->rootIsDecorated()
            && atTreeColumnLeadingEdge(view, option->rect, option->direction);

        const StyleContext context = contextOf(widget, option, StyleComponentElement::Branch);

        if (const auto color = resolve<Base::Color>(context, StyleProperty::BorderColor)) {
            const auto thickness = resolve<Numeric>(context, StyleProperty::BorderThickness);

            QPen pen(color->asValue<QColor>());
            pen.setWidthF(thickness ? static_cast<double>(*thickness) : 1.0);
            pen.setCapStyle(Qt::FlatCap);

            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, false);
            painter->setPen(pen);
            painter->drawLines(branchSegments(
                option->rect,
                option->state,
                topLevel,
                option->direction,
                leadingRowGap(option, widget)
            ));
            painter->restore();
        }
    }

    if (option->state & State_Children) {
        drawBranchArrow(painter, option, widget);
    }
}

void FreeCADStyle::drawBranchArrow(QPainter* painter, const QStyleOption* option, const QWidget* widget) const
{
    const bool rightToLeft = option->direction == Qt::RightToLeft;
    const Qt::ArrowType direction = (option->state & State_Open) ? Qt::DownArrow
        : rightToLeft                                            ? Qt::LeftArrow
                                                                 : Qt::RightArrow;

    const StyleContext context = contextOf(widget, option, StyleComponentElement::Branch);
    QColor arrowColor = resolveIconColor(context, option->palette);
    arrowColor.setAlpha(arrowAlpha);

    // Centred on the point the connectors converge on, so the glyph sits in the gap they leave
    // rather than beside it. The odd box size keeps that centre on the same half-pixel.
    const QPointF center = branchCenter(option->rect, leadingRowGap(option, widget));
    const QRect arrowRect(
        static_cast<int>(center.x() - (arrowBoxSize / 2.0)),
        static_cast<int>(center.y() - (arrowBoxSize / 2.0)),
        arrowBoxSize,
        arrowBoxSize
    );

    drawChevronArrow(painter, arrowRect, direction, arrowColor);
}

bool FreeCADStyle::ownsItemViewLayout(const QStyleOptionViewItem* option, const QWidget* widget) const
{
    if (!option || !qobject_cast<const QAbstractItemView*>(widget)) {
        return false;
    }

    // Icon-mode views stack the icon above the text and word-wrapped cells need Qt's line
    // breaking — neither arrangement maps onto the token geometry, so both keep Qt's layout.
    const bool isSideBySide = option->decorationPosition == QStyleOptionViewItem::Left
        || option->decorationPosition == QStyleOptionViewItem::Right;
    if (!isSideBySide || (option->features & QStyleOptionViewItem::WrapText)) {
        return false;
    }

    return contextOf(widget, option, StyleComponentElement::Item).element
        == StyleComponentElement::Item;
}

int FreeCADStyle::itemViewTextGutter(const QStyleOption* option, const QWidget* widget) const
{
    return pixelMetric(PM_FocusFrameHMargin, option, widget) + 1;
}

std::optional<FreeCADStyle::ItemViewLayout> FreeCADStyle::itemViewLayout(
    const QStyleOptionViewItem* option,
    const QWidget* widget
) const
{
    if (!ownsItemViewLayout(option, widget)) {
        return {};
    }

    const StyleContext context = contextOf(widget, option, StyleComponentElement::Item);
    const BoxGeometryDefinition geometry = resolveBoxGeometry(context);

    // The reserved inter-row gap belongs to the list background, not to the cell content.
    const QRect content
        = geometry.contentRect(option->rect)
              .adjusted(0, isFirstVisibleRow(option, widget) ? 0 : geometry.spacing, 0, 0);

    const auto centredAt = [&content](int left, const QSize& size) {
        const int top = content.top() + ((content.height() - size.height()) / 2);
        return QRect(QPoint(left, top), size);
    };

    ItemViewLayout layout;
    int left = content.left();
    int right = content.right();

    if (option->features & QStyleOptionViewItem::HasCheckIndicator) {
        const QSize indicatorSize(
            pixelMetric(PM_IndicatorWidth, option, widget),
            pixelMetric(PM_IndicatorHeight, option, widget)
        );
        layout.checkIndicator = centredAt(left, indicatorSize);
        left = layout.checkIndicator.right() + 1 + geometry.iconSpacing;
    }

    if (option->features & QStyleOptionViewItem::HasDecoration) {
        const QSize decorationSize = option->decorationSize;
        if (option->decorationPosition == QStyleOptionViewItem::Left) {
            layout.decoration = centredAt(left, decorationSize);
            left = layout.decoration.right() + 1 + geometry.iconSpacing;
        }
        else {
            layout.decoration = centredAt(right + 1 - decorationSize.width(), decorationSize);
            right = layout.decoration.left() - 1 - geometry.iconSpacing;
        }
    }

    // The text takes whatever is left; QCommonStyle elides it to fit. It insets the rect it is
    // handed by one gutter on each side before drawing, so hand it a rect widened by exactly
    // that much — the label then lands where IconSpacing put it, with its full width intact.
    const int gutter = itemViewTextGutter(option, widget);
    layout.text = QRect(QPoint(left - gutter, content.top()), QPoint(right + gutter, content.bottom()));

    if (option->direction == Qt::RightToLeft) {
        layout.checkIndicator = visualRect(option->direction, content, layout.checkIndicator);
        layout.decoration = visualRect(option->direction, content, layout.decoration);
        layout.text = visualRect(option->direction, content, layout.text);
    }

    return layout;
}

QRect FreeCADStyle::itemViewSubElementRect(
    SubElement element,
    const QStyleOption* option,
    const QWidget* widget
) const
{
    const auto* vopt = qstyleoption_cast<const QStyleOptionViewItem*>(option);
    const auto layout = itemViewLayout(vopt, itemViewOf(vopt, widget));
    if (!layout) {
        return QProxyStyle::subElementRect(element, option, widget);
    }

    switch (element) {
        case SE_ItemViewItemCheckIndicator:
            return layout->checkIndicator;
        case SE_ItemViewItemDecoration:
            return layout->decoration;
        default:
            return layout->text;
    }
}

void FreeCADStyle::drawPrimitive(
    PrimitiveElement element,
    const QStyleOption* option,
    QPainter* painter,
    const QWidget* widget
) const
{
    if (element == PE_PanelButtonCommand) {
        drawComponent(painter, option->rect, widget, option);
        return;
    }

    if (element == PE_PanelLineEdit) {
        // Qt sets lineWidth = 0 on the inner QLineEdit embedded inside QAbstractSpinBox
        // (via setFrame(false)). Respect that: the spinbox outer frame is drawn separately
        // via CC_SpinBox → PE_PanelLineEdit with the spinbox widget, so the inner edit
        // panel must not draw a second border.
        if (const auto* frameOption = qstyleoption_cast<const QStyleOptionFrame*>(option);
            frameOption && frameOption->lineWidth == 0) {
            // The spinbox outer frame was already drawn by drawComplexControl(CC_SpinBox).
            // Do not repaint the inner QLineEdit panel — it would cover our background
            // with the system-palette colour (especially wrong in the disabled state).
            return;
        }
        drawComponent(painter, option->rect, widget, option);
        return;
    }

    if (element == PE_Frame) {
        if (qstyleoption_cast<const QStyleOptionFrame*>(option)) {
            drawComponent(painter, option->rect, widget, option);
            return;
        }
    }

    if (element == PE_FrameFocusRect) {
        // Fusion draws a semi-transparent rounded fill for focused item view cells
        // (State_Item is set by QCommonStyle::drawControl(CE_ItemViewItem)). Suppress it —
        // the selection highlight already provides sufficient focus indication.
        if (option->state & QStyle::State_Item) {
            return;
        }
    }

    if (element == PE_PanelItemViewRow) {
        // Qt emits this before the branch column and the cells, which is exactly where the
        // row's resting surface belongs: connectors and content then sit on top of it rather
        // than being buried by it.
        const StyleContext context = contextOf(widget, option, StyleComponentElement::Item);
        if (context.element == StyleComponentElement::Item) {
            if (const auto* vopt = qstyleoption_cast<const QStyleOptionViewItem*>(option)) {
                drawItemViewRow(painter, vopt, widget, RowLayer::Surface);
            }
            return;
        }
    }

    if (element == PE_PanelItemViewItem) {
        const auto* vopt = qstyleoption_cast<const QStyleOptionViewItem*>(option);
        if (!vopt) {
            return;
        }

        drawItemViewRow(painter, vopt, widget, RowLayer::Interaction);

        const StyleContext context = contextOf(widget, option, StyleComponentElement::Item);
        const BoxGeometryDefinition itemGeometry = resolveBoxGeometry(context);
        const QRect itemRect = option->rect.adjusted(
            0,
            isFirstVisibleRow(option, widget) ? 0 : itemGeometry.spacing,
            0,
            0
        );

        paintBox(painter, itemRect, context);

        return;
    }

    if (element == PE_IndicatorBranch) {
        drawItemViewBranch(painter, option, widget);
        return;
    }

    if (element == PE_IndicatorRadioButton) {
        const StyleContext context = contextOf(widget, option, StyleComponentElement::Indicator);
        drawBoxBackground(painter, option->rect, resolveBoxStyle(context));
        if (option->state & QStyle::State_On) {
            drawRadioButtonDot(painter, option->rect, context, option->palette);
        }
        return;
    }

    if (element == PE_IndicatorCheckBox) {
        const StyleContext context = contextOf(widget, option, StyleComponentElement::Indicator);
        drawBoxBackground(painter, option->rect, resolveBoxStyle(context));
        if (option->state & QStyle::State_On) {
            drawCheckMark(painter, option->rect, context, option->palette);
        }
        else if (option->state & QStyle::State_NoChange) {
            drawIndeterminateMark(painter, option->rect, context, option->palette);
        }
        return;
    }

    if (element == PE_IndicatorToolBarSeparator) {
        // In a horizontal toolbar the buttons are side-by-side, so the separator
        // is a vertical line; in a vertical toolbar it is a horizontal line.
        const bool toolbarIsHorizontal = option->state & QStyle::State_Horizontal;
        drawSeparatorLine(
            painter,
            toolbarIsHorizontal ? option->rect.adjusted(0, 4, 0, -4)
                                : option->rect.adjusted(4, 0, -4, 0),
            !toolbarIsHorizontal
        );
        return;
    }

    if (element == PE_FrameTabBarBase) {
        if (const auto* tabBaseOption = qstyleoption_cast<const QStyleOptionTabBarBase*>(option)) {
            drawTabBarBase(painter, tabBaseOption, widget);
            return;
        }
    }

    if (element == PE_FrameTabWidget) {
        if (const auto* tabWidgetOption = qstyleoption_cast<const QStyleOptionTabWidgetFrame*>(option)) {
            drawTabWidgetFrame(painter, tabWidgetOption, widget);
            return;
        }
    }

    if (element == PE_IndicatorTabClose) {
        drawTabCloseButton(painter, option, widget);
        return;
    }

    if (element == PE_IndicatorArrowDown || element == PE_IndicatorArrowUp
        || element == PE_IndicatorArrowLeft || element == PE_IndicatorArrowRight) {
        // clang-format off
        const Qt::ArrowType direction = [&] {
            switch (element) {
                case PE_IndicatorArrowUp:    return Qt::UpArrow;
                case PE_IndicatorArrowLeft:  return Qt::LeftArrow;
                case PE_IndicatorArrowRight: return Qt::RightArrow;
                default:                     return Qt::DownArrow;
            }
        }();
        // clang-format on
        const StyleContext context = contextOf(widget, option);
        QColor arrowColor = resolveIconColor(context, option->palette);
        arrowColor.setAlpha(160);
        drawChevronArrow(painter, option->rect, direction, arrowColor);
        return;
    }

    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

QSize FreeCADStyle::tabBarTabSizeFromContents(
    const QStyleOption* option,
    const QSize& size,
    const QWidget* widget
) const
{
    QSize result = QProxyStyle::sizeFromContents(CT_TabBarTab, option, size, widget);

    const auto* tabOption = qstyleoption_cast<const QStyleOptionTab*>(option);
    if (tabOption) {
        const BoxGeometryDefinition tabGeometry = resolveBoxGeometry(
            withNorthPosition(contextOf(widget, option, StyleComponentElement::Tab))
        );

        // Adjust icon–text gap: Qt hardcodes 4 px; replace with our token value.
        if (!tabOption->icon.isNull() && !tabOption->text.isEmpty()) {
            result.rwidth() += tabGeometry.iconGapDelta();
        }

        // Adjust close-button gap: Qt also hardcodes 4 px next to each button; replace with
        // our token value (same Tab IconSpacing, one delta per button side present).
        if (tabOption->rightButtonSize.isValid()) {
            result.rwidth() += tabGeometry.iconGapDelta();
        }
        if (tabOption->leftButtonSize.isValid()) {
            result.rwidth() += tabGeometry.iconGapDelta();
        }
    }

    // The background is painted narrower by |tabOverlap| on the trailing edge to create the
    // visual gap between tabs (see drawTabBarTab). Add that same amount to the tab rect so
    // the content area, computed from the background rect, still has symmetric padding.
    const int tabOverlap = proxy()->pixelMetric(PM_TabBarTabOverlap, option, widget);
    if (tabOverlap < 0) {
        result.rwidth() -= tabOverlap;
    }

    // If the tab bar has an expanding size policy along its cross-axis, grow tabs to fill the
    // allocated extent rather than staying at the style-computed minimum.
    // Note: QTabBar transposes tab sizes for East/West; the height here maps to widget width.
    if (const auto* tabBar = qobject_cast<const QTabBar*>(widget)) {
        const bool isVertical = tabBar->shape() == QTabBar::RoundedEast
            || tabBar->shape() == QTabBar::RoundedWest || tabBar->shape() == QTabBar::TriangularEast
            || tabBar->shape() == QTabBar::TriangularWest;
        if (isVertical) {
            if (tabBar->sizePolicy().horizontalPolicy() & QSizePolicy::ExpandFlag) {
                result.setWidth(tabBar->width());
            }
        }
        else {
            if (tabBar->sizePolicy().verticalPolicy() & QSizePolicy::ExpandFlag) {
                result.setHeight(tabBar->height());
            }
        }
    }

    return result;
}

QSize FreeCADStyle::toolButtonSizeFromContents(
    const QStyleOptionToolButton* option,
    const QSize& size,
    const QWidget* widget
) const
{
    BoxGeometryDefinition geometry = resolveBoxGeometry(contextOf(widget, option));

    const int menuWidth = proxy()->pixelMetric(PM_MenuButtonIndicator, option, widget);
    const bool hasMenu = option->features & QStyleOptionToolButton::MenuButtonPopup;

    const std::optional<Qt::Orientation> toolbarOrientation = toolbarOrientationOf(widget);

    // QToolButton::sizeHint() adds PM_MenuButtonIndicator to the width for
    // MenuButtonPopup buttons before calling sizeFromContents, regardless of toolbar
    // orientation. For vertical toolbars the strip goes below the icon, so we move
    // the indicator contribution from width to height.
    QSize contentSize = size;
    if (hasMenu && toolbarOrientation == Qt::Vertical) {
        contentSize.rwidth() -= menuWidth;
        contentSize.rheight() += menuWidth;
    }

    // For horizontal menu-strip buttons, minWidth expresses the button body minimum;
    // add the strip width so constrain sees the correct total minimum.
    const bool hasHorizontalMenu = hasMenu && toolbarOrientation != Qt::Vertical;
    if (hasHorizontalMenu && geometry.minWidth) {
        geometry.minWidth = *geometry.minWidth + menuWidth;
    }

    // For instant/delayed popup buttons the arrow indicator is drawn inline within the
    // button body (no separate strip). Reserve width for it so the icon does not overlap.
    const bool hasInlineIndicator = (option->features & QStyleOptionToolButton::HasMenu) && !hasMenu;
    if (hasInlineIndicator) {
        contentSize.rwidth() += menuWidth;
        if (geometry.minWidth) {
            geometry.minWidth = *geometry.minWidth + menuWidth;
        }
    }

    return geometry.sizeFromContents(contentSize);
}

QSize FreeCADStyle::itemViewItemSizeFromContents(
    const QStyleOption* option,
    const QSize& size,
    const QWidget* widget
) const
{
    const StyleContext context = contextOf(widget, option, StyleComponentElement::Item);
    if (context.element != StyleComponentElement::Item) {
        return QProxyStyle::sizeFromContents(CT_ItemViewItem, option, size, widget);
    }
    const BoxGeometryDefinition geometry = resolveBoxGeometry(context);

    // If there is an index widget registered for this item (set via setItemWidget),
    // use its natural sizeHint as the base so callers do not need to setSizeHint.
    QSize baseSize = size;
    const auto* vopt = qstyleoption_cast<const QStyleOptionViewItem*>(option);
    if (const auto* view = qobject_cast<const QAbstractItemView*>(widget);
        view && vopt && vopt->index.isValid()) {
        if (const QWidget* indexWidget = view->indexWidget(vopt->index)) {
            baseSize = indexWidget->sizeHint();
        }
    }
    if (!baseSize.isValid()) {
        baseSize = QProxyStyle::sizeFromContents(CT_ItemViewItem, option, size, widget);
        // Qt sizes a cell by surrounding each of its parts with one gutter. itemViewLayout()
        // separates them with IconSpacing instead, so trade the gutters Qt charged for the
        // gaps actually inserted.
        if (vopt && ownsItemViewLayout(vopt, itemViewOf(vopt, widget))) {
            const int parts = itemViewPartCount(*vopt);
            const int gaps = std::max(0, parts - 1) * geometry.iconSpacing;
            const int gutters = 2 * parts * itemViewTextGutter(option, widget);
            baseSize.setWidth(std::max(0, baseSize.width() - gutters + gaps));
        }
    }

    QSize itemSize = geometry.sizeFromContents(baseSize);
    // ListItemSpacing: reserve an inter-row gap above each row. The gap is excluded from the
    // content rect (subElementRect) and the highlight (drawItemViewRow), so it renders as the
    // list background between rows. The topmost row has nothing above it to separate from.
    itemSize.rheight() += isFirstVisibleRow(option, widget) ? 0 : geometry.spacing;
    return itemSize;
}

QSize FreeCADStyle::sizeFromContents(
    ContentsType type,
    const QStyleOption* option,
    const QSize& size,
    const QWidget* widget
) const
{
    if (type == CT_PushButton) {
        const auto* btnOption = qstyleoption_cast<const QStyleOptionButton*>(option);
        const BoxGeometryDefinition geometry = resolveBoxGeometry(contextOf(widget, option));
        QSize contentSize = size;
        if (btnOption && !btnOption->icon.isNull() && !btnOption->text.isEmpty()) {
            contentSize.rwidth() += geometry.iconGapDelta();
        }
        return geometry.sizeFromContents(contentSize);
    }

    if (type == CT_ComboBox) {
        const auto* comboOption = qstyleoption_cast<const QStyleOptionComboBox*>(option);
        const BoxGeometryDefinition geometry = resolveBoxGeometry(contextOf(widget, option));
        QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
        // QComboBox::sizeHint bakes iconSize.width() + qtBuiltInIconGap into the content
        // size it passes here when the current item has an icon.  Replace that gap with
        // the token value, matching the layout used in drawComboBoxLabel.
        if (comboOption && !comboOption->currentIcon.isNull()) {
            result.rwidth() += geometry.iconGapDelta();
        }
        return geometry.sizeFromContents(result);
    }

    if (type == CT_MenuBarItem) {
        const BoxGeometryDefinition geometry = resolveBoxGeometry(
            contextOf(widget, option, StyleComponentElement::Item)
        );

        return geometry.marginBox(size);
    }

    if (type == CT_TabBarTab) {
        return tabBarTabSizeFromContents(option, size, widget);
    }

    if (type == CT_LineEdit || type == CT_SpinBox) {
        const BoxGeometryDefinition geometry = resolveBoxGeometry(contextOf(widget, option));
        return geometry.sizeFromContents(QProxyStyle::sizeFromContents(type, option, size, widget));
    }

    if (type == CT_ToolButton) {
        if (const auto* opt = qstyleoption_cast<const QStyleOptionToolButton*>(option)) {
            return toolButtonSizeFromContents(opt, size, widget);
        }
    }

    if (type == CT_ItemViewItem) {
        return itemViewItemSizeFromContents(option, size, widget);
    }

    if (type == CT_HeaderSection) {
        if (const auto* headerOption = qstyleoption_cast<const QStyleOptionHeader*>(option)) {
            const StyleContext itemContext
                = contextOf(widget, headerOption, StyleComponentElement::Item);
            const BoxGeometryDefinition geometry = resolveBoxGeometry(itemContext);
            return geometry.sizeFromContents(QProxyStyle::sizeFromContents(type, option, size, widget));
        }
    }

    if (type == CT_GroupBox) {
        if (const auto* groupBoxOption = qstyleoption_cast<const QStyleOptionGroupBox*>(option)) {
            return groupBoxSizeFromContents(groupBoxOption, size, widget);
        }
    }

    return QProxyStyle::sizeFromContents(type, option, size, widget);
}

QRect FreeCADStyle::subElementRect(SubElement element, const QStyleOption* option, const QWidget* widget) const
{
    // QProxyStyle sets baseStyle->proxy = this, so the base style's drawControl(CE_ItemViewItem)
    // calls proxy()->subElementRect() which reaches OUR overrides below.  Serving the three cell
    // sub-elements from itemViewLayout() therefore drives both drawing (via the base-style
    // drawControl callback) and editor placement (via updateEditorGeometry).
    if (element == SE_ItemViewItemCheckIndicator || element == SE_ItemViewItemDecoration
        || element == SE_ItemViewItemText) {
        return itemViewSubElementRect(element, option, widget);
    }

    if (element == SE_HeaderLabel) {
        const auto* headerOption = qstyleoption_cast<const QStyleOptionHeader*>(option);
        if (!headerOption) {
            return QProxyStyle::subElementRect(element, option, widget);
        }
        const StyleContext itemContext = contextOf(widget, option, StyleComponentElement::Item);
        const BoxGeometryDefinition geometry = resolveBoxGeometry(itemContext);
        QStyleOptionHeader adjustedOption = *headerOption;
        adjustedOption.rect = geometry.contentRect(headerOption->rect);
        return QProxyStyle::subElementRect(element, &adjustedOption, widget);
    }

    if (element == SE_TabWidgetTabContents) {
        StyleContext paneContext;
        paneContext.component = StyleComponent::TabWidget;
        paneContext.element = StyleComponentElement::Root;
        bindWidget(paneContext, widget);
        const BoxGeometryDefinition geometry = resolveBoxGeometry(paneContext);
        const QRect paneRect = QProxyStyle::subElementRect(SE_TabWidgetTabPane, option, widget);
        return geometry.contentRect(paneRect);
    }

    if (element == SE_LineEditContents) {
        // Qt sets lineWidth = 0 on the inner QLineEdit of QAbstractSpinBox (setFrame(false)).
        // In that case, the spinbox itself manages the edit field rect — do not apply our
        // padding on top of it.
        const auto* frameOption = qstyleoption_cast<const QStyleOptionFrame*>(option);
        if (frameOption && frameOption->lineWidth == 0) {
            return QProxyStyle::subElementRect(element, option, widget);
        }
        const StyleContext context = contextOf(widget, option);
        const BoxGeometryDefinition geometry = resolveBoxGeometry(context);
        return geometry.contentRect(option->rect);
    }

    return QProxyStyle::subElementRect(element, option, widget);
}

QFont FreeCADStyle::groupBoxTitleFont(const QStyleOptionGroupBox* option, const QWidget* widget) const
{
    const StyleContext titleContext = contextOf(widget, option, StyleComponentElement::Title);
    return resolveFont(titleContext, widget != nullptr ? widget->font() : QApplication::font());
}

QRect FreeCADStyle::groupBoxTitleRect(const QStyleOptionGroupBox* option, const QWidget* widget) const
{
    QRect titleRect;

    if (option->subControls & SC_GroupBoxLabel) {
        titleRect = proxy()->subControlRect(CC_GroupBox, option, SC_GroupBoxLabel, widget);
    }

    if (option->subControls & SC_GroupBoxCheckBox) {
        titleRect = titleRect.united(
            proxy()->subControlRect(CC_GroupBox, option, SC_GroupBoxCheckBox, widget)
        );
    }

    return titleRect;
}

QRect FreeCADStyle::groupBoxSubControlRect(
    const QStyleOptionGroupBox* option,
    SubControl subControl,
    const QWidget* widget
) const
{
    // QCommonStyle lays the label out from option->fontMetrics. Substituting the title font's
    // metrics is what makes its alignment and right-to-left handling place our font correctly.
    QStyleOptionGroupBox titled = *option;
    titled.fontMetrics = QFontMetrics(groupBoxTitleFont(option, widget));

    if (subControl == SC_GroupBoxLabel || subControl == SC_GroupBoxCheckBox) {
        // Fusion positions the title from option->rect.width() alone, never from its left edge —
        // true of every alignment branch, not only the leading one. Insetting the rect first
        // gives Fusion the right *available width* to align a centred or trailing title within;
        // the same inset then has to be added back as a flat shift afterwards, because nothing
        // downstream folds that missing origin back in. For left-to-right layouts
        // QStyle::visualRect() is a no-op, so the whole correction is this shift. For
        // right-to-left layouts visualRect() does reposition the rect, but only by reading the
        // option rect's right edge and width — never its left edge — so it already absorbs the
        // right-padding inset on its own and still leaves the same left-padding gap unresolved.
        // That is why the shift below always uses the left padding, mirrored in sign but not in
        // which margin it reads.
        const QMarginsF padding = resolveBoxGeometry(contextOf(widget, option)).padding;
        const int leftPadding = static_cast<int>(padding.left());
        const int rightPadding = static_cast<int>(padding.right());

        titled.rect = titled.rect.adjusted(leftPadding, 0, -rightPadding, 0);

        const QRect delegated = QProxyStyle::subControlRect(CC_GroupBox, &titled, subControl, widget);
        const int shift = titled.direction == Qt::RightToLeft ? -leftPadding : leftPadding;

        QRect placed = delegated.translated(shift, 0);

        // Fusion pads the title band by two pixels and then nudges the label a further pixel
        // down, so the band it hands back is not centred on the frame's top edge the way the
        // masking model needs it to be — the title reads low and the notch with it. Re-centre it
        // here, where the label, the check indicator and the notch all read the same rect.
        const QRect frameRect
            = QProxyStyle::subControlRect(CC_GroupBox, &titled, SC_GroupBoxFrame, widget);
        placed.moveTop(frameRect.top() - (placed.height() / 2));

        return placed;
    }

    if (subControl != SC_GroupBoxContents) {
        return QProxyStyle::subControlRect(CC_GroupBox, &titled, subControl, widget);
    }

    const QRect frameRect = QProxyStyle::subControlRect(CC_GroupBox, &titled, SC_GroupBoxFrame, widget);
    const QMarginsF padding = resolveBoxGeometry(contextOf(widget, option)).padding;

    // Uniform: the padding is the gap between the frame and its contents, and a title does not
    // change it. The title's lower half hangs into the frame, but only into the top padding,
    // which is deep enough for the descenders of the fonts a title is set in.
    return frameRect.adjusted(
        static_cast<int>(padding.left()),
        static_cast<int>(padding.top()),
        -static_cast<int>(padding.right()),
        -static_cast<int>(padding.bottom())
    );
}

QSize FreeCADStyle::groupBoxSizeFromContents(
    const QStyleOptionGroupBox* option,
    const QSize& size,
    const QWidget* widget
) const
{
    // The base style reserves the title band's height from option->fontMetrics. Substituting the
    // title font's metrics is what keeps that band as tall as the space the layout leaves clear
    // for it, which is measured from the same font.
    QStyleOptionGroupBox titled = *option;
    const QFontMetrics titleMetrics(groupBoxTitleFont(option, widget));
    titled.fontMetrics = titleMetrics;

    QSize result = QProxyStyle::sizeFromContents(CT_GroupBox, &titled, size, widget);

    if (option->text.isEmpty()) {
        return result;
    }

    // The width of the title itself never reaches the base style: QGroupBox::minimumSizeHint
    // measures it with the widget's font and hands the result in as @p size, and it is not a
    // virtual we can override. Re-charge the difference here so a larger title font cannot clip
    // and a smaller one does not reserve width it will never use.
    const QString measured = option->text + u' ';

    result.rwidth() += titleMetrics.horizontalAdvance(measured)
        - option->fontMetrics.horizontalAdvance(measured);

    return result;
}

QRect FreeCADStyle::comboBoxSubControlRect(
    const QStyleOptionComboBox* option,
    SubControl subControl,
    const QWidget* widget
) const
{
    const BoxGeometryDefinition geometry = resolveBoxGeometry(contextOf(widget, option));
    const QRect outerRect = option->rect;
    const QRect contentRect = geometry.contentRect(outerRect);
    const int arrowWidth = proxy()->pixelMetric(PM_MenuButtonIndicator, option, widget);

    const int arrowLeft = contentRect.right() - arrowWidth + 1;
    const int editRight = arrowLeft - 1;

    switch (subControl) {
        case SC_ComboBoxFrame:
            return outerRect;
        case SC_ComboBoxEditField:
            return {
                contentRect.left(),
                contentRect.top(),
                editRight - contentRect.left() + 1,
                contentRect.height()
            };
        case SC_ComboBoxArrow:
            return {arrowLeft, contentRect.top(), arrowWidth, contentRect.height()};
        default:
            return QProxyStyle::subControlRect(CC_ComboBox, option, subControl, widget);
    }
}

QRect FreeCADStyle::spinBoxSubControlRect(
    const QStyleOptionSpinBox* option,
    SubControl subControl,
    const QWidget* widget
) const
{
    const BoxGeometryDefinition geometry = resolveBoxGeometry(contextOf(widget, option));
    const QRect outerRect = option->rect;
    const QSize preferredSize = sizeFromContents(CT_SpinBox, option, {}, widget);
    const QRect contentRect = geometry.contentRect(outerRect, preferredSize);

    // Borrow the button width from the base style; only the position changes.
    const bool hasButtons = option->buttonSymbols != QAbstractSpinBox::NoButtons;
    const QSize buttonSize = hasButtons
        ? QProxyStyle::subControlRect(CC_SpinBox, option, SC_SpinBoxUp, widget).size()
        : QSize {};

    const int buttonLeft = contentRect.right() - buttonSize.width();
    const int editRight = hasButtons ? buttonLeft - 1 : contentRect.right();
    const int centerY = contentRect.center().y();

    switch (subControl) {
        case SC_SpinBoxFrame:
            return outerRect;
        case SC_SpinBoxEditField:
            return {
                contentRect.left(),
                contentRect.top(),
                editRight - contentRect.left() + 1,
                contentRect.height()
            };

        case SC_SpinBoxUp:
        case SC_SpinBoxDown: {
            if (!hasButtons) {
                return {};
            }

            const auto buttonTop = subControl == SC_SpinBoxUp ? centerY - buttonSize.height()
                                                              : centerY;

            return {buttonLeft, buttonTop + 1, buttonSize.width(), buttonSize.height()};
        }
        default:
            return QProxyStyle::subControlRect(CC_SpinBox, option, subControl, widget);
    }
}

QRect FreeCADStyle::toolButtonSubControlRect(
    const QStyleOptionToolButton* option,
    SubControl subControl,
    const QWidget* widget
) const
{
    const QRect rect = option->rect;

    if (option->features & QStyleOptionToolButton::MenuButtonPopup) {
        const int menuWidth = proxy()->pixelMetric(PM_MenuButtonIndicator, option, widget);
        const bool isVertical = toolbarOrientationOf(widget) == Qt::Vertical;

        switch (subControl) {
            case SC_ToolButton:
                if (isVertical) {
                    return {rect.left(), rect.top(), rect.width(), rect.height() - menuWidth};
                }
                return {rect.left(), rect.top(), rect.width() - menuWidth, rect.height()};
            case SC_ToolButtonMenu:
                if (isVertical) {
                    return {rect.left(), rect.bottom() - menuWidth + 1, rect.width(), menuWidth};
                }
                return {rect.right() - menuWidth + 1, rect.top(), menuWidth, rect.height()};
            default:
                return QProxyStyle::subControlRect(CC_ToolButton, option, subControl, widget);
        }
    }

    return QProxyStyle::subControlRect(CC_ToolButton, option, subControl, widget);
}

QRect FreeCADStyle::subControlRect(
    ComplexControl complexControl,
    const QStyleOptionComplex* option,
    SubControl subControl,
    const QWidget* widget
) const
{
    if (complexControl == CC_ComboBox) {
        if (const auto* opt = qstyleoption_cast<const QStyleOptionComboBox*>(option)) {
            return comboBoxSubControlRect(opt, subControl, widget);
        }
    }
    if (complexControl == CC_SpinBox) {
        if (const auto* opt = qstyleoption_cast<const QStyleOptionSpinBox*>(option)) {
            return spinBoxSubControlRect(opt, subControl, widget);
        }
    }
    if (complexControl == CC_ToolButton) {
        if (const auto* opt = qstyleoption_cast<const QStyleOptionToolButton*>(option)) {
            return toolButtonSubControlRect(opt, subControl, widget);
        }
    }
    if (complexControl == CC_GroupBox) {
        if (const auto* opt = qstyleoption_cast<const QStyleOptionGroupBox*>(option)) {
            return groupBoxSubControlRect(opt, subControl, widget);
        }
    }
    return QProxyStyle::subControlRect(complexControl, option, subControl, widget);
}

void FreeCADStyle::drawSpinBox(
    const QStyleOptionSpinBox* option,
    QPainter* painter,
    const QWidget* widget
) const
{
    if (option->frame && (option->subControls & SC_SpinBoxFrame)) {
        const QRect frameRect = proxy()->subControlRect(CC_SpinBox, option, SC_SpinBoxFrame, widget);
        drawComponent(painter, frameRect, widget, option);
    }

    // Draw spin button arrows on a transparent background (Breeze-style: no
    // separate button fill). We do not delegate to the base style at all — it
    // would re-draw its own frame and button backgrounds on top of ours.
    if (option->buttonSymbols != QAbstractSpinBox::NoButtons) {
        const bool isPlusMinus = option->buttonSymbols == QAbstractSpinBox::PlusMinus;

        const auto drawSpinButton = [&](SubControl subControl,
                                        PrimitiveElement arrowIndicator,
                                        PrimitiveElement plusMinusIndicator) {
            if (!(option->subControls & subControl)) {
                return;
            }
            QStyleOptionSpinBox buttonOption = *option;
            buttonOption.rect = proxy()->subControlRect(CC_SpinBox, option, subControl, widget);
            // Clear the sunken flag unless this specific button is active.
            if (!(option->activeSubControls & subControl)) {
                buttonOption.state &= ~State_Sunken;
            }
            proxy()->drawPrimitive(
                isPlusMinus ? plusMinusIndicator : arrowIndicator,
                &buttonOption,
                painter,
                widget
            );
        };

        drawSpinButton(SC_SpinBoxUp, PE_IndicatorArrowUp, PE_IndicatorSpinPlus);
        drawSpinButton(SC_SpinBoxDown, PE_IndicatorArrowDown, PE_IndicatorSpinMinus);
    }
}

void FreeCADStyle::drawComboBox(
    const QStyleOptionComboBox* option,
    QPainter* painter,
    const QWidget* widget
) const
{
    drawComponent(painter, option->rect, widget, option);

    // QComboBox::paintEvent draws CE_ComboBoxLabel separately; it uses our
    // subControlRect(SC_ComboBoxEditField) for the text area.
    if (option->subControls & SC_ComboBoxArrow) {
        QStyleOptionComboBox arrowOption = *option;
        arrowOption.rect = proxy()->subControlRect(CC_ComboBox, option, SC_ComboBoxArrow, widget);
        proxy()->drawPrimitive(PE_IndicatorArrowDown, &arrowOption, painter, widget);
    }
}

void FreeCADStyle::drawToolButton(
    const QStyleOptionToolButton* option,
    QPainter* painter,
    const QWidget* widget
) const
{
    const bool hasMenuButton = option->features & QStyleOptionToolButton::MenuButtonPopup;
    const bool isVertical = toolbarOrientationOf(widget) == Qt::Vertical;

    // Draw the main button area. Strip State_Sunken when only the menu strip is the
    // active subcontrol so that clicking the dropdown does not depress the main area.
    // When the menu strip is being pressed Qt may clear State_MouseOver from the overall
    // state. Use activeSubControls instead: it is non-zero whenever the mouse is over
    // any part of the split button, so the main half keeps its hover look.
    const QRect mainRect = proxy()->subControlRect(CC_ToolButton, option, SC_ToolButton, widget);
    QStyleOptionToolButton mainOption = *option;
    if (!(option->activeSubControls & SC_ToolButton)) {
        mainOption.state &= ~State_Sunken;
    }
    if (hasMenuButton && option->activeSubControls) {
        mainOption.state |= State_MouseOver;
    }
    drawBoxBackground(
        painter,
        mainRect,
        seamedBoxStyle(contextOf(widget, &mainOption), StyleComponentElement::Root, hasMenuButton, isVertical)
    );

    if (hasMenuButton) {
        // Draw the dropdown arrow strip with its own interactive state.
        const QRect menuRect
            = proxy()->subControlRect(CC_ToolButton, option, SC_ToolButtonMenu, widget);
        QStyleOptionToolButton menuOption = *option;
        if (!(option->activeSubControls & SC_ToolButtonMenu)) {
            menuOption.state &= ~State_Sunken;
        }
        drawBoxBackground(
            painter,
            menuRect,
            seamedBoxStyle(contextOf(widget, &menuOption), StyleComponentElement::Menu, hasMenuButton, isVertical)
        );

        QStyleOptionToolButton arrowOption = *option;
        arrowOption.rect = menuRect;
        proxy()->drawPrimitive(PE_IndicatorArrowDown, &arrowOption, painter, widget);
    }
    QRect labelRect = mainRect;

    if (option->features & QStyleOptionToolButton::HasMenu && !hasMenuButton) {
        // Instant/delayed popup: draw a small arrow indicator on the right, vertically
        // centered within the content area (respecting margin and padding).
        const BoxGeometryDefinition geometry = resolveBoxGeometry(contextOf(widget, option));
        const QRect contentArea = geometry.contentRect(option->rect);
        const int arrowSize = proxy()->pixelMetric(PM_MenuButtonIndicator, option, widget);

        QStyleOptionToolButton arrowOption = *option;
        arrowOption.rect = QRect(
            contentArea.right() - arrowSize + 1,
            contentArea.top() + ((contentArea.height() - arrowSize) / 2),
            arrowSize,
            arrowSize
        );
        proxy()->drawPrimitive(PE_IndicatorArrowDown, &arrowOption, painter, widget);

        // Narrow the label rect by arrowSize only. The sizeFromContents adds
        // arrowSize + iconSpacing to the button width, so the content rect inside
        // CE_ToolButtonLabel ends up iconSpacing wider than the icon — the icon
        // centers within it, placing iconSpacing/2 of buffer between the icon and
        // the arrow rect. Do not subtract iconSpacing here as well, which would
        // cancel its effect entirely.
        labelRect.setRight(mainRect.right() - arrowSize);
    }

    // Draw label (icon + text). Restrict to SC_ToolButton so it does not bleed into
    // the menu strip. Also clear SC_ToolButtonMenu so QCommonStyle::CE_ToolButtonLabel
    // does not draw its own menu indicator arrow on top of ours.
    QStyleOptionToolButton labelOption = *option;
    labelOption.rect = labelRect;
    labelOption.subControls &= ~SC_ToolButtonMenu;
    proxy()->drawControl(CE_ToolButtonLabel, &labelOption, painter, widget);
}

FreeCADStyle::BoxStyleDefinition FreeCADStyle::seamedBoxStyle(
    const StyleContext& context,
    StyleComponentElement element,
    bool hasMenuButton,
    bool isVertical
) const
{
    BoxStyleDefinition style = resolveBoxStyle(context);
    if (!hasMenuButton) {
        return style;
    }

    const bool isTrailing = (element != StyleComponentElement::Menu);

    // The main button (isTrailing) keeps its border on the joining edge — it acts as
    // the visible separator between the two halves. The menu strip removes its border
    // on that same edge to avoid a double border.
    if (!isTrailing && style.borderThickness.has_value()) {
        if (isVertical) {
            style.borderThickness->setTop(0);
        }
        else {
            style.borderThickness->setLeft(0);
        }
    }

    // Both halves need square corners at the seam.
    if (isVertical) {
        if (isTrailing) {
            style.borderRadius.setBottom(0);
        }
        else {
            style.borderRadius.setTop(0);
        }
    }
    else {
        if (isTrailing) {
            style.borderRadius.setRight(0);
        }
        else {
            style.borderRadius.setLeft(0);
        }
    }
    return style;
}

QPainterPath FreeCADStyle::groupBoxBorderMask(
    const QStyleOptionGroupBox* option,
    const QWidget* widget,
    const QRect& frameRect
) const
{
    QRect titleRect = groupBoxTitleRect(option, widget);
    if (titleRect.isNull()) {
        return {};
    }

    const StyleContext titleContext = contextOf(widget, option, StyleComponentElement::Title);
    const QMarginsF titlePadding = resolveBoxGeometry(titleContext).padding;

    titleRect
        .adjust(-static_cast<int>(titlePadding.left()), 0, static_cast<int>(titlePadding.right()), 0);

    // Horizontally only. The title straddles the frame's top edge, so the notch has to reach
    // past that edge to take the whole stroke with it; a title wider than its box, on the other
    // hand, must not cut outside the frame.
    titleRect.setLeft(std::max(titleRect.left(), frameRect.left()));
    titleRect.setRight(std::min(titleRect.right(), frameRect.right()));

    QPainterPath mask;
    mask.addRect(frameRect);

    QPainterPath notch;
    notch.addRect(titleRect);

    return mask.subtracted(notch);
}

void FreeCADStyle::drawGroupBoxLabel(
    QPainter* painter,
    const QStyleOptionGroupBox* option,
    const QWidget* widget
) const
{
    const StyleContext titleContext = contextOf(widget, option, StyleComponentElement::Title);
    const QRect labelRect = proxy()->subControlRect(CC_GroupBox, option, SC_GroupBoxLabel, widget);

    // A copy rather than a pen, so a theme that defines no title colour keeps Qt's own
    // disabled-group handling. QGroupBox fills option->textColor from SH_GroupBox_TextLabelColor
    // whenever the palette carries no explicit WindowText brush, so it outranks the palette.
    QPalette palette = option->palette;
    if (const auto colour = resolve<Base::Color>(titleContext, StyleProperty::TextColor)) {
        palette.setColor(QPalette::WindowText, colour->asValue<QColor>());
    }
    else if (option->textColor.isValid()) {
        palette.setColor(QPalette::WindowText, option->textColor);
    }

    painter->save();
    painter->setFont(groupBoxTitleFont(option, widget));
    proxy()->drawItemText(
        painter,
        labelRect,
        Qt::AlignVCenter | option->textAlignment | mnemonicTextFlags(option, widget),
        palette,
        option->state & State_Enabled,
        option->text,
        QPalette::WindowText
    );
    painter->restore();

    // A checkable group box takes strong focus, and the label is the only part of it that can
    // show which one holds it.
    if (option->state & State_HasFocus) {
        QStyleOptionFocusRect focus;
        focus.QStyleOption::operator=(*option);
        focus.rect = labelRect;

        proxy()->drawPrimitive(PE_FrameFocusRect, &focus, painter, widget);
    }
}

void FreeCADStyle::drawGroupBox(
    QPainter* painter,
    const QStyleOptionGroupBox* option,
    const QWidget* widget
) const
{
    if (option->subControls & SC_GroupBoxFrame) {
        const QRect frameRect = proxy()->subControlRect(CC_GroupBox, option, SC_GroupBoxFrame, widget);

        drawBoxBackground(
            painter,
            frameRect,
            resolveBoxStyle(contextOf(widget, option)),
            groupBoxBorderMask(option, widget, frameRect)
        );
    }

    if (option->subControls & SC_GroupBoxCheckBox) {
        QStyleOptionButton indicator;
        indicator.QStyleOption::operator=(*option);
        indicator.rect = proxy()->subControlRect(CC_GroupBox, option, SC_GroupBoxCheckBox, widget);

        proxy()->drawPrimitive(PE_IndicatorCheckBox, &indicator, painter, widget);
    }

    if (option->subControls & SC_GroupBoxLabel) {
        drawGroupBoxLabel(painter, option, widget);
    }
}

void FreeCADStyle::drawComplexControl(
    ComplexControl control,
    const QStyleOptionComplex* option,
    QPainter* painter,
    const QWidget* widget
) const
{
    if (control == CC_SpinBox) {
        if (const auto* opt = qstyleoption_cast<const QStyleOptionSpinBox*>(option)) {
            drawSpinBox(opt, painter, widget);
            return;
        }
    }
    if (control == CC_ComboBox) {
        if (const auto* opt = qstyleoption_cast<const QStyleOptionComboBox*>(option)) {
            drawComboBox(opt, painter, widget);
            return;
        }
    }
    if (control == CC_ToolButton) {
        if (const auto* opt = qstyleoption_cast<const QStyleOptionToolButton*>(option)) {
            drawToolButton(opt, painter, widget);
            return;
        }
    }
    if (control == CC_GroupBox) {
        if (const auto* opt = qstyleoption_cast<const QStyleOptionGroupBox*>(option)) {
            drawGroupBox(painter, opt, widget);
            return;
        }
    }
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

void FreeCADStyle::drawControl(
    ControlElement element,
    const QStyleOption* option,
    QPainter* painter,
    const QWidget* widget
) const
{
    if (element == CE_PushButton || element == CE_PushButtonBevel) {
        if (auto btnOpt = qstyleoption_cast<const QStyleOptionButton*>(option)) {
            QStyleOptionButton modified = *btnOpt;
            modified.features &= ~QStyleOptionButton::Flat;
            QProxyStyle::drawControl(element, &modified, painter, widget);
            return;
        }
    }

    if (element == CE_PushButtonLabel) {
        if (const auto* btnOption = qstyleoption_cast<const QStyleOptionButton*>(option)) {
            drawPushButtonLabel(painter, btnOption, widget);
            return;
        }
    }

    if (element == CE_ToolButtonLabel) {
        if (const auto* tbOption = qstyleoption_cast<const QStyleOptionToolButton*>(option)) {
            drawToolButtonLabel(painter, tbOption, widget);
            return;
        }
    }

    if (element == CE_ComboBoxLabel) {
        if (const auto* comboOption = qstyleoption_cast<const QStyleOptionComboBox*>(option)) {
            drawComboBoxLabel(painter, comboOption, widget);
            return;
        }
    }

    if (element == CE_TabBarTabShape) {
        if (const auto* tabOption = qstyleoption_cast<const QStyleOptionTab*>(option)) {
            drawTabBarTab(painter, tabOption, widget);
            return;
        }
    }

    if (element == CE_TabBarTabLabel) {
        if (const auto* tabOption = qstyleoption_cast<const QStyleOptionTab*>(option)) {
            drawTabBarTabLabel(painter, tabOption, widget);
            return;
        }
    }

    if (element == CE_ShapedFrame) {
        if (const auto* frameOption = qstyleoption_cast<const QStyleOptionFrame*>(option)) {
            const QFrame::Shape shape = frameOption->frameShape;

            if (shape == QFrame::HLine || shape == QFrame::VLine) {
                drawSeparatorLine(painter, option->rect, shape == QFrame::HLine);
                return;
            }

            if (shape == QFrame::StyledPanel || shape == QFrame::Panel) {
                drawComponent(painter, option->rect, contextOf(widget, option));
                return;
            }
        }
    }

    if (element == CE_ToolBar) {
        const StyleContext context = contextOf(widget, option);
        drawBoxBackground(painter, option->rect, resolveBoxStyle(context));
        return;
    }

    if (element == CE_MenuBarEmptyArea) {
        // Draw the bar background for the empty area of the menu bar. Qt sets the painter
        // clip to the region not occupied by items before calling this, so we simply draw
        // on option->rect and let the clip restrict painting to the empty portion only.
        // Items draw their own portion of the bar background inside CE_MenuBarItem.
        const StyleContext barContext = contextOf(widget, option);
        drawBoxBackground(painter, option->rect, resolveBoxStyle(barContext));
        return;
    }

    if (element == CE_MenuBarItem) {
        if (const auto* menuOption = qstyleoption_cast<const QStyleOptionMenuItem*>(option)) {
            drawMenuBarItem(painter, menuOption, widget);
            return;
        }
    }

    if (element == CE_ItemViewItem) {
        if (const auto* vopt = qstyleoption_cast<const QStyleOptionViewItem*>(option)) {
            // Patch HighlightedText so QCommonStyle uses the token-defined text color for
            // selected items rather than the palette's default (typically white/near-white).
            const StyleContext context = contextOf(widget, option, StyleComponentElement::Item);
            QStyleOptionViewItem adjusted = *vopt;
            if (const auto textColor = resolve<Base::Color>(context, StyleProperty::TextColor)) {
                adjusted.palette.setColor(
                    QPalette::All,
                    QPalette::HighlightedText,
                    textColor->asValue<QColor>()
                );
            }
            QProxyStyle::drawControl(CE_ItemViewItem, &adjusted, painter, widget);
            return;
        }
    }

    // CE_HeaderSection is the actual background element called when QStyleSheetStyle
    // intercepts CE_Header and decomposes it via QCommonStyle's
    // proxy()->drawControl(CE_HeaderSection). CE_Header is also handled for direct callers (Qt
    // versions that skip QStyleSheetStyle).
    if (element == CE_HeaderSection || element == CE_Header) {
        if (const auto* headerOption = qstyleoption_cast<const QStyleOptionHeader*>(option)) {
            drawHeaderSection(painter, headerOption, widget);

            if (element == CE_Header) {
                // Delegate label (text, icon, sort indicator) to the base style.
                // Patch the palette so ButtonText picks up the token text color.
                const StyleContext itemContext = contextOf(widget, option, StyleComponentElement::Item);
                QStyleOptionHeader adjusted = *headerOption;
                if (const auto textColor = resolve<Base::Color>(itemContext, StyleProperty::TextColor)) {
                    adjusted.palette
                        .setColor(QPalette::All, QPalette::ButtonText, textColor->asValue<QColor>());
                }
                QProxyStyle::drawControl(CE_HeaderLabel, &adjusted, painter, widget);
            }

            return;
        }
    }

    QProxyStyle::drawControl(element, option, painter, widget);
}

void FreeCADStyle::drawToolButtonLabel(
    QPainter* painter,
    const QStyleOptionToolButton* option,
    const QWidget* widget
) const
{
    const StyleContext context = contextOf(widget, option);
    const BoxGeometryDefinition geometry = resolveBoxGeometry(context);
    const QRect contentRect = geometry.contentRect(option->rect);

    const Qt::ToolButtonStyle tbStyle = option->toolButtonStyle;
    const bool hasIconOrArrow = !option->icon.isNull() || option->arrowType != Qt::NoArrow;
    const bool hasText = hasIconOrArrow && !option->text.isEmpty()
        && (tbStyle == Qt::ToolButtonTextBesideIcon || tbStyle == Qt::ToolButtonTextUnderIcon);

    if (!hasText) {
        // Icon-only with a real (non-arrow) icon: draw it ourselves so the
        // token-based icon color is applied. Text-only, arrow-only, and
        // ToolButtonTextOnly always delegate — we have nothing to colour there.
        const bool hasRealIcon = !option->icon.isNull() && option->arrowType == Qt::NoArrow
            && tbStyle != Qt::ToolButtonTextOnly;
        if (!hasRealIcon) {
            QProxyStyle::drawControl(CE_ToolButtonLabel, option, painter, widget);
            return;
        }

        // Prefer the padded content rect, but where the padding leaves too little room for
        // the icon on an axis, ignore the padding there (grow back toward the full button
        // rect) so a short button shows the icon at size instead of squashing it.
        QRect iconRect = contentRect;
        if (iconRect.width() < option->iconSize.width()) {
            iconRect.setLeft(option->rect.left());
            iconRect.setWidth(option->rect.width());
        }
        if (iconRect.height() < option->iconSize.height()) {
            iconRect.setTop(option->rect.top());
            iconRect.setHeight(option->rect.height());
        }
        iconRect = applyButtonShift(iconRect, option, widget);

        // Shrink to the available room preserving aspect ratio; never distort or upscale.
        QSize iconSize = option->iconSize;
        if (iconSize.width() > iconRect.width() || iconSize.height() > iconRect.height()) {
            iconSize.scale(iconRect.size(), Qt::KeepAspectRatio);
        }

        const QPixmap pixmap = renderStyledIcon(painter, option->icon, iconSize, option, context);
        if (!pixmap.isNull()) {
            proxy()->drawItemPixmap(painter, iconRect, Qt::AlignCenter, pixmap);
        }
        return;
    }

    const int iconSpacing = geometry.iconSpacing;

    const QRect shiftedContentRect = applyButtonShift(contentRect, option, widget);

    const bool hasArrow = option->arrowType != Qt::NoArrow;

    QPixmap pixmap;
    QSize pixmapSize = option->iconSize;
    if (!hasArrow && !option->icon.isNull()) {
        pixmap = renderStyledIcon(
            painter,
            option->icon,
            shiftedContentRect.size().boundedTo(option->iconSize),
            option,
            context
        );
        pixmapSize = pixmap.size() / painter->device()->devicePixelRatio();
    }

    const auto drawArrowInRect = [&](const QRect& arrowRect) {
        QStyleOption arrowOpt(*option);
        arrowOpt.rect = arrowRect;
        PrimitiveElement primitive = PE_IndicatorArrowDown;
        switch (option->arrowType) {
            case Qt::LeftArrow:
                primitive = PE_IndicatorArrowLeft;
                break;
            case Qt::RightArrow:
                primitive = PE_IndicatorArrowRight;
                break;
            case Qt::UpArrow:
                primitive = PE_IndicatorArrowUp;
                break;
            default:
                break;
        }
        proxy()->drawPrimitive(primitive, &arrowOpt, painter, widget);
    };

    const int textFlags = mnemonicTextFlags(option, widget);

    painter->save();
    painter->setFont(option->font);

    if (tbStyle == Qt::ToolButtonTextBesideIcon) {
        const QRect iconRect(
            shiftedContentRect.left(),
            shiftedContentRect.top() + (shiftedContentRect.height() - pixmapSize.height()) / 2,
            pixmapSize.width(),
            pixmapSize.height()
        );
        const QRect textRect = shiftedContentRect.adjusted(pixmapSize.width() + iconSpacing, 0, 0, 0);

        if (hasArrow) {
            drawArrowInRect(iconRect);
        }
        else {
            proxy()->drawItemPixmap(painter, iconRect, Qt::AlignCenter, pixmap);
        }
        proxy()->drawItemText(
            painter,
            QStyle::visualRect(option->direction, shiftedContentRect, textRect),
            textFlags | Qt::AlignLeft | Qt::AlignVCenter,
            option->palette,
            option->state & State_Enabled,
            option->text,
            QPalette::ButtonText
        );
    }
    else {
        // Qt::ToolButtonTextUnderIcon
        const int fontHeight = option->fontMetrics.height();
        const QRect iconRect = shiftedContentRect.adjusted(0, 0, 0, -(fontHeight + iconSpacing));
        const QRect textRect(
            shiftedContentRect.left(),
            iconRect.bottom() + 1 + iconSpacing,
            shiftedContentRect.width(),
            fontHeight
        );

        if (hasArrow) {
            drawArrowInRect(iconRect);
        }
        else {
            proxy()->drawItemPixmap(painter, iconRect, Qt::AlignCenter, pixmap);
        }
        proxy()->drawItemText(
            painter,
            QStyle::visualRect(option->direction, shiftedContentRect, textRect),
            textFlags | Qt::AlignHCenter | Qt::AlignTop,
            option->palette,
            option->state & State_Enabled,
            option->text,
            QPalette::ButtonText
        );
    }

    painter->restore();
}

void FreeCADStyle::drawPushButtonLabel(
    QPainter* painter,
    const QStyleOptionButton* option,
    const QWidget* widget
) const
{
    const StyleContext context = contextOf(widget, option);
    const BoxGeometryDefinition geometry = resolveBoxGeometry(context);

    // option->rect at this point is SE_PushButtonContents from Fusion (inset by its own frame
    // width), which doesn't reflect our token-based padding. Use widget->rect() — the true
    // button rect — as the base, then apply token padding to derive the content area.
    // This is consistent with CT_PushButton in sizeFromContents, which also computes the total
    // size as content + token padding (not Fusion's frame).
    const QRect buttonRect = widget ? widget->rect() : option->rect;
    const QRect contentRect = geometry.contentRect(buttonRect);

    // For icon-only or text-only, delegate to parent with the token-padded content rect.
    // The parent centers the content within this rect; press-state shift is left to the parent.
    if (option->icon.isNull() || option->text.isEmpty()) {
        QStyleOptionButton adjustedOption = *option;
        adjustedOption.rect = contentRect;
        QProxyStyle::drawControl(CE_PushButtonLabel, &adjustedOption, painter, widget);
        return;
    }

    // Icon + text: custom layout with token icon spacing.
    const QRect shiftedContentRect = applyButtonShift(contentRect, option, widget);
    const int iconSpacing = geometry.iconSpacing;

    const QPixmap pixmap = renderStyledIcon(
        painter,
        option->icon,
        shiftedContentRect.size().boundedTo(option->iconSize),
        option,
        context
    );
    const QSize pixmapSize = pixmap.size() / painter->device()->devicePixelRatio();

    // Center the icon+text group horizontally in the content rect.
    const int textWidth = option->fontMetrics.horizontalAdvance(option->text);
    const int groupWidth = pixmapSize.width() + iconSpacing + textWidth;
    const int groupLeft = shiftedContentRect.left() + (shiftedContentRect.width() - groupWidth) / 2;

    const int textLeft = groupLeft + pixmapSize.width() + iconSpacing;
    const QRect iconRect(
        groupLeft,
        shiftedContentRect.top() + (shiftedContentRect.height() - pixmapSize.height()) / 2,
        pixmapSize.width(),
        pixmapSize.height()
    );
    const QRect textRect(
        textLeft,
        shiftedContentRect.top(),
        shiftedContentRect.right() - textLeft,
        shiftedContentRect.height()
    );

    const int textFlags = mnemonicTextFlags(option, widget) | Qt::AlignVCenter | Qt::AlignLeft;

    painter->save();
    proxy()->drawItemPixmap(painter, iconRect, Qt::AlignCenter, pixmap);
    proxy()->drawItemText(
        painter,
        QStyle::visualRect(option->direction, shiftedContentRect, textRect),
        textFlags,
        option->palette,
        option->state & State_Enabled,
        option->text,
        QPalette::ButtonText
    );
    painter->restore();
}

void FreeCADStyle::drawComboBoxLabel(
    QPainter* painter,
    const QStyleOptionComboBox* option,
    const QWidget* widget
) const
{
    // For editable combos, the text is drawn by the embedded QLineEdit and the
    // icon–QLineEdit gap is hardcoded inside QComboBoxPrivate::updateLineEditGeometry()
    // (not overridable from a style), so delegate unchanged.
    if (option->editable) {
        QProxyStyle::drawControl(CE_ComboBoxLabel, option, painter, widget);
        return;
    }

    const QRect editFieldRect
        = proxy()->subControlRect(CC_ComboBox, option, SC_ComboBoxEditField, widget);

    // Icon-only or text-only: delegate to parent unchanged — Qt's CE_ComboBoxLabel
    // calls subControlRect(SC_ComboBoxEditField) internally, so it already uses our
    // overridden rect.  Replacing option->rect here would cause double-padding.
    if (option->currentIcon.isNull() || option->currentText.isEmpty()) {
        QProxyStyle::drawControl(CE_ComboBoxLabel, option, painter, widget);
        return;
    }

    const BoxGeometryDefinition geometry = resolveBoxGeometry(contextOf(widget, option));
    const int iconSpacing = geometry.iconSpacing;

    const QIcon::Mode iconMode = (option->state & State_Enabled) ? QIcon::Normal : QIcon::Disabled;
    const QPixmap pixmap = option->currentIcon.pixmap(
        editFieldRect.size().boundedTo(option->iconSize),
        painter->device()->devicePixelRatio(),
        iconMode,
        QIcon::Off
    );
    const QSize pixmapSize = pixmap.size() / painter->device()->devicePixelRatio();

    const QRect iconRect(
        editFieldRect.left(),
        editFieldRect.top() + (editFieldRect.height() - pixmapSize.height()) / 2,
        pixmapSize.width(),
        pixmapSize.height()
    );
    const QRect textRect(
        editFieldRect.left() + pixmapSize.width() + iconSpacing,
        editFieldRect.top(),
        editFieldRect.width() - pixmapSize.width() - iconSpacing,
        editFieldRect.height()
    );

    const int textFlags = mnemonicTextFlags(option, widget) | Qt::AlignVCenter | Qt::AlignLeft;

    painter->save();
    painter->setClipRect(editFieldRect);
    proxy()->drawItemPixmap(painter, iconRect, Qt::AlignCenter, pixmap);
    proxy()->drawItemText(
        painter,
        QStyle::visualRect(option->direction, editFieldRect, textRect),
        textFlags,
        option->palette,
        option->state & State_Enabled,
        option->currentText,
        QPalette::ButtonText
    );
    painter->restore();
}

void FreeCADStyle::drawTabBarTab(QPainter* painter, const QStyleOptionTab* option, const QWidget* widget) const
{
    const Position position = tabPositionOf(option->shape);
    const StyleContext positionContext = contextOf(widget, option, StyleComponentElement::Tab);
    const int tabOverlap = tabOverlapOf(option, widget);
    const bool isVertical = (position == Position::East || position == Position::West);

    drawBoxBackground(
        painter,
        tabVisualRect(option->rect, tabOverlap, isVertical),
        resolveBoxStyle(positionContext)
    );
}

void FreeCADStyle::drawTabBarTabLabel(
    QPainter* painter,
    const QStyleOptionTab* option,
    const QWidget* widget
) const
{
    const Position position = tabPositionOf(option->shape);
    const bool isVertical = (position == Position::East || position == Position::West);

    const bool hasIcon = !option->icon.isNull();
    const bool hasText = !option->text.isEmpty();

    const StyleContext tabContext = contextOf(widget, option, StyleComponentElement::Tab);

    // The background is drawn on a rect shrunk by |tabOverlap| on the trailing edge (see
    // drawTabBarTab). To keep content padding symmetric relative to the visible background,
    // base all content geometry on the same visual rect.
    const QRect visualRect = tabVisualRect(option->rect, tabOverlapOf(option, widget), isVertical);

    // For vertical tabs or non-icon+text tabs, delegate to parent. Apply token text color by
    // setting palette ButtonText so Qt's draw path picks it up automatically. Use visualRect so
    // Qt's tabLayout sees the same bounds as the background.
    if (isVertical || !hasIcon || !hasText) {
        QStyleOptionTab adjusted = *option;
        adjusted.rect = visualRect;
        if (const auto color = resolve<Base::Color>(tabContext, StyleProperty::TextColor)) {
            adjusted.palette.setColor(QPalette::All, QPalette::ButtonText, color->asValue<QColor>());
        }
        QProxyStyle::drawControl(CE_TabBarTabLabel, &adjusted, painter, widget);
        return;
    }

    // Geometry is always resolved in the canonical North context (PM_TabBarTabHSpace/VSpace does
    // the same: the tab size is computed in North space and transposed by QTabBar for East/West).
    const BoxGeometryDefinition geometry = resolveBoxGeometry(withNorthPosition(tabContext));

    const QRect contentRect = geometry.contentRect(visualRect);

    const QPixmap pixmap
        = renderStyledIcon(painter, option->icon, option->iconSize, option, tabContext);
    const QSize pixmapSize = pixmap.size() / painter->device()->devicePixelRatio();

    const int iconSpacing = geometry.iconSpacing;

    const QRect iconRect(
        contentRect.left(),
        contentRect.top() + (contentRect.height() - pixmapSize.height()) / 2,
        pixmapSize.width(),
        pixmapSize.height()
    );
    const QRect textRect = contentRect.adjusted(pixmapSize.width() + iconSpacing, 0, 0, 0);

    const int textFlags = mnemonicTextFlags(option, widget);

    painter->save();

    QPalette::ColorRole textRole = QPalette::ButtonText;
    if (const auto color = resolve<Base::Color>(tabContext, StyleProperty::TextColor)) {
        painter->setPen(color->asValue<QColor>());
        textRole = QPalette::NoRole;
    }

    proxy()->drawItemPixmap(painter, iconRect, Qt::AlignCenter, pixmap);
    proxy()->drawItemText(
        painter,
        QStyle::visualRect(option->direction, contentRect, textRect),
        textFlags | Qt::AlignLeft | Qt::AlignVCenter,
        option->palette,
        option->state & State_Enabled,
        option->text,
        textRole
    );

    painter->restore();
}

void FreeCADStyle::drawMenuBarItem(
    QPainter* painter,
    const QStyleOptionMenuItem* option,
    const QWidget* widget
) const
{
    const StyleContext itemContext = contextOf(widget, option, StyleComponentElement::Item);
    const BoxGeometryDefinition geometry = resolveBoxGeometry(itemContext);

    const QRect contentRect = geometry.contentRect(option->rect);

    drawBoxBackground(painter, geometry.borderRect(option->rect), resolveBoxStyle(itemContext));

    painter->save();

    if (const auto textColor = resolve<Base::Color>(itemContext, StyleProperty::TextColor)) {
        painter->setPen(textColor->asValue<QColor>());
    }
    else {
        painter->setPen(option->palette.buttonText().color());
    }

    constexpr int textFlags = Qt::AlignCenter | Qt::TextShowMnemonic | Qt::TextDontClip;
    painter->drawText(contentRect, textFlags, option->text);

    painter->restore();
}

void FreeCADStyle::drawHeaderSection(
    QPainter* painter,
    const QStyleOptionHeader* option,
    const QWidget* widget
) const
{
    const StyleContext itemContext = contextOf(widget, option, StyleComponentElement::Item);
    drawBoxBackground(painter, option->rect, resolveBoxStyle(itemContext));
}

void FreeCADStyle::drawTabBarBase(
    QPainter* painter,
    const QStyleOptionTabBarBase* option,
    const QWidget* widget
) const
{
    const StyleContext positionContext = contextOf(widget, option, StyleComponentElement::Base);
    drawBoxBackground(painter, option->rect, resolveBoxStyle(positionContext));
}

void FreeCADStyle::drawTabWidgetFrame(
    QPainter* painter,
    const QStyleOptionTabWidgetFrame* option,
    const QWidget* widget
) const
{
    const Position position = tabPositionOf(option->shape);

    // Draw the pane frame using design-system tokens.
    StyleContext paneContext;
    paneContext.component = StyleComponent::TabWidget;
    paneContext.element = StyleComponentElement::Root;
    bindWidget(paneContext, widget);
    drawBoxBackground(painter, option->rect, resolveBoxStyle(paneContext));

    // Draw the shadow strip at the attachment edge.
    // Build context manually — contextOf() requires a QTabBar widget to produce the
    // TabBar component; here the widget is QTabWidget.
    StyleContext stripContext;
    stripContext.component = StyleComponent::TabBar;
    stripContext.element = StyleComponentElement::Base;
    stripContext.variant.set(VariantSlot::Position, position);
    const auto* tabWidget = qobject_cast<const QTabWidget*>(widget);
    bindWidget(stripContext, tabWidget != nullptr ? tabWidget->tabBar() : nullptr);

    const int stripHeight = resolve<int>(stripContext, StyleProperty::Height).value_or(0);
    if (stripHeight == 0) {
        return;
    }

    const QRect& rect = option->rect;
    // clang-format off
    const QRect stripRect = [&]() -> QRect {
        switch (position) {
            case Position::South: return {rect.left(), rect.bottom() + 1, rect.width(), stripHeight};
            case Position::East:  return {rect.right() + 1, rect.top(), stripHeight, rect.height()};
            case Position::West:  return {rect.left() - stripHeight, rect.top(), stripHeight, rect.height()};
            default:              return {rect.left(), rect.top() - stripHeight, rect.width(), stripHeight};
        }
    }();
    // clang-format on

    BoxStyleDefinition stripStyle = resolveBoxStyle(stripContext);
    // The pane box already draws the border; suppress the strip's own border to avoid doubling.
    stripStyle.borderColor = std::nullopt;
    stripStyle.borderThickness = std::nullopt;

    drawBoxBackground(painter, stripRect, stripStyle);
}

std::optional<Value> FreeCADStyle::resolve(std::string_view name) const
{
    return Application::Instance->styleParameterManager()->resolve(std::string(name));
}

std::optional<Value> FreeCADStyle::resolve(std::initializer_list<std::string_view> names) const
{
    for (const std::string_view name : names) {
        if (auto value = resolve(name)) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<Value> FreeCADStyle::resolve(
    std::initializer_list<std::string_view> prefixes,
    std::string_view suffix
) const
{
    for (const std::string_view prefix : prefixes) {
        if (auto value = resolve(std::string(prefix) + std::string(suffix))) {
            return value;
        }
    }
    return std::nullopt;
}

// ─── Drawing helpers ─────────────────────────────────────────────────────────

QColor FreeCADStyle::resolveIconColor(const StyleContext& context, const QPalette& palette) const
{
    if (const auto color = resolve<Base::Color>(context, StyleProperty::IconColor)) {
        return color->asValue<QColor>();
    }
    if (const auto color = resolve<Base::Color>(context, StyleProperty::TextColor)) {
        return color->asValue<QColor>();
    }
    return palette.buttonText().color();
}

QPixmap FreeCADStyle::renderStyledIcon(
    QPainter* painter,
    const QIcon& icon,
    const QSize& maxSize,
    QIcon::Mode mode,
    QIcon::State state,
    const StyleContext& context,
    const QPalette& palette
) const
{
    return IconManager::instance().render(
        icon,
        {
            .size = maxSize,
            .dpr = painter->device()->devicePixelRatio(),
            .color = resolveIconColor(context, palette),
            .mode = mode,
            .state = state,
        }
    );
}

QPixmap FreeCADStyle::renderStyledIcon(
    QPainter* painter,
    const QIcon& icon,
    const QSize& maxSize,
    const QStyleOption* option,
    const StyleContext& context
) const
{
    return renderStyledIcon(
        painter,
        icon,
        maxSize,
        iconModeOf(option),
        iconStateOf(option),
        context,
        option->palette
    );
}

int FreeCADStyle::mnemonicTextFlags(const QStyleOption* option, const QWidget* widget) const
{
    int flags = Qt::TextShowMnemonic;
    if (!proxy()->styleHint(SH_UnderlineShortcut, option, widget)) {
        flags |= Qt::TextHideMnemonic;
    }
    return flags;
}

QRect FreeCADStyle::applyButtonShift(
    const QRect& rect,
    const QStyleOption* option,
    const QWidget* widget
) const
{
    if (!(option->state & (State_Sunken | State_On))) {
        return rect;
    }
    QRect shifted = rect;
    shifted.translate(
        proxy()->pixelMetric(PM_ButtonShiftHorizontal, option, widget),
        proxy()->pixelMetric(PM_ButtonShiftVertical, option, widget)
    );
    return shifted;
}

int FreeCADStyle::tabOverlapOf(const QStyleOptionTab* option, const QWidget* widget) const
{
    const bool isLastOrOnly = option->position == QStyleOptionTab::End
        || option->position == QStyleOptionTab::OnlyOneTab;
    return isLastOrOnly ? 0 : proxy()->pixelMetric(PM_TabBarTabOverlap, option, widget);
}

QRect FreeCADStyle::tabVisualRect(const QRect& rect, int tabOverlap, bool isVertical)
{
    if (tabOverlap == 0) {
        return rect;
    }
    if (isVertical) {
        return rect.adjusted(0, 0, 0, tabOverlap);
    }
    return rect.adjusted(0, 0, tabOverlap, 0);
}


void FreeCADStyle::drawComponent(QPainter* painter, const QRect& rect, const StyleContext& context) const
{
    drawBoxBackground(painter, rect, resolveBoxStyle(context));
}

void FreeCADStyle::drawComponent(
    QPainter* painter,
    const QRect& rect,
    const QWidget* widget,
    const QStyleOption* option
) const
{
    drawComponent(painter, rect, contextOf(widget, option));
}

void FreeCADStyle::drawSeparatorLine(QPainter* painter, const QRect& rect, bool isHorizontal) const
{
    int thickness = 1;
    if (const auto numeric = resolve<Numeric>("SeparatorThickness")) {
        thickness = static_cast<int>(*numeric);
    }
    if (const auto color = resolve<Base::Color>("SeparatorColor")) {
        const QRect lineRect = isHorizontal
            ? QRect(rect.left(), rect.center().y() - (thickness / 2), rect.width(), thickness)
            : QRect(rect.center().x() - (thickness / 2), rect.top(), thickness, rect.height());
        painter->fillRect(lineRect, color->asValue<QColor>());
    }
}

void FreeCADStyle::updateScrollAreaMask(QAbstractScrollArea* scrollArea) const
{
    if (scrollArea->size().isEmpty()) {
        return;
    }

    const StyleContext context = contextOf(scrollArea, nullptr);
    const BoxStyleDefinition boxStyle = resolveBoxStyle(context);

    CornerRadii outerRadii = boxStyle.borderRadius.resolve(scrollArea->size());
    outerRadii.setBottom(0);

    if (!outerRadii.isRounded()) {
        scrollArea->clearMask();
        return;
    }

    // Clip the scroll area to its outer border-radius so the square widget corners
    // are not visible at the compositor level.
    QBitmap bitmap(scrollArea->size());
    bitmap.fill(Qt::color0);
    {
        QPainter maskPainter(&bitmap);
        maskPainter.fillPath(roundedRectPath(QRectF(scrollArea->rect()), outerRadii), Qt::color1);
    }
    scrollArea->setMask(bitmap);
}

void FreeCADStyle::polish(QWidget* widget)
{
    QProxyStyle::polish(widget);

    if (widget == nullptr) {
        return;
    }

    // Overrides are inherited down the widget tree. computeOverrideSet() walks up from this
    // widget rather than seeding from the parent's stored id, so a widget polished before its
    // parent still lands on the right set. Only this widget is tagged here: QWidget::
    // ensurePolished() already walks descendants and polishes each in turn.
    //
    // This must run before tagWidgetTransparency() below: that call can synchronously dispatch
    // QEvent::StyleChange to this widget, and a handler reacting to it (QTabBar::changeEvent,
    // for one) resolves tokens for this same widget before polish() returns. The override id
    // has to already be in place when that happens, or the handler runs under the id this
    // widget had before this polish — stale metrics until the next style event. Nothing in the
    // transparency block below depends on this widget's own override id: it resolves only
    // against widget->parentWidget().
    storeOverrideSet(widget, computeOverrideSet(widget));

    // Transparency is inherited down the widget tree. Seeding from the parent here covers
    // widgets built after their parent's subtree was propagated — lazily created editors,
    // popups and the like — without an extra event filter. QWidget::ensurePolished() already
    // walks descendants top-down and polishes each in turn, so tag only this widget here;
    // recursing would redo Qt's own walk and turn total work into Σ(subtree) instead of N.
    // ownSurface()/canInheritTransparency() are the same rule updateTransparency() uses, so an
    // explicit override or a window boundary is honoured identically through either path.
    const bool inherited = canInheritTransparency(widget)
        && transparencyBelow(widget->parentWidget());
    tagWidgetTransparency(widget, ownSurface(widget, inherited));

    if (qobject_cast<QTabBar*>(widget)) {
        widget->setMouseTracking(true);
        widget->installEventFilter(this);
    }

    if (const auto expressionButton = qobject_cast<ExpressionButton*>(widget)) {
        expressionButton->setNormalIcon(
            IconManager::instance().icon(":/icons/bound-expression-symbol.svg")
        );
    }

    if (auto* comboBox = qobject_cast<QComboBox*>(widget)) {
        constrainComboDropdown(comboBox);
    }

    if (qobject_cast<TaskView::TaskBox*>(widget)) {
        widget->setAutoFillBackground(false);
    }

    if (qobject_cast<QMdiSubWindow*>(widget)) {
        // The subwindow is the surface its view is painted on, and a view paints only its own
        // chrome — the start page, for one, leaves the area around its lists bare. An opaque
        // fill here is what lets the backing store clip the subwindows stacked underneath;
        // without it their last paint stays visible through every such gap.
        widget->setAutoFillBackground(true);
    }

    if (auto* itemView = qobject_cast<QAbstractItemView*>(widget)) {
        itemView->setAttribute(Qt::WA_MouseTracking);
    }

    if (auto* scrollArea = qobject_cast<QAbstractScrollArea*>(widget)) {
        auto viewport = scrollArea->viewport();

        if (!viewport) {
            return;
        }

        // A QMdiArea is a scroll area whose viewport children are the subwindows themselves.
        // Stripping their backgrounds the way we strip a viewport's would leave every view in
        // the workspace see-through, and the style paints no panel here to take their place.
        if (qobject_cast<QMdiArea*>(scrollArea)) {
            return;
        }

        scrollArea->removeEventFilter(this);
        scrollArea->installEventFilter(this);

        const auto disableDefaultBackground = [](QWidget* widget) {
            widget->setAttribute(Qt::WA_NoSystemBackground);
            widget->setAutoFillBackground(false);
        };

        disableDefaultBackground(viewport);
        std::ranges::for_each(
            viewport->findChildren<QWidget*>(Qt::FindDirectChildrenOnly),
            disableDefaultBackground
        );

        updateScrollAreaMask(scrollArea);
    }
}

void FreeCADStyle::constrainComboDropdown(QComboBox* comboBox)
{
    auto* listView = qobject_cast<QListView*>(comboBox->view());
    if (!listView) {
        return;
    }
    listView->setProperty(comboDropdownProperty, true);
    listView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // The popup list belongs to Qt, so a caller that needs its own dropdown metrics names the
    // component on the combo box and it is carried over here. The list then resolves against
    // that prefix ahead of DropdownList, which is how one dropdown takes a height of its own.
    if (const QVariant component = comboBox->property("dropdownComponent"); component.isValid()) {
        listView->setProperty("component", component);
    }

    QWidget* container = listView->parentWidget();
    if (!container) {
        return;
    }

    applyComboDropdownMaxHeight(listView);

    // Guard against double-installation on re-polish (e.g. theme change).
    if (!container->property(comboContainerProperty).toBool()) {
        container->setProperty(comboContainerProperty, true);
        container->installEventFilter(this);
    }
}

void FreeCADStyle::applyComboDropdownMaxHeight(QListView* listView) const
{
    QWidget* container = listView->parentWidget();
    if (!container) {
        return;
    }

    const BoxGeometryDefinition geometry = resolveBoxGeometry(contextOf(listView));

    // An absent MaxHeight — including one a theme cleared with reset() — means the dropdown is
    // bounded only by Qt, which keeps it on screen and honours maxVisibleItems. Qt's own
    // scroller buttons take over there, so they go back into service with it.
    const int maxHeight = geometry.maxHeight.value_or(QWIDGETSIZE_MAX);
    listView->setMaximumHeight(maxHeight);
    container->setMaximumHeight(maxHeight);

    if (geometry.maxHeight) {
        hideScrollerButtons(container);
    }
    else {
        restoreScrollerButtons(container);
    }
}

void FreeCADStyle::hideScrollerButtons(QWidget* container)
{
    for (auto* child : container->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
        if (strcmp(child->metaObject()->className(), "QComboBoxPrivateScroller") == 0) {
            child->setMaximumHeight(0);
        }
    }
}

void FreeCADStyle::restoreScrollerButtons(QWidget* container)
{
    for (auto* child : container->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
        if (strcmp(child->metaObject()->className(), "QComboBoxPrivateScroller") == 0) {
            child->setMaximumHeight(QWIDGETSIZE_MAX);
        }
    }
}

void FreeCADStyle::unpolish(QWidget* widget)
{
    if (widget == nullptr) {
        QProxyStyle::unpolish(widget);
        return;
    }

    if (qobject_cast<QTabBar*>(widget)) {
        widget->removeEventFilter(this);
    }
    if (auto* comboBox = qobject_cast<QComboBox*>(widget)) {
        restoreComboDropdownDefaults(comboBox);
    }
    if (auto* scrollArea = qobject_cast<QAbstractScrollArea*>(widget)) {
        scrollArea->removeEventFilter(this);
        scrollArea->clearMask();
    }

    // The id means nothing under another style, and leaving it behind would have that style's
    // widget resolve against a set it never asked for.
    storeOverrideSet(widget, StyleParameters::OverrideRegistry::emptyId);

    QProxyStyle::unpolish(widget);
}

void FreeCADStyle::restoreComboDropdownDefaults(QComboBox* comboBox)
{
    // Use findChildren instead of view() — calling view() lazily creates the container
    // for a combo that was never opened.
    for (auto* listView : comboBox->findChildren<QListView*>()) {
        if (!listView->property(comboDropdownProperty).toBool()) {
            continue;
        }
        listView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        listView->setMaximumHeight(QWIDGETSIZE_MAX);

        QWidget* container = listView->parentWidget();
        if (!container) {
            continue;
        }
        container->setMaximumHeight(QWIDGETSIZE_MAX);
        restoreScrollerButtons(container);
        container->removeEventFilter(this);
    }
}

void FreeCADStyle::correctComboPopupPlacement(QWidget* container)
{
    QWidget* comboBox = container->parentWidget();
    if (!comboBox) {
        return;
    }

    // Widen by the scrollbar width if the scrollbar appeared after Qt calculated the width.
    for (auto* child : container->findChildren<QListView*>()) {
        if (!child->property(comboDropdownProperty).toBool()) {
            continue;
        }
        const auto* vbar = child->verticalScrollBar();
        if (vbar && vbar->isVisible()) {
            container->resize(container->width() + vbar->width(), container->height());
        }
        break;
    }

    const QPoint comboScreenPos = comboBox->mapToGlobal(QPoint {});
    const int comboTopScreenY = comboScreenPos.y();
    const int comboBottomScreenY = comboTopScreenY + comboBox->height();
    const int containerTopScreenY = container->mapToGlobal(QPoint {}).y();
    const int containerHeight = container->height();

    if (containerTopScreenY >= comboTopScreenY) {
        return;  // popup is below — no correction needed
    }

    // Qt placed the popup above because the unconstrained sizeHint didn't fit below.
    // Check whether the constrained height actually fits below.
    const QScreen* screen = QGuiApplication::screenAt(comboScreenPos);
    const int screenBottom = screen ? screen->availableGeometry().bottom() : INT_MAX;

    if (comboBottomScreenY + containerHeight <= screenBottom) {
        const int delta = comboBottomScreenY - containerTopScreenY;
        container->move(container->pos() + QPoint(0, delta));
    }
    else {
        // Genuinely doesn't fit below — close the gap above.
        const int containerBottomScreenY = containerTopScreenY + containerHeight;
        if (containerBottomScreenY < comboTopScreenY) {
            const int delta = comboTopScreenY - containerBottomScreenY;
            container->move(container->pos() + QPoint(0, delta));
        }
    }
}

bool FreeCADStyle::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == ThemeReloadEvent::registeredType()) {
        clearTokenCache();

        // IsTransparent may resolve differently after a reload, so the tags the propagator
        // produced from the previous theme have to be recomputed before anything repaints.
        // Guard the list: updateTransparency() sends StyleChange synchronously and in-tree
        // handlers may destroy other top-level widgets while we are still walking them.
        QList<QPointer<QWidget>> topLevels;
        for (QWidget* topLevel : QApplication::topLevelWidgets()) {
            topLevels.append(topLevel);
        }
        for (const QPointer<QWidget>& topLevel : topLevels) {
            if (!topLevel.isNull()) {
                updateTransparency(topLevel, false);
            }
        }

        for (QWidget* widget : QApplication::allWidgets()) {
            widget->update();
        }
        return false;  // Let ThemeReloadHandler in Application also process the event
    }

    if (event->type() == QEvent::Polish) {
        resetTaskPanelMargins(obj);
    }

    forceTabBarRepaint(obj, event);

    if (event->type() == QEvent::Show) {
        scheduleComboPopupCorrection(obj);
    }

    if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
        if (auto* scrollArea = qobject_cast<QAbstractScrollArea*>(obj)) {
            updateScrollAreaMask(scrollArea);
        }
    }

    // QAbstractScrollArea::paintEvent is an empty stub in Qt 6 — the frame is never
    // drawn via CE_ShapedFrame. For non-item-view scroll areas (e.g. QScrollArea with
    // component="List") we must draw the token-based border/background ourselves.
    // Item views are handled via PE_Frame in their own paintEvent.
    if (event->type() == QEvent::Paint) {
        if (auto* scrollArea = qobject_cast<QAbstractScrollArea*>(obj)) {
            if (!qobject_cast<QAbstractItemView*>(scrollArea)) {
                QPainter painter(scrollArea);
                drawComponent(&painter, scrollArea->rect(), contextOf(scrollArea, nullptr));
            }
        }
    }

    return QObject::eventFilter(obj, event);
}

void FreeCADStyle::resetTaskPanelMargins(QObject* obj)
{
    if (auto* groupBox = qobject_cast<QGroupBox*>(obj)) {
        if (auto* layout = groupBox->layout()) {
            layout->setContentsMargins(0, 0, 0, 0);
        }
    }


    // Apply token padding to QTextEdit / QPlainTextEdit via the document margin.
    // This pads the text content relative to the viewport while leaving scrollbars
    // flush with the frame edge (unlike viewport-margin approaches).
    if (auto* textEdit = qobject_cast<QTextEdit*>(obj)) {
        applyTextEditDocumentPadding(textEdit, textEdit->document());
    }
    else if (auto* plainTextEdit = qobject_cast<QPlainTextEdit*>(obj)) {
        applyTextEditDocumentPadding(plainTextEdit, plainTextEdit->document());
    }
}

void FreeCADStyle::applyTextEditDocumentPadding(QWidget* widget, QTextDocument* document) const
{
    const StyleContext context = contextOf(widget);
    const BoxGeometryDefinition geometry = resolveBoxGeometry(context);
    document->setDocumentMargin(geometry.padding.left());
}

// Force tab bar repaint on mouse move/leave so our cursor-position hover check in
// contextOf() sees up-to-date state. Qt's internal WA_Hover tracking for QTabBar does
// not consistently trigger repaints in all configurations.
void FreeCADStyle::forceTabBarRepaint(QObject* obj, QEvent* event)
{
    if (!qobject_cast<QTabBar*>(obj)) {
        return;
    }

    if (event->type() == QEvent::MouseMove || event->type() == QEvent::Leave
        || event->type() == QEvent::HoverMove || event->type() == QEvent::HoverLeave) {
        static_cast<QWidget*>(obj)->update();
    }
}

void FreeCADStyle::scheduleComboPopupCorrection(QObject* obj)
{
    if (obj->property(comboContainerProperty).toBool()) {
        QPointer<QWidget> containerGuard = qobject_cast<QWidget*>(obj);
        // Defer so Qt finishes its own screen-edge clamping first.
        QTimer::singleShot(0, [this, containerGuard]() {
            if (containerGuard && containerGuard->isVisible()) {
                correctComboPopupPlacement(containerGuard);
            }
        });
    }
}

// ─── Directional rotation helpers ────────────────────────────────────────────

namespace
{

/**
 * @brief Applies the standard 4-way edge rotation to an array of values.
 *
 * Canonical (North) order: (left/topLeft, top/topRight, right/bottomRight, bottom/bottomLeft).
 * South swaps opposite pairs; East/West rotate by one step in either direction.
 */
template<typename T>
std::array<T, 4> rotate4(std::array<T, 4> values, Position position)
{
    // The array has known size - bounds are guaranteed
    // NOLINTBEGIN(*-pro-bounds-avoid-unchecked-container-access)
    // clang-format off
    switch (position) {
        case Position::South: return {values[2], values[3], values[0], values[1]};
        case Position::East:  return {values[3], values[0], values[1], values[2]};
        case Position::West:  return {values[1], values[2], values[3], values[0]};
        default:              return values;
    }
    // clang-format on
    // NOLINTEND(*-pro-bounds-avoid-unchecked-container-access)
}

/** @brief Rotates canonical (North) margins to the given position. */
QMarginsF rotated(const QMarginsF& margins, Position position)
{
    const auto [left, top, right, bottom] = rotate4(
        std::to_array({margins.left(), margins.top(), margins.right(), margins.bottom()}),
        position
    );
    return {left, top, right, bottom};
}

/** @brief Rotates canonical (North) corner radii to the given position. */
FreeCADStyle::CornerRadii rotated(const FreeCADStyle::CornerRadii& corners, Position position)
{
    const auto [topLeft, topRight, bottomRight, bottomLeft] = rotate4(
        std::to_array({corners.topLeft, corners.topRight, corners.bottomRight, corners.bottomLeft}),
        position
    );
    return {.topLeft = topLeft, .topRight = topRight, .bottomRight = bottomRight, .bottomLeft = bottomLeft};
}

/**
 * @brief Rotates a canonical (North, top→bottom) linear gradient brush to the given position.
 *
 * Point transform: North=(px,py), South=(px,1-py), East=(1-py,px), West=(py,1-px).
 * Non-linear-gradient brushes are returned unchanged.
 */
// clang-format off
QBrush rotated(const QBrush& brush, Position position)
{
    if (position == Position::North) {
        return brush;
    }
    const QGradient* gradient = brush.gradient();
    if (!gradient || gradient->type() != QGradient::LinearGradient) {
        return brush;
    }
    const auto* linear = static_cast<const QLinearGradient*>(gradient);
    const auto rotatePoint = [position](const QPointF& pointF) -> QPointF {
        switch (position) {
            case Position::South: return {pointF.x(),       1.0 - pointF.y()};
            case Position::East:  return {1.0 - pointF.y(), pointF.x()      };
            case Position::West:  return {pointF.y(),       1.0 - pointF.x()};
            default:              return pointF;
        }
    };

    QLinearGradient result(rotatePoint(linear->start()), rotatePoint(linear->finalStop()));
    result.setStops(linear->stops());
    result.setCoordinateMode(linear->coordinateMode());
    result.setSpread(linear->spread());

    return result;
}
// clang-format on

// ─── Color effect helpers ─────────────────────────────────────────────────────

/**
 * @brief Applies a ColorEffect to each color in a QBrush.
 *
 * Solid brushes are shifted directly. Gradient brushes have each stop color
 * shifted individually so the gradient shape is preserved.
 */
QBrush applyEffectToBrush(const QBrush& brush, const ColorEffect& effect)
{
    const auto applyToColor = [&](const QColor& color) -> QColor {
        return effect.apply(Base::Color::fromValue(color)).asValue<QColor>();
    };

    if (brush.style() == Qt::SolidPattern) {
        return QBrush(applyToColor(brush.color()));
    }

    if (const QGradient* gradient = brush.gradient()) {
        QGradientStops stops = gradient->stops();
        for (auto& [position, color] : stops) {
            color = applyToColor(color);
        }

        // clang-format off
        switch (gradient->type()) {
            case QGradient::LinearGradient: {
                const auto* linear = static_cast<const QLinearGradient*>(gradient);
                QLinearGradient result(linear->start(), linear->finalStop());
                result.setStops(stops);
                result.setSpread(gradient->spread());
                result.setCoordinateMode(gradient->coordinateMode());
                return result;
            }
            case QGradient::RadialGradient: {
                const auto* radial = static_cast<const QRadialGradient*>(gradient);
                QRadialGradient result(radial->center(), radial->radius(), radial->focalPoint());
                result.setStops(stops);
                result.setSpread(gradient->spread());
                result.setCoordinateMode(gradient->coordinateMode());
                return result;
            }
            case QGradient::ConicalGradient: {
                const auto* conical = static_cast<const QConicalGradient*>(gradient);
                QConicalGradient result(conical->center(), conical->angle());
                result.setStops(stops);
                result.setCoordinateMode(gradient->coordinateMode());
                return result;
            }
            default:
                break;
        }
        // clang-format on
    }

    return brush;  // unsupported brush type — return unchanged
}

}  // namespace

// ─── Tab position mapping ────────────────────────────────────────────────────

/**
 * @brief Maps a QTabBar::Shape to the canonical Position.
 *
 * Both Rounded and Triangular shapes at the same edge map to the same position.
 */
/*static*/ Position FreeCADStyle::tabPositionOf(QTabBar::Shape shape)
{
    switch (shape) {
        case QTabBar::RoundedNorth:
        case QTabBar::TriangularNorth:
            return Position::North;
        case QTabBar::RoundedEast:
        case QTabBar::TriangularEast:
            return Position::East;
        case QTabBar::RoundedSouth:
        case QTabBar::TriangularSouth:
            return Position::South;
        case QTabBar::RoundedWest:
        case QTabBar::TriangularWest:
            return Position::West;
        default:
            return Position::North;
    }
}

/**
 * @brief Maps a QToolBar's dock area to a Position for token resolution.
 *
 * Walks up the widget hierarchy to find the owning QMainWindow, then queries
 * toolBarArea(). Returns North for toolbars not managed by a QMainWindow.
 */
/*static*/ Position FreeCADStyle::toolbarPositionOf(const QToolBar* toolbar)
{
    const QWidget* ancestor = toolbar ? toolbar->parentWidget() : nullptr;
    while (ancestor) {
        if (const auto* mainWindow = qobject_cast<const QMainWindow*>(ancestor)) {
            switch (mainWindow->toolBarArea(const_cast<QToolBar*>(toolbar))) {
                case Qt::TopToolBarArea:
                    return Position::North;
                case Qt::BottomToolBarArea:
                    return Position::South;
                case Qt::RightToolBarArea:
                    return Position::East;
                case Qt::LeftToolBarArea:
                    return Position::West;
                default:
                    return Position::North;
            }
        }
        ancestor = ancestor->parentWidget();
    }
    return Position::North;
}

// ─── Context building ────────────────────────────────────────────────────────

static bool isFlat(const QWidget* widget, const QStyleOption* option)
{
    if (const auto* buttonOption = qstyleoption_cast<const QStyleOptionButton*>(option)) {
        if (buttonOption->features & QStyleOptionButton::Flat) {
            return true;
        }
    }
    if (const auto* button = qobject_cast<const QPushButton*>(widget)) {
        return button->isFlat();
    }
    if (const auto* toolButton = qobject_cast<const QToolButton*>(widget)) {
        return toolButton->autoRaise();
    }
    return widget && widget->property("flat").toBool();
}

bool FreeCADStyle::isTransparent(const QWidget* widget)
{
    return widget != nullptr && widget->property(transparencyProperty).toBool();
}

void FreeCADStyle::bindWidget(StyleContext& context, const QWidget* widget)
{
    context.widget = widget;

    if (isTransparent(widget)) {
        context.variant.set(VariantSlot::TransparencyMode, TransparencyMode::Transparent);
    }
}

bool FreeCADStyle::transparencyBelow(const QWidget* widget) const
{
    if (widget == nullptr) {
        return false;
    }

    // Only the propagated tag describes the surface: it says the widget is painted over
    // something see-through. The Transparent variant contextOf() derives for a toolbar hosted
    // in a status bar means the opposite direction — suppress my own chrome, I blend into an
    // opaque host — and must not be mistaken for a see-through surface for the children.
    // A theme that does want such a toolbar's children in the Transparent chain says so with
    // ToolBarTransparentIsTransparent, which resolves here for exactly that widget.
    return resolve<bool>(contextOf(widget), StyleProperty::IsTransparent).value_or(isTransparent(widget));
}

void FreeCADStyle::tagWidgetTransparency(QWidget* widget, bool surface) const
{
    if (isTransparent(widget) == surface) {
        return;
    }

    widget->setProperty(transparencyProperty, surface);

    // The tag changes padding, spacing and height tokens as well as colours, so a repaint
    // is not enough — QTabBar caches its tab sizes until the style changes.
    QEvent styleChange(QEvent::StyleChange);
    QCoreApplication::sendEvent(widget, &styleChange);
}

bool FreeCADStyle::ownSurface(const QWidget* widget, bool inherited)
{
    // An explicit property opens a root; otherwise inherit.
    const QVariant seed = widget->property(transparencyOverrideProperty);
    return seed.isValid() ? seed.toBool() : inherited;
}

bool FreeCADStyle::canInheritTransparency(const QWidget* widget)
{
    // Popups, menus, tooltips and dialogs are separate top-level surfaces over the desktop,
    // not surfaces over the 3D view — they do not inherit through the QObject parent/child
    // link used purely for lifetime management. An explicit override still applies regardless,
    // via ownSurface().
    return widget != nullptr && !widget->isWindow();
}

void FreeCADStyle::forEachChildWidget(QWidget* widget, const std::function<void(QWidget*)>& visit)
{
    // Copy first: callers invoke handlers (StyleChange, DynamicPropertyChange, ...) synchronously
    // while recursing, and in-tree handlers may add or remove siblings — e.g. DlgToolbarsImp
    // repopulates a tree on StyleChange — which would invalidate a live QObjectList mid-iteration.
    const QObjectList children = widget->children();
    for (QObject* child : children) {
        if (auto* childWidget = qobject_cast<QWidget*>(child)) {
            visit(childWidget);
        }
    }
}

void FreeCADStyle::updateTransparency(QWidget* widget, bool inherited)
{
    if (widget == nullptr) {
        return;
    }

    tagWidgetTransparency(widget, ownSurface(widget, inherited));

    const bool below = transparencyBelow(widget);

    forEachChildWidget(widget, [this, below](QWidget* childWidget) {
        updateTransparency(childWidget, canInheritTransparency(childWidget) && below);
    });
}

StyleContext FreeCADStyle::contextOf(
    const QWidget* widget,
    const QStyleOption* option,
    const StyleComponentElement& element
)
{
    StyleContext context;

    if (qobject_cast<const QToolButton*>(widget)) {
        const bool isInToolBar = qobject_cast<const QToolBar*>(widget->parent());
        context.component = isInToolBar ? StyleComponent::ToolBarButton : StyleComponent::ToolButton;
    }
    else if (qobject_cast<const QPushButton*>(widget)) {
        context.component = StyleComponent::PushButton;
    }
    else if (qobject_cast<const QLineEdit*>(widget) || qobject_cast<const QAbstractSpinBox*>(widget)) {
        context.component = StyleComponent::LineEdit;
    }
    else if (qobject_cast<const QTextEdit*>(widget) || qobject_cast<const QPlainTextEdit*>(widget)) {
        context.component = StyleComponent::TextEdit;
    }
    else if (const auto* comboBox = qobject_cast<const QComboBox*>(widget)) {
        context.component = comboBox->isEditable() ? StyleComponent::ComboBox
                                                   : StyleComponent::Select;
    }
    else if (qobject_cast<const QRadioButton*>(widget)) {
        context.component = StyleComponent::RadioButton;
    }
    else if (qobject_cast<const QCheckBox*>(widget) || element == StyleComponentElement::Indicator) {
        context.component = StyleComponent::CheckBox;
    }
    else if (qobject_cast<const QTreeView*>(widget)) {
        context.component = StyleComponent::Tree;
        context.element = element;
    }
    else if (qobject_cast<const QListView*>(widget)) {
        // FreeCADStyle::polish() tags the QComboBox's internal list view with this property
        // so we can reliably distinguish it without depending on Qt's internal parent chain,
        // which can change when the popup container is reparented at show time.
        const bool isDropdown = widget->property(FreeCADStyle::comboDropdownProperty).toBool();
        context.component = isDropdown ? StyleComponent::DropdownList : StyleComponent::List;
        context.element = element;
    }
    else if (qobject_cast<const QHeaderView*>(widget)) {
        context.component = StyleComponent::Header;
        context.element = element;
    }
    else if (qobject_cast<const QAbstractItemView*>(widget)) {
        // Catches QTableView, QColumnView, and other item-view subclasses not matched above.
        context.component = StyleComponent::List;
        context.element = element;
    }
    else if (qobject_cast<const QGroupBox*>(widget)) {
        context.component = StyleComponent::GroupBox;
        context.element = element;

        if (const auto* groupBoxOption = qstyleoption_cast<const QStyleOptionGroupBox*>(option);
            groupBoxOption && (groupBoxOption->features & QStyleOptionFrame::Flat)) {
            context.variant.set(VariantSlot::FrameType, FrameType::Flat);
        }
    }
    else if (const auto* toolbar = qobject_cast<const QToolBar*>(widget)) {
        context.component = StyleComponent::ToolBar;
        context.element = element;
        context.variant.set(VariantSlot::Position, toolbarPositionOf(toolbar));

        // Toolbars hosted in the status bar or as menu bar corner widgets blend into
        // their host surface — use the Transparent variant to suppress background and border.
        for (const QObject* ancestor = widget->parent(); ancestor; ancestor = ancestor->parent()) {
            if (qobject_cast<const QStatusBar*>(ancestor) || qobject_cast<const QMenuBar*>(ancestor)) {
                context.variant.set(VariantSlot::TransparencyMode, TransparencyMode::Transparent);
                break;
            }
        }
    }
    else if (qobject_cast<const QMenuBar*>(widget)) {
        context.component = StyleComponent::MenuBar;
        context.element = element;
        // QMenuBarItem uses State_Selected to indicate the active/hovered item; map it to Hovered.
        // State_Sunken indicates the menu is open (pressed).
        if (option && (option->state & QStyle::State_Selected)) {
            context.state |= StyleState::Hovered;
        }
        if (option && (option->state & QStyle::State_Sunken)) {
            context.state |= StyleState::Pressed;
        }
    }
    else if (
        qobject_cast<const QAbstractButton*>(widget) && widget->parent()
        && qobject_cast<const QTabBar*>(widget->parent())
    ) {
        // Qt's tab close buttons are private QAbstractButton children of QTabBar.
        // They must be detected before the generic QAbstractButton fallthrough below.
        context.component = StyleComponent::TabBar;
        context.element = StyleComponentElement::CloseButton;
        // component=TabBar is not in the isButton guard in the generic state block below,
        // so Pressed must be mapped explicitly here.
        if (option && (option->state & QStyle::State_Sunken)) {
            context.state |= StyleState::Pressed;
        }
        // Qt's CloseButton sets State_Raised (not State_MouseOver) for hover: its paintEvent
        // uses underMouse() and does not rely on WA_Hover. Map both flags to be safe.
        if (option
            && ((option->state & QStyle::State_Raised) || (option->state & QStyle::State_MouseOver))) {
            context.state |= StyleState::Hovered;
        }
    }
    else if (const auto* tabBar = qobject_cast<const QTabBar*>(widget)) {
        context.component = StyleComponent::TabBar;
        context.element = element;
        context.variant.set(VariantSlot::Position, tabPositionOf(tabBar->shape()));
        // QTabBar uses State_Selected (not State_On) to mark the active tab; map it to Checked.
        if (option && (option->state & QStyle::State_Selected)) {
            context.state |= StyleState::Checked;
        }
        // State_MouseOver is not reliably set in QStyleOptionTab — Qt tracks tab hover via
        // WA_Hover events and an internal hoverIndex, but does not always propagate that to
        // the option flags. Check cursor position directly instead.
        if (option && option->rect.contains(tabBar->mapFromGlobal(QCursor::pos()))) {
            context.state |= StyleState::Hovered;
        }
    }

    // ButtonType — derived from style option features first, then widget properties.
    const auto* buttonOption = qstyleoption_cast<const QStyleOptionButton*>(option);
    if (buttonOption && (buttonOption->features & QStyleOptionButton::DefaultButton)) {
        context.variant.set(VariantSlot::ButtonType, ButtonType::Primary);
    }
    else if (isFlat(widget, option)) {
        context.variant.set(VariantSlot::ButtonType, ButtonType::Link);
    }

    // An explicit "buttonType" property overrides the above. It is the only way to mark a
    // QToolButton as Primary, since a QToolButton never emits the QStyleOptionButton
    // DefaultButton feature that a default QPushButton uses.
    if (widget) {
        const QString buttonType = widget->property("buttonType").toString();
        if (buttonType == u"primary") {
            context.variant.set(VariantSlot::ButtonType, ButtonType::Primary);
        }
        else if (buttonType == u"link") {
            context.variant.set(VariantSlot::ButtonType, ButtonType::Link);
        }
    }

    // ControlSize — derived from the "controlSize" widget property.
    if (widget) {
        const QString sizeName = widget->property("controlSize").toString();
        if (sizeName == u"internal") {
            context.variant.set(VariantSlot::ControlSize, ControlSize::Internal);
        }
        else if (sizeName == u"small") {
            context.variant.set(VariantSlot::ControlSize, ControlSize::Small);
        }
        else if (sizeName == u"big") {
            context.variant.set(VariantSlot::ControlSize, ControlSize::Big);
        }
    }

    // Component override — derived from the "component" widget property.
    // For unrecognised widget types the name is resolved to a StyleComponent enum value
    // when possible (e.g. QFrame[component="List"] → StyleComponent::List). For recognised
    // widget types the name becomes a prefix override (e.g. "DocumentTree" on a QTreeView).
    //
    // The property names what the widget itself is, so it does not apply to an indicator the
    // style paints on the widget's behalf for a different component — a check indicator inside
    // an item view belongs to CheckBox, and letting the host's name win would rank the host's
    // own box tokens above the indicator's.
    const bool indicatorOfAnotherComponent = element == StyleComponentElement::Indicator
        && qobject_cast<const QCheckBox*>(widget) == nullptr
        && qobject_cast<const QRadioButton*>(widget) == nullptr;

    if (widget && !indicatorOfAnotherComponent) {
        const std::string overrideName = widget->property("component").toString().toStdString();
        if (!overrideName.empty()) {
            auto* manager = Application::Instance->styleParameterManager();

            if (const auto namedComponent = manager->descriptorRegistry().findComponent(overrideName)) {
                context.component = *namedComponent;
            }
            else {
                context.componentOverride = overrideName;
            }
        }
    }

    // State — all active flags captured as a bitmask.
    if (option) {
        if (!(option->state & QStyle::State_Enabled)) {
            context.state |= StyleState::Disabled;
        }

        // State_Sunken means "button is being pressed" for buttons, but "has a sunken
        // frame appearance" for input widgets (QLineEdit always sets it). Only map it
        // to Pressed for button-like and item-view components to avoid masking the
        // Focused state on inputs.
        const bool isButton = context.component == StyleComponent::PushButton
            || context.component == StyleComponent::ToolButton
            || context.component == StyleComponent::Select
            || context.component == StyleComponent::CheckBox
            || context.component == StyleComponent::RadioButton
            || context.component == StyleComponent::Header
            || context.component == StyleComponent::List || context.component == StyleComponent::Tree
            || context.component == StyleComponent::DropdownList;
        if (isButton && (option->state & QStyle::State_Sunken)) {
            context.state |= StyleState::Pressed;
        }
        if (option->state & QStyle::State_MouseOver) {
            context.state |= StyleState::Hovered;
        }
        if (option->state & QStyle::State_On) {
            context.state |= StyleState::Checked;
        }
        // For item views, State_Selected marks the currently selected item.
        // Map it to Checked — same convention used for active tabs.
        const bool isItemView = context.component == StyleComponent::List
            || context.component == StyleComponent::Tree
            || context.component == StyleComponent::DropdownList;
        if (isItemView && (option->state & QStyle::State_Selected)) {
            context.state |= StyleState::Checked;
        }
        if (option->state & QStyle::State_HasFocus) {
            context.state |= StyleState::Focused;
        }
    }

    // QAbstractSpinBox delegates keyboard focus to an inner QLineEdit child, so
    // the spinbox widget's hasFocus() returns false and State_HasFocus is absent
    // from its style option. Supplement the state by checking the inner edit directly.
    if (qobject_cast<const QAbstractSpinBox*>(widget)) {
        if (const QLineEdit* innerEdit = widget->findChild<QLineEdit*>()) {
            if (innerEdit->hasFocus()) {
                context.state |= StyleState::Focused;
            }
        }
    }

    // An editable QComboBox also delegates keyboard focus to its inner QLineEdit.
    // Same pattern as QAbstractSpinBox: supplement state from the inner edit.
    if (const auto* comboBox = qobject_cast<const QComboBox*>(widget);
        comboBox && comboBox->isEditable()) {
        if (const QLineEdit* lineEdit = comboBox->lineEdit()) {
            if (lineEdit->hasFocus()) {
                context.state |= StyleState::Focused;
            }
        }
    }

    // RowType — set only for the normal (no active state) case so that interaction
    // states (hover, selection, pressed, disabled) use their own tokens without the
    // Alternate variant prefix interfering (e.g. hovered alternate rows resolve via
    // ListRowHovered*, not ListRowAlternateHovered*).
    if (context.state == StyleState::Normal) {
        if (const auto* vopt = qstyleoption_cast<const QStyleOptionViewItem*>(option)) {
            if (vopt->features & QStyleOptionViewItem::Alternate) {
                context.variant.set(VariantSlot::RowType, RowType::Alternate);
            }
        }
    }

    // Carries the widget for override lookup and applies the TransparencyMode tag inherited
    // down the widget tree by updateTransparency().
    bindWidget(context, widget);

    return context;
}

// ─── Token resolution ────────────────────────────────────────────────────────

std::optional<Value> FreeCADStyle::resolve(const StyleContext& context, StyleProperty property) const
{
    const uint32_t bin = overrideSetOf(context.widget);
    const uint64_t key = context.cacheKey(property);

    if (const auto* cached = tokenCache.find(bin, key)) {
        return *cached;
    }

    auto* manager = Application::Instance->styleParameterManager();

    const std::string propertySuffix(propertyString(property));
    std::optional<Value> result;

    for (const std::string& prefix : manager->descriptorRegistry().buildPrefixes(context)) {
        // Use the flat resolver per prefix: the prefix list IS the inheritance
        // walk, so name-based chain synthesis must not run here.
        result = manager->resolve(
            prefix + propertySuffix,
            StyleParameters::ParameterManager::ResolveContext {
                .visited = {},
                .overrides = bin,
            }
        );
        if (!result) {
            continue;
        }
        if (result->holds<None>()) {
            result = std::nullopt;
            break;
        }
        break;
    }

    tokenCache.store(bin, key, result);
    return result;
}

FreeCADStyle::BoxStyleDefinition FreeCADStyle::resolveBoxStyle(const StyleContext& context) const
{
    const uint32_t bin = overrideSetOf(context.widget);
    const uint64_t key = context.cacheKey();

    if (const auto* cached = boxStyleCache.find(bin, key)) {
        return *cached;
    }

    const auto position = static_cast<Position>(context.variant.get(VariantSlot::Position));
    const StyleContext northContext = withNorthPosition(context);

    BoxStyleDefinition result;

    // Directional tokens: resolved from canonical North, rotated to actual position.
    // rotated(x, North) is the identity — safe to call unconditionally for all components.
    if (const auto background = resolve(northContext, StyleProperty::Background)) {
        result.background = rotated(Base::convertTo<QBrush>(*background), position);
    }
    if (const auto borderRadius = resolve<Corners>(northContext, StyleProperty::BorderRadius)) {
        result.borderRadius = rotated(Base::convertTo<CornerRadii>(*borderRadius), position);
    }
    if (const auto borderThickness = resolve<Insets>(northContext, StyleProperty::BorderThickness)) {
        result.borderThickness = rotated(Base::convertTo<QMarginsF>(*borderThickness), position);
    }

    // Non-directional visual tokens resolved from the actual context.
    if (const auto borderColors = resolve<BorderColors>(context, StyleProperty::BorderColor)) {
        result.borderColor = Base::convertTo<BorderColorsPerSide>(*borderColors);
    }
    if (const auto innerShadow
        = resolve<StyleParameters::InnerShadow>(context, StyleProperty::InnerShadow)) {
        result.innerShadow = Base::convertTo<InnerShadow>(*innerShadow);
    }

    // BackgroundEffect: resolved from northContext (same directional semantics as Background).
    if (const auto effect = resolve<ColorEffect>(northContext, StyleProperty::BackgroundEffect)) {
        result.background = applyEffectToBrush(result.background, *effect);
    }

    // BorderColorEffect: resolved from actual context (same as BorderColor).
    if (const auto effect = resolve<ColorEffect>(context, StyleProperty::BorderColorEffect)) {
        if (result.borderColor) {
            auto& colors = *result.borderColor;
            for (QColor* side : {&colors.top, &colors.right, &colors.bottom, &colors.left}) {
                *side = effect->apply(Base::Color::fromValue(*side)).asValue<QColor>();
            }
        }
    }

    boxStyleCache.store(bin, key, result);
    return result;
}

FreeCADStyle::BoxGeometryDefinition FreeCADStyle::resolveBoxGeometry(const StyleContext& context) const
{
    const uint32_t bin = overrideSetOf(context.widget);
    const uint64_t key = context.cacheKey();

    if (const auto* cached = boxGeometryCache.find(bin, key)) {
        return *cached;
    }

    BoxGeometryDefinition result;

    if (const auto padding = resolve<Insets>(context, StyleProperty::Padding)) {
        result.padding = Base::convertTo<QMarginsF>(*padding);
    }

    if (const auto margin = resolve<Insets>(context, StyleProperty::Margin)) {
        result.margin = Base::convertTo<QMarginsF>(*margin);
    }

    if (const auto height = resolve<Numeric>(context, StyleProperty::Height)) {
        result.height = static_cast<int>(*height);
    }

    if (const auto minWidth = resolve<Numeric>(context, StyleProperty::MinWidth)) {
        result.minWidth = static_cast<int>(*minWidth);
    }

    if (const auto resolvedWidth = resolve<Numeric>(context, StyleProperty::Width)) {
        result.width = static_cast<int>(*resolvedWidth);
    }

    if (const auto resolvedMaxWidth = resolve<Numeric>(context, StyleProperty::MaxWidth)) {
        result.maxWidth = static_cast<int>(*resolvedMaxWidth);
    }

    if (const auto resolvedMinHeight = resolve<Numeric>(context, StyleProperty::MinHeight)) {
        result.minHeight = static_cast<int>(*resolvedMinHeight);
    }

    if (const auto resolvedMaxHeight = resolve<Numeric>(context, StyleProperty::MaxHeight)) {
        result.maxHeight = static_cast<int>(*resolvedMaxHeight);
    }

    if (const auto spacing = resolve<Numeric>(context, StyleProperty::IconSpacing)) {
        result.iconSpacing = static_cast<int>(*spacing);
    }

    if (const auto resolvedSpacing = resolve<Numeric>(context, StyleProperty::Spacing)) {
        result.spacing = static_cast<int>(*resolvedSpacing);
    }

    boxGeometryCache.store(bin, key, result);
    return result;
}

QFont FreeCADStyle::resolveFont(const StyleContext& context, const QFont& base) const
{
    QFont font = base;

    if (const auto size = resolve<Numeric>(context, StyleProperty::FontSize)) {
        if (size->value > 0) {
            if (size->unit == "pt") {
                font.setPointSizeF(size->value);
            }
            else {
                font.setPixelSize(static_cast<int>(*size));
            }
        }
    }

    if (const auto weight = resolve<Numeric>(context, StyleProperty::FontWeight)) {
        // QFont rejects anything outside 1..1000 and warns; a malformed token should not be
        // able to spray the log from inside a paint call.
        const int clamped = std::clamp(static_cast<int>(*weight), 1, 1000);
        font.setWeight(static_cast<QFont::Weight>(clamped));
    }

    return font;
}

void FreeCADStyle::clearTokenCache()
{
    tokenCache.clear();
    boxStyleCache.clear();
    boxGeometryCache.clear();
    StyleContext::Intern::global().clear();
}

uint32_t FreeCADStyle::overrideSetOf(const QWidget* widget) const
{
    if (widget == nullptr) {
        return StyleParameters::OverrideRegistry::emptyId;
    }

    if (overrideMemoWidget.data() == widget) {
        return overrideMemoSet;
    }

    overrideMemoWidget = widget;
    overrideMemoSet = widget->property(overrideSetProperty).toUInt();

    return overrideMemoSet;
}

StyleParameters::OverrideSet FreeCADStyle::declaredOverrides(const QWidget* widget)
{
    StyleParameters::OverrideSet declared;

    const std::string_view prefix {overridePropertyPrefix};

    for (const QByteArray& propertyName : widget->dynamicPropertyNames()) {
        const std::string_view name {
            propertyName.constData(),
            static_cast<size_t>(propertyName.size()),
        };

        if (!name.starts_with(prefix) || name.size() == prefix.size()) {
            continue;
        }

        const QVariant value = widget->property(propertyName.constData());
        if (value.typeId() != QMetaType::QString) {
            continue;
        }

        declared.emplace(std::string(name.substr(prefix.size())), value.toString().toStdString());
    }

    return declared;
}

uint32_t FreeCADStyle::computeOverrideSet(const QWidget* widget)
{
    // Before this walk existed, polishing a top-level widget never reached the manager:
    // canInheritTransparency() short-circuited the rest of polish()'s style-token work for it.
    // Now every polish does, including ones that can run without a Gui::Application behind them
    // (a bare QApplication::setStyle() in a test, or an embedding) — fail soft instead.
    if (Application::Instance == nullptr) {
        return StyleParameters::OverrideRegistry::emptyId;
    }

    StyleParameters::OverrideSet merged;

    for (const QWidget* ancestor = widget; ancestor != nullptr; ancestor = ancestor->parentWidget()) {
        // try_emplace keeps what is already there, so the nearest declaration of a name wins.
        for (auto&& [name, expression] : declaredOverrides(ancestor)) {
            merged.try_emplace(name, std::move(expression));
        }

        // A window is its own surface: its own declarations count, but it does not inherit from
        // whatever it is parented to for lifetime management. Same rule as
        // canInheritTransparency().
        if (ancestor->isWindow()) {
            break;
        }
    }

    return Application::Instance->styleParameterManager()->overrideRegistry().intern(merged);
}

void FreeCADStyle::storeOverrideSet(QWidget* widget, uint32_t set) const
{
    // QObject::setProperty() allocates the object's dynamic-property storage on first use, even
    // when it is only clearing a property that was never set — so only write when there is
    // something to record or an existing property to clear, or every widget with no overrides
    // would pay that allocation anyway. An invalid QVariant removes the property.
    const bool hasStoredSet = widget->dynamicPropertyNames().contains(overrideSetProperty);
    if (set != StyleParameters::OverrideRegistry::emptyId || hasStoredSet) {
        widget->setProperty(
            overrideSetProperty,
            set == StyleParameters::OverrideRegistry::emptyId ? QVariant {} : QVariant {set}
        );
    }

    // Seeded rather than cleared: this function already knows the correct (widget, set) pair,
    // so priming the memo with it directly saves the very next overrideSetOf() call the
    // QObject::property() lookup the memo exists to avoid.
    overrideMemoWidget = widget;
    overrideMemoSet = set;
}

void FreeCADStyle::setStyleOverride(QWidget* widget, const QString& name, const QString& expression)
{
    if (widget == nullptr) {
        return;
    }

    const QString propertyName = QString::fromLatin1(overridePropertyPrefix) + name;
    widget->setProperty(propertyName.toLatin1().constData(), expression);

    refreshStyleOverrides(widget);
}

void FreeCADStyle::refreshStyleOverrides(QWidget* widget)
{
    if (widget == nullptr) {
        return;
    }

    // Nothing to recompute when this widget is not ours to style; the declaration stays on the
    // widget and is picked up if a FreeCADStyle polishes it later.
    if (const auto* style = qobject_cast<const FreeCADStyle*>(widget->style())) {
        style->recomputeOverrideSets(widget);
    }
}

void FreeCADStyle::recomputeOverrideSets(QWidget* widget) const
{
    storeOverrideSet(widget, computeOverrideSet(widget));

    forEachChildWidget(widget, [this](QWidget* childWidget) { recomputeOverrideSets(childWidget); });
}

StyleContext FreeCADStyle::withNorthPosition(const StyleContext& context)
{
    StyleContext north = context;
    north.variant.set(VariantSlot::Position, Position::North);
    return north;
}
