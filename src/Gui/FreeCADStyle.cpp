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
#include <cmath>
#include <map>
#include <span>
#include <string>
#include <vector>
#include <QApplication>
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
#include <QStyleOption>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QRadioButton>
#include <QListView>
#include <QStyleOptionViewItem>
#include <QTreeView>
#include <QPointer>
#include <QScrollBar>
#include <QScreen>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>

#include <Base/Color.h>
#include <Base/Converter.h>
#include <Base/Exception.h>

#include "Application.h"
#include "QuantitySpinBox_p.h"
#include "ThemeReloadEvent.h"
#include "Utilities.h"
#include "StyleParameters/Corners.h"
#include "StyleParameters/InnerShadow.h"
#include "StyleParameters/Insets.h"
#include "StyleParameters/ParameterManager.h"

#include <IconManager.h>

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
        [this](const IconManager::IconMeta& /*meta*/, QIcon::Mode mode, QIcon::State /*state*/) -> QColor {
            StyleContext context;
            context.component = StyleComponent::PushButton;
            if (mode == QIcon::Disabled) {
                context.state |= StyleState::Disabled;
            }
            else if (mode == QIcon::Active) {
                context.state |= StyleState::Hovered;
            }
            else if (mode == QIcon::Selected) {
                context.state |= StyleState::Checked;
            }
            if (const auto color = resolve<Base::Color>(context, StyleProperty::IconColor)) {
                return color->asValue<QColor>();
            }
            if (const auto color = resolve<Base::Color>(context, StyleProperty::TextColor)) {
                return color->asValue<QColor>();
            }
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
        .topLeft = corners.topLeft().value,
        .topRight = corners.topRight().value,
        .bottomRight = corners.bottomRight().value,
        .bottomLeft = corners.bottomLeft().value,
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

}  // namespace Base

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
    // Clamp each radius to at most half the shorter side so the path stays valid
    // even when a large radius (e.g. 100 px) is used to produce a circular shape.
    const qreal maxRadius = std::min(rect.width(), rect.height()) / 2.0;
    const qreal topLeft = std::min(radii.topLeft, maxRadius);
    const qreal topRight = std::min(radii.topRight, maxRadius);
    const qreal bottomRight = std::min(radii.bottomRight, maxRadius);
    const qreal bottomLeft = std::min(radii.bottomLeft, maxRadius);

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

