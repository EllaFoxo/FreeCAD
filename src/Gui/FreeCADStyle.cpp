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

#include "FreeCADStyle.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <span>
#include <string>
#include <vector>
#include <QGroupBox>
#include <QImage>
#include <QLayout>
#include <QLinearGradient>
#include <QPainterPath>
#include <QStyleOption>
#include <QRadialGradient>
#include <QStyleOption>

#include <Base/Color.h>
#include <Base/Converter.h>
#include <Base/Exception.h>

#include "Application.h"
#include "StyleParameters/Corners.h"
#include "StyleParameters/Gradient.h"
#include "StyleParameters/InnerShadow.h"
#include "StyleParameters/Insets.h"
#include "StyleParameters/ParameterManager.h"

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

// ─── Base::convertTo specializations ──────────────────────────────────────
// Conversions for FreeCADStyle-specific types (CornerRadii, InnerShadow) that
// cannot live in Utilities.h because they depend on types declared in this
// header. General-purpose StyleParameters↔Qt conversions (QMarginsF, QBrush,
// QLinearGradient, QRadialGradient) are defined in Utilities.h.

namespace Base
{

template<>
FreeCADStyle::CornerRadii convertTo<FreeCADStyle::CornerRadii, StyleParameters::Corners>(
    const StyleParameters::Corners& corners
)
{
    return {
        .topLeft = corners.topLeft().value,
        .topRight = corners.topRight().value,
        .bottomRight = corners.bottomRight().value,
        .bottomLeft = corners.bottomLeft().value,
    };
}

template<>
FreeCADStyle::InnerShadow convertTo<FreeCADStyle::InnerShadow, StyleParameters::InnerShadow>(
    const StyleParameters::InnerShadow& shadow
)
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
    const qreal topLeft = radii.topLeft;
    const qreal topRight = radii.topRight;
    const qreal bottomRight = radii.bottomRight;
    const qreal bottomLeft = radii.bottomLeft;

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

// ─── StyleToken string tables ──────────────────────────────────────────────
// Data-driven mappings from enum values to the string fragments used in token
// names such as "ButtonPrimaryHoveredBackground".
//
// All tables use std::map with explicit enum keys so that entries are
// self-documenting and order-independent.  Default values (empty string) are
// absent from the maps; the lookup functions return "" on a miss.
//
// To add a new component: add a StyleComponent entry and insert its chain here.
// To add a new property/variant/state: add the enum entry and insert its
// string here — there is no ordering constraint to worry about.

// Returns a const reference to map[key] if present, or to a static empty
// default otherwise.  Returning by reference avoids copies and is safe to
// wrap in std::span since the default outlives the call.
template<typename Map>
auto lookup(const Map& map, const typename Map::key_type& key) -> const typename Map::mapped_type&
{
    static const typename Map::mapped_type empty {};
    const auto found = map.find(key);
    return found != map.end() ? found->second : empty;
}

// ── Component inheritance chains ────────────────────────────────────────────
// Each component maps to an ordered chain of token prefixes, most-specific first.
// To add a new abstract base: edit the relevant chain — no enum change needed.
// clang-format off
const std::map<StyleComponent, std::vector<std::string_view>> componentChains = {
    {StyleComponent::PushButton, {"Button", "FormControl"}},
    {StyleComponent::ToolButton, {"ToolButton", "Button", "FormControl"}},
    {StyleComponent::LineEdit,   {"LineEdit", "FormControl"}},
    {StyleComponent::TextEdit,   {"TextEdit", "LineEdit", "FormControl"}},
    {StyleComponent::Select,     {"Select", "Button", "FormControl"}},
    {StyleComponent::ComboBox,   {"ComboBox", "LineEdit", "FormControl"}},
};
// clang-format on

std::span<const std::string_view> componentChain(StyleComponent component)
{
    static constexpr auto pushButton = std::to_array<std::string_view>({"Button", "FormControl"});
    static constexpr auto toolButton = std::to_array<std::string_view>(
        {"ToolButton", "Button", "FormControl"}
    );

    switch (component) {
        case StyleComponent::PushButton:
            return pushButton;
        case StyleComponent::ToolButton:
            return toolButton;
        default:
            return {};
    }
}

// ── Variant slot string tables ───────────────────────────────────────────────
// Default (0) is absent from each inner map; variantSlotString returns "" for it.

// clang-format off
const std::map<VariantSlot, std::map<uint8_t, std::string_view>> variantSlotNames = {
    {VariantSlot::ButtonType, {
        {static_cast<uint8_t>(ButtonType::Primary), "Primary"},
        {static_cast<uint8_t>(ButtonType::Link),    "Link"},
    }},
    {VariantSlot::ControlSize, {
        {static_cast<uint8_t>(ControlSize::Small), "Small"},
        {static_cast<uint8_t>(ControlSize::Big),   "Big"},
    }},
};
// clang-format on

std::string_view variantSlotString(VariantSlot slot, uint8_t value)
{
    return lookup(lookup(variantSlotNames, slot), value);
}

// Concatenates the string fragments of all non-default variant slots.
// e.g. ButtonType=Primary, ControlSize=Default → "Primary"
std::string variantString(const VariantKey& variant)
{
    std::string result;
    for (size_t index = 0; index < variant.slots.size(); ++index) {
        result += variantSlotString(static_cast<VariantSlot>(index), variant.slots.at(index));
    }
    return result;
}

// ── State strings ────────────────────────────────────────────────────────────

const std::map<StyleState, std::string_view> stateNames = {
    {StyleState::Pressed, "Pressed"},
    {StyleState::Hovered, "Hovered"},
    {StyleState::Checked, "Checked"},
    {StyleState::Focused, "Focused"},
};

std::string_view stateString(StyleState state)
{
    return lookup(stateNames, state);
}

// ── Property strings ─────────────────────────────────────────────────────────

// clang-format off
const std::map<StyleProperty, std::string_view> propertyNames = {
    {StyleProperty::Width,           "Width"},
    {StyleProperty::MinWidth,        "MinWidth"},
    {StyleProperty::MaxWidth,        "MaxWidth"},
    {StyleProperty::Height,          "Height"},
    {StyleProperty::MinHeight,       "MinHeight"},
    {StyleProperty::MaxHeight,       "MaxHeight"},
    {StyleProperty::BorderThickness, "BorderThickness"},
    {StyleProperty::BorderRadius,    "BorderRadius"},
    {StyleProperty::BorderColor,     "BorderColor"},
    {StyleProperty::Padding,         "Padding"},
    {StyleProperty::Margin,          "Margin"},
    {StyleProperty::IconSize,        "IconSize"},
    {StyleProperty::IconSpacing,     "IconSpacing"},
    {StyleProperty::FontSize,        "FontSize"},
    {StyleProperty::FontWeight,      "FontWeight"},
    {StyleProperty::Background,      "Background"},
    {StyleProperty::TextColor,       "TextColor"},
    {StyleProperty::Overlay,         "Overlay"},
    {StyleProperty::OverlayOpacity,  "OverlayOpacity"},
    {StyleProperty::InnerShadow,     "InnerShadow"},
};
// clang-format on

std::string_view propertyString(StyleProperty property)
{
    return lookup(propertyNames, property);
}

// ─── Prefix list builder ────────────────────────────────────────────────────
//
// Produces the ordered fallback prefix list for a StyleContext.
//
// Given component="Button", variant="Primary", active states={Hovered, Focused}
// the result is:
//   "ButtonPrimaryHovered"   ← variant + highest-priority state
//   "ButtonPrimaryFocused"   ← variant + next state
//   "ButtonPrimary"          ← variant, no state
//   "ButtonHovered"          ← no variant, highest-priority state
//   "ButtonFocused"          ← no variant, next state
//   "Button"                 ← baseline

// Priority order — highest first. Mirrors the enum declaration order (Pressed > Hovered > …).
constexpr auto statePriorityOrder = std::to_array({
    StyleState::Pressed,
    StyleState::Hovered,
    StyleState::Checked,
    StyleState::Focused,
});

std::vector<std::string> buildPrefixes(const StyleContext& context)
{
    const std::string variantSuffix = variantString(context.variant);

    std::vector<StyleState> activeStates;
    for (const StyleState stateFlag : statePriorityOrder) {
        if (context.state.testFlag(stateFlag)) {
            activeStates.push_back(stateFlag);
        }
    }

    std::vector<std::string> prefixes;

    for (const std::string_view componentPrefix : componentChain(context.component)) {
        if (!variantSuffix.empty()) {
            for (const StyleState stateFlag : activeStates) {
                prefixes.push_back(
                    std::string(componentPrefix) + variantSuffix + std::string(stateString(stateFlag))
                );
            }
            prefixes.push_back(std::string(componentPrefix) + variantSuffix);
        }

        for (const StyleState stateFlag : activeStates) {
            prefixes.push_back(std::string(componentPrefix) + std::string(stateString(stateFlag)));
        }

        prefixes.push_back(std::string(componentPrefix));
    }

    return prefixes;
}

// ─── Cache key packing ─────────────────────────────────────────────────────
//
// Packs a (StyleContext, StyleProperty) pair into a uint32_t for use as an
// unordered_map key. Bit layout:
//
//   bits  0– 4 : StyleComponent  (5 bits, up to 32 values)
//   bits  5– 8 : StyleState      (4-bit bitmask)
//   bits  9–14 : StyleProperty   (6 bits, up to 64 values)
//   bits 15–.. : VariantSlots    (4 bits each, starting at bit 15)
//
// Adding a new VariantSlot or enum value does not require changing this function.

uint32_t packVariant(const VariantKey& variant)
{
    uint32_t packed = 0;
    for (size_t index = 0; index < variant.slots.size(); ++index) {
        packed |= static_cast<uint32_t>(variant.slots.at(index)) << (index * 4);
    }
    return packed;
}

// clang-format off
// Bit offsets within the packed cache key.
constexpr uint32_t componentBitOffset = 0;
constexpr uint32_t stateBitOffset     = 5;   // component (5 bits) ends at bit 4
constexpr uint32_t propertyBitOffset  = 9;   // state (4-bit bitmask) ends at bit 8
constexpr uint32_t variantBitOffset   = 15;  // property (6 bits) ends at bit 14
// clang-format on

uint32_t packCacheKey(const StyleContext& context, StyleProperty property)
{
    // clang-format off
    return (static_cast<uint32_t>(context.component)                << componentBitOffset)
         | (static_cast<uint32_t>(context.state.toUnderlyingType()) << stateBitOffset)
         | (static_cast<uint32_t>(property)                         << propertyBitOffset)
         | (packVariant(context.variant)                            << variantBitOffset);
    // clang-format on
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

        // Fill outer rect with border colour.
        painter->fillPath(roundedRectPath(QRectF(rect), rule.borderRadius), QBrush(*rule.borderColor));
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

    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

std::optional<StyleParameters::Value> FreeCADStyle::resolve(std::string_view name) const
{
    return Application::Instance->styleParameterManager()->resolve(std::string(name));
}

std::optional<StyleParameters::Value> FreeCADStyle::resolve(
    std::initializer_list<std::string_view> names
) const
{
    for (const std::string_view name : names) {
        if (auto value = resolve(name)) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<StyleParameters::Value> FreeCADStyle::resolve(
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

StyleContext FreeCADStyle::contextOf(const QWidget* widget, const QStyleOption* option)
{
    StyleContext context;

    if (qobject_cast<const QToolButton*>(widget)) {
        context.component = StyleComponent::ToolButton;
    }
    else if (qobject_cast<const QPushButton*>(widget)) {
        context.component = StyleComponent::PushButton;
    }

    // ButtonType — derived from style option features first, then widget properties.
    const auto* buttonOption = qstyleoption_cast<const QStyleOptionButton*>(option);
    if (buttonOption && (buttonOption->features & QStyleOptionButton::DefaultButton)) {
        context.variant.set(VariantSlot::ButtonType, ButtonType::Primary);
    }
    else if (buttonOption && (buttonOption->features & QStyleOptionButton::Flat)) {
        context.variant.set(VariantSlot::ButtonType, ButtonType::Link);
    }
    else if (const auto* toolButton = qobject_cast<const QToolButton*>(widget);
             toolButton && toolButton->autoRaise()) {
        context.variant.set(VariantSlot::ButtonType, ButtonType::Link);
    }
    else if (widget && widget->property("flat").toBool()) {
        context.variant.set(VariantSlot::ButtonType, ButtonType::Link);
    }

    // ControlSize — derived from the "controlSize" widget property.
    if (widget) {
        const QString sizeName = widget->property("controlSize").toString();
        if (sizeName == u"small") {
            context.variant.set(VariantSlot::ControlSize, ControlSize::Small);
        }
        else if (sizeName == u"big") {
            context.variant.set(VariantSlot::ControlSize, ControlSize::Big);
        }
    }

    // State — all active flags captured as a bitmask.
    if (option) {
        if (option->state & QStyle::State_Sunken) {
            context.state |= StyleState::Pressed;
        }
        if (option->state & QStyle::State_MouseOver) {
            context.state |= StyleState::Hovered;
        }
        if (option->state & QStyle::State_On) {
            context.state |= StyleState::Checked;
        }
        if (option->state & QStyle::State_HasFocus) {
            context.state |= StyleState::Focused;
        }
    }

    return context;
}

std::optional<StyleParameters::Value> FreeCADStyle::resolve(
    const StyleContext& context,
    StyleProperty property
) const
{
    const uint32_t key = packCacheKey(context, property);

    if (const auto found = tokenCache.find(key); found != tokenCache.end()) {
        return found->second;
    }

    const std::vector<std::string> prefixes = buildPrefixes(context);
    const std::string_view propertySuffix = propertyString(property);

    std::optional<StyleParameters::Value> result;
    for (const std::string& prefix : prefixes) {
        result = resolve(prefix + std::string(propertySuffix));
        if (result) {
            break;
        }
    }

    tokenCache.emplace(key, result);
    return result;
}

FreeCADStyle::BoxStyleDefinition FreeCADStyle::resolveBoxStyle(const StyleContext& context) const
{
    BoxStyleDefinition result;

    if (const auto backgroundValue = resolve(context, StyleProperty::Background)) {
        result.background = Base::convertTo<QBrush>(*backgroundValue);
    }

    if (const auto overlay = resolve<Base::Color>(context, StyleProperty::Overlay)) {
        result.overlay = overlay->asValue<QColor>();
    }

    if (const auto borderColor = resolve<Base::Color>(context, StyleProperty::BorderColor)) {
        result.borderColor = borderColor->asValue<QColor>();
    }

    if (const auto borderThickness
        = resolve<StyleParameters::Insets>(context, StyleProperty::BorderThickness)) {
        result.borderThickness = Base::convertTo<QMarginsF>(*borderThickness);
    }

    if (const auto borderRadius
        = resolve<StyleParameters::Corners>(context, StyleProperty::BorderRadius)) {
        result.borderRadius = Base::convertTo<CornerRadii>(*borderRadius);
    }

    if (const auto innerShadow
        = resolve<StyleParameters::InnerShadow>(context, StyleProperty::InnerShadow)) {
        result.innerShadow = Base::convertTo<InnerShadow>(*innerShadow);
    }

    return result;
}

FreeCADStyle::BoxGeometryDefinition FreeCADStyle::resolveBoxGeometry(const StyleContext& context) const
{
    tokenCache.clear();
}

bool FreeCADStyle::eventFilter(QObject* obj, QEvent* event)
{
    // This is a hacky fix for https://github.com/FreeCAD/FreeCAD/issues/23607
    // Basically after widget is shown or polished we enforce it's minimum size to at least cover
    // the minimum size hint - something that QSS ignores if min-width is specified
    if (event->type() == QEvent::Polish || event->type() == QEvent::Show) {
        if (auto* btn = qobject_cast<QPushButton*>(obj)) {
            btn->setMinimumWidth(std::max(btn->minimumSizeHint().width(), btn->minimumWidth()));
        }
    }

    if (event->type() == QEvent::Polish) {
        if (auto* groupBox = qobject_cast<QGroupBox*>(obj)) {
            if (auto* layout = groupBox->layout()) {
                layout->setContentsMargins(0, 0, 0, 0);
            }
        }
    }

    return QObject::eventFilter(obj, event);
}
