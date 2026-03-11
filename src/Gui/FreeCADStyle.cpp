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
#include "StyleParameters/Insets.h"
#include "StyleParameters/ParameterManager.h"

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
QMarginsF convertTo<QMarginsF, StyleParameters::Insets>(const StyleParameters::Insets& insets)
{
    return QMarginsF(insets.left().value, insets.top().value, insets.right().value, insets.bottom().value);
}

template<>
QBrush convertTo<QBrush, StyleParameters::Value>(const StyleParameters::Value& value)
{
    using namespace StyleParameters;

    if (value.holds<::Base::Color>()) {
        return QBrush(value.get<::Base::Color>().asValue<QColor>());
    }

    if (!value.holds<Tuple>()) {
        return Qt::NoBrush;
    }

    const Tuple& tuple = value.get<Tuple>();

    const auto applyStopsAndBuild = [](auto qGradient, const auto& gradient) -> QBrush {
        qGradient.setCoordinateMode(QGradient::ObjectMode);
        for (const auto& stop : gradient.colorStops()) {
            qGradient.setColorAt(stop.position.value, stop.color.template asValue<QColor>());
        }
        return QBrush(qGradient);
    };

    if (tuple.kind == TupleKind::LinearGradient) {
        try {
            const LinearGradient gradient(tuple);
            return applyStopsAndBuild(
                QLinearGradient(gradient.x1(), gradient.y1(), gradient.x2(), gradient.y2()),
                gradient
            );
        }
        catch (const ::Base::Exception&) {
            return Qt::NoBrush;
        }
    }

    if (tuple.kind == TupleKind::RadialGradient) {
        try {
            const RadialGradient gradient(tuple);
            return applyStopsAndBuild(
                QRadialGradient(
                    gradient.cx(),
                    gradient.cy(),
                    gradient.radius(),
                    gradient.fx(),
                    gradient.fy()
                ),
                gradient
            );
        }
        catch (const ::Base::Exception&) {
            return Qt::NoBrush;
        }
    }

    return Qt::NoBrush;
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

    return QObject::eventFilter(obj, event);
}