FreeCADStyle::CornerRadii innerRadii(const FreeCADStyle::CornerRadii& outer, const QMarginsF& thickness)
{
    auto shrink = [](qreal radius, qreal a, qreal b) -> qreal {
        return std::max(0.0, radius - std::max(a, b));
    };
    return {
        .topLeft = shrink(outer.topLeft, thickness.top(), thickness.left()),
        .topRight = shrink(outer.topRight, thickness.top(), thickness.right()),
        .bottomRight = shrink(outer.bottomRight, thickness.bottom(), thickness.right()),
        .bottomLeft = shrink(outer.bottomLeft, thickness.bottom(), thickness.left()),
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
        .radiusTopLeft = radii.topLeft,
        .radiusTopRight = radii.topRight,
        .radiusBottomRight = radii.bottomRight,
        .radiusBottomLeft = radii.bottomLeft,
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


}  // namespace

void FreeCADStyle::drawBoxBackground(QPainter* painter, const QRect& rect, const BoxStyleDefinition& rule)
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
    // Clip to the outer rect so antialiased arc pixels cannot bleed outside.
    painter->setClipRect(rect, Qt::IntersectClip);

    QRect backgroundRect = rect;
    CornerRadii backgroundRadii = rule.borderRadius;

    if (hasBorder) {
        const QMarginsF& thickness = *rule.borderThickness;

        // Snap each border side to the nearest integer pixel.
        const QMarginsF snappedThickness(
            qRound(thickness.left()),
            qRound(thickness.top()),
            qRound(thickness.right()),
            qRound(thickness.bottom())
        );
        backgroundRect = rect.adjusted(
            qRound(thickness.left()),
            qRound(thickness.top()),
            -qRound(thickness.right()),
            -qRound(thickness.bottom())
        );
        backgroundRadii = innerRadii(rule.borderRadius, snappedThickness);

        // Subtract inner from outer path to fill only the border ring, preserving transparency.
        const QPainterPath outerPath = roundedRectPath(QRectF(rect), rule.borderRadius);
        const QPainterPath innerPath = roundedRectPath(QRectF(backgroundRect), backgroundRadii);
        const QPainterPath borderRingPath = outerPath.subtracted(innerPath);
        painter->fillPath(borderRingPath, QBrush(*rule.borderColor));

        if (rule.borderOverlay) {
            painter->fillPath(borderRingPath, *rule.borderOverlay);
        }
    }

    if (hasBackground) {
        painter->fillPath(roundedRectPath(QRectF(backgroundRect), backgroundRadii), rule.background);

        if (rule.overlay) {
            painter->fillPath(roundedRectPath(QRectF(backgroundRect), backgroundRadii), *rule.overlay);
        }
    }

    painter->restore();

    if (hasInnerShadow) {
        const int padding = static_cast<int>(std::ceil(rule.innerShadow->blur)) + 1;
        const QImage& shadowImage = getCachedShadowImage(rect, rule.borderRadius, *rule.innerShadow);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setClipPath(roundedRectPath(QRectF(rect), rule.borderRadius), Qt::IntersectClip);
        painter->drawImage(
            QPointF(
                rect.left() - padding + rule.innerShadow->x,
                rect.top() - padding + rule.innerShadow->y
            ),
            shadowImage
        );
        painter->restore();
    }
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

    const std::map<PixelMetric, std::pair<StyleComponentElement, StyleProperty>> metrics = {
        {PM_ExclusiveIndicatorWidth, {StyleComponentElement::Indicator, Width}},
        {PM_ExclusiveIndicatorHeight, {StyleComponentElement::Indicator, Height}},
        {PM_IndicatorWidth, {StyleComponentElement::Indicator, Width}},
        {PM_IndicatorHeight, {StyleComponentElement::Indicator, Height}},
        {PM_CheckBoxLabelSpacing, {StyleComponentElement::Indicator, Spacing}},
        {PM_RadioButtonLabelSpacing, {StyleComponentElement::Indicator, Spacing}},
        {PM_MenuButtonIndicator, {StyleComponentElement::Menu, Width}},
        {PM_ToolBarItemMargin, {StyleComponentElement::Item, Margin}},
        {PM_ToolBarItemSpacing, {StyleComponentElement::Item, Spacing}},
    };

    switch (metric) {
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
        if (qobject_cast<const QTextEdit*>(widget) || qobject_cast<const QPlainTextEdit*>(widget)) {
            const auto* frameOption = qstyleoption_cast<const QStyleOptionFrame*>(option);
            if (frameOption && frameOption->lineWidth > 0) {
                drawComponent(painter, option->rect, widget, option);
                return;
            }
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
            drawTabWidgetFrame(painter, tabWidgetOption);
            return;
        }
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
    if (tabOption && !tabOption->icon.isNull() && !tabOption->text.isEmpty()) {
        const BoxGeometryDefinition geometry = resolveBoxGeometry(
            withNorthPosition(contextOf(widget, option, StyleComponentElement::Tab))
        );
        result.rwidth() += geometry.iconGapDelta();
    }

    // The background is painted narrower by |tabOverlap| on the trailing edge to create the
    // visual gap between tabs (see drawTabBarTab). Add that same amount to the tab rect so
    // the content area, computed from the background rect, still has symmetric padding.
    const int tabOverlap = proxy()->pixelMetric(PM_TabBarTabOverlap, option, widget);
    if (tabOverlap < 0) {
        result.rwidth() -= tabOverlap;
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
    }

    return geometry.sizeFromContents(baseSize);
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
        return geometry.constrain(result);
    }

    if (type == CT_TabBarTab) {
        return tabBarTabSizeFromContents(option, size, widget);
    }

    if (type == CT_LineEdit || type == CT_SpinBox) {
        const BoxGeometryDefinition geometry = resolveBoxGeometry(contextOf(widget, option));
        return geometry.constrain(QProxyStyle::sizeFromContents(type, option, size, widget));
    }

    if (type == CT_ToolButton) {
        if (const auto* opt = qstyleoption_cast<const QStyleOptionToolButton*>(option)) {
            return toolButtonSizeFromContents(opt, size, widget);
        }
    }

    if (type == CT_ItemViewItem) {
        return itemViewItemSizeFromContents(option, size, widget);
    }

    return QProxyStyle::sizeFromContents(type, option, size, widget);
}

QRect FreeCADStyle::subElementRect(SubElement element, const QStyleOption* option, const QWidget* widget) const
{
    // QProxyStyle sets baseStyle->proxy = this, so the base style's drawControl(CE_ItemViewItem)
    // calls proxy()->subElementRect() which reaches OUR overrides below.  We therefore only need
    // to override SE_ItemViewItemDecoration and SE_ItemViewItemText — the inset propagates into
    // both drawing (via the base-style drawControl callback) and editor/widget placement (via
    // updateEditorGeometry).  We delegate to QProxyStyle with the already-inset rect so
    // viewItemLayout positions the icon within the inset area and the text rect after it.
    const auto itemViewInsetRect = [&](SubElement el) -> QRect {
        const auto* vopt = qstyleoption_cast<const QStyleOptionViewItem*>(option);
        // option->widget is the view; widget may be the viewport during some repaints.
        const QWidget* effectiveWidget = widget;
        if (!effectiveWidget && vopt) {
            effectiveWidget = vopt->widget;
        }
        const StyleContext context = contextOf(effectiveWidget, option, StyleComponentElement::Item);
        const bool isItemComponent = context.element == StyleComponentElement::Item;
        if (!isItemComponent || !vopt) {
            return QProxyStyle::subElementRect(el, option, widget);
        }
        const BoxGeometryDefinition geometry = resolveBoxGeometry(context);
        QStyleOptionViewItem adjustedOption = *vopt;
        adjustedOption.rect = geometry.contentRect(vopt->rect);
        return QProxyStyle::subElementRect(el, &adjustedOption, widget);
    };

    if (element == SE_ItemViewItemDecoration) {
        return itemViewInsetRect(SE_ItemViewItemDecoration);
    }

    if (element == SE_ItemViewItemText) {
        return itemViewInsetRect(SE_ItemViewItemText);
    }

    if (element == SE_TabWidgetTabContents) {
        StyleContext paneContext;
        paneContext.component = StyleComponent::TabWidget;
        paneContext.element = StyleComponentElement::Root;
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

    const int buttonLeft = contentRect.right() - buttonSize.width() + 1;
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
        case SC_SpinBoxUp: {
            if (!hasButtons) {
                return {};
            }
            return {
                buttonLeft,
                centerY - buttonSize.height() + 1,
                buttonSize.width(),
                buttonSize.height()
            };
        }
        case SC_SpinBoxDown: {
            if (!hasButtons) {
                return {};
            }
            return {buttonLeft, centerY + 1, buttonSize.width(), buttonSize.height()};
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
    if (!(option->features & QStyleOptionToolButton::MenuButtonPopup)) {
        return QProxyStyle::subControlRect(CC_ToolButton, option, subControl, widget);
    }

    const QRect rect = option->rect;
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
    else if (option->features & QStyleOptionToolButton::HasMenu) {
        // Instant/delayed popup: draw a small arrow indicator in the bottom-right corner.
        const int arrowSize = proxy()->pixelMetric(PM_MenuButtonIndicator, option, widget);
        QStyleOptionToolButton arrowOption = *option;
        arrowOption.rect = QRect(
            option->rect.right() - arrowSize + 1,
            option->rect.bottom() - arrowSize + 1,
            arrowSize,
            arrowSize
        );
        proxy()->drawPrimitive(PE_IndicatorArrowDown, &arrowOption, painter, widget);
    }

    // Draw label (icon + text). Restrict to SC_ToolButton so it does not bleed into
    // the menu strip. Also clear SC_ToolButtonMenu so QCommonStyle::CE_ToolButtonLabel
    // does not draw its own menu indicator arrow on top of ours.
    QStyleOptionToolButton labelOption = *option;
    labelOption.rect = mainRect;
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
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

void FreeCADStyle::drawControl(
    ControlElement element,
    const QStyleOption* option,
    QPainter* painter,
    const QWidget* widget
) const
{
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

        const QRect shiftedRect = applyButtonShift(contentRect, option, widget);
        const QPixmap pixmap = renderStyledIcon(
            painter,
            option->icon,
            shiftedRect.size().boundedTo(option->iconSize),
            option,
            context
        );

        if (!pixmap.isNull()) {
            proxy()->drawItemPixmap(painter, shiftedRect, Qt::AlignCenter, pixmap);
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

void FreeCADStyle::drawTabBarBase(
    QPainter* painter,
    const QStyleOptionTabBarBase* option,
    const QWidget* widget
) const
{
    const StyleContext positionContext = contextOf(widget, option, StyleComponentElement::Base);
    drawBoxBackground(painter, option->rect, resolveBoxStyle(positionContext));
}

void FreeCADStyle::drawTabWidgetFrame(QPainter* painter, const QStyleOptionTabWidgetFrame* option) const
{
    const Position position = tabPositionOf(option->shape);

    // Draw the pane frame using design-system tokens.
    StyleContext paneContext;
    paneContext.component = StyleComponent::TabWidget;
    paneContext.element = StyleComponentElement::Root;
    drawBoxBackground(painter, option->rect, resolveBoxStyle(paneContext));

    // Draw the shadow strip at the attachment edge.
    // Build context manually — contextOf() requires a QTabBar widget to produce the
    // TabBar component; here the widget is QTabWidget.
    StyleContext stripContext;
    stripContext.component = StyleComponent::TabBar;
    stripContext.element = StyleComponentElement::Base;
    stripContext.variant.set(VariantSlot::Position, position);

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

void FreeCADStyle::polish(QWidget* widget)
{
    QProxyStyle::polish(widget);
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
}

void FreeCADStyle::constrainComboDropdown(QComboBox* comboBox)
{
    comboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

    auto* listView = qobject_cast<QListView*>(comboBox->view());
    if (!listView) {
        return;
    }
    listView->setProperty(comboDropdownProperty, true);
    listView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    StyleContext context;
    context.component = StyleComponent::DropdownList;
    const BoxGeometryDefinition geometry = resolveBoxGeometry(context);
    if (!geometry.maxHeight) {
        return;
    }

    listView->setMaximumHeight(*geometry.maxHeight);

    QWidget* container = listView->parentWidget();
    if (!container) {
        return;
    }

    container->setMaximumHeight(*geometry.maxHeight);
    hideScrollerButtons(container);

    // Guard against double-installation on re-polish (e.g. theme change).
    if (!container->property(comboContainerProperty).toBool()) {
        container->setProperty(comboContainerProperty, true);
        container->installEventFilter(this);
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
    if (qobject_cast<QTabBar*>(widget)) {
        widget->removeEventFilter(this);
    }
    if (auto* comboBox = qobject_cast<QComboBox*>(widget)) {
        restoreComboDropdownDefaults(comboBox);
    }
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
