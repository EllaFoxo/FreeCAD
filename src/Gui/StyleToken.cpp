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

#include <array>
#include <map>
#include <span>
#include <string>
#include <vector>

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTextEdit>
#include <QTreeView>

#include <Base/Converter.h>

#include "StyleParameters/Corners.h"
#include "StyleParameters/InnerShadow.h"
#include "StyleParameters/Insets.h"
#include "Utilities.h"

#include <QLinearGradient>
#include <QToolBar>

using namespace Gui;

// Explicit specialization declarations for convertTo<> overloads defined in FreeCADStyle.cpp.
// Without these declarations the compiler falls back to the primary template, which fails
// for types that don't satisfy vec_traits.
namespace Base
{
template<>
FreeCADStyle::CornerRadii convertTo<FreeCADStyle::CornerRadii, StyleParameters::Corners>(
    const StyleParameters::Corners& corners
);

template<>
FreeCADStyle::InnerShadow convertTo<FreeCADStyle::InnerShadow, StyleParameters::InnerShadow>(
    const StyleParameters::InnerShadow& shadow
);
}  // namespace Base

namespace
{

// ─── Directional rotation helpers ───────────────────────────────────────────

/**
 * @brief Applies the standard 4-way edge rotation to an array of values.
 *
 * Canonical (North) order: (left/topLeft, top/topRight, right/bottomRight, bottom/bottomLeft).
 * South swaps opposite pairs; East/West rotate by one step in either direction.
 */
template<typename T>
std::array<T, 4> rotate4(std::array<T, 4> values, Position position)
{
    // clang-format off
    switch (position) {
        case Position::South: return {values[2], values[3], values[0], values[1]};
        case Position::East:  return {values[3], values[0], values[1], values[2]};
        case Position::West:  return {values[1], values[2], values[3], values[0]};
        default:              return values;
    }
    // clang-format on
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

// ─── StyleToken string tables ──────────────────────────────────────────────
// All tables use std::map so entries are self-documenting and order-independent.

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
    {StyleComponent::PushButton,    {"Button", "FormControl"}},
    {StyleComponent::ToolButton,    {"ToolButton", "Button", "FormControl"}},
    {StyleComponent::LineEdit,      {"LineEdit", "FormControl"}},
    {StyleComponent::TextEdit,      {"TextEdit", "LineEdit", "FormControl"}},
    {StyleComponent::Select,        {"Select", "Button", "FormControl"}},
    {StyleComponent::ComboBox,      {"ComboBox", "LineEdit", "FormControl"}},
    {StyleComponent::List,          {"List"}},
    {StyleComponent::DropdownList,  {"DropdownList", "List"}},
    {StyleComponent::Tree,          {"Tree", "List"}},
    {StyleComponent::CheckBox,      {"CheckBox", "FormControl"}},
    {StyleComponent::RadioButton,   {"RadioButton", "CheckBox", "FormControl"}},
    {StyleComponent::TabBar,        {"TabBar"}},
    {StyleComponent::TabWidget,     {"TabWidget"}},
    {StyleComponent::ToolBar,       {"ToolBar"}},
    {StyleComponent::ToolBarButton, {"ToolBarButton", "ToolButton", "Button", "FormControl"}}
};
// clang-format on

std::span<const std::string_view> componentChain(StyleComponent component)
{
    return lookup(componentChains, component);
}

// ── Element string table ─────────────────────────────────────────────────────
// Root (0) is absent; elementString returns "" for it, so Root components
// produce the same token prefixes as before this field was introduced.

// clang-format off
const std::map<StyleComponentElement, std::string_view> elementNames = {
    {StyleComponentElement::Item,      "Item"},
    {StyleComponentElement::Indicator, "Indicator"},
    {StyleComponentElement::Tab,       "Tab"},
    {StyleComponentElement::Base,      "Base"},
    {StyleComponentElement::Menu,      "Menu"},
};
// clang-format on

std::string_view elementString(StyleComponentElement element)
{
    return lookup(elementNames, element);
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
    {VariantSlot::Position, {
        {static_cast<uint8_t>(Position::East),  "East"},
        {static_cast<uint8_t>(Position::South), "South"},
        {static_cast<uint8_t>(Position::West),  "West"},
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
    {StyleState::Disabled, "Disabled"},
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
    {StyleProperty::BorderOverlay,   "BorderOverlay"},
    {StyleProperty::Padding,         "Padding"},
    {StyleProperty::Margin,          "Margin"},
    {StyleProperty::Spacing,         "Spacing"},
    {StyleProperty::Overlap,         "Overlap"},
    {StyleProperty::IconSize,        "IconSize"},
    {StyleProperty::IconSpacing,     "IconSpacing"},
    {StyleProperty::FontSize,        "FontSize"},
    {StyleProperty::FontWeight,      "FontWeight"},
    {StyleProperty::Background,      "Background"},
    {StyleProperty::TextColor,       "TextColor"},
    {StyleProperty::Overlay,         "Overlay"},
    {StyleProperty::OverlayOpacity,  "OverlayOpacity"},
    {StyleProperty::InnerShadow,     "InnerShadow"},
    {StyleProperty::IconColor,       "IconColor"},
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
//   "ButtonPrimaryHovered"   ← variant + highest-priority state
//   "ButtonPrimaryFocused"   ← variant + next state
//   "ButtonPrimary"          ← variant, no state
//   "ButtonHovered"          ← no variant, highest-priority state
//   "ButtonFocused"          ← no variant, next state
//   "Button"                 ← baseline

// Priority order — highest first. Mirrors the enum declaration order (Disabled > Pressed > Hovered > …).
constexpr auto statePriorityOrder = std::to_array({
    StyleState::Disabled,
    StyleState::Pressed,
    StyleState::Hovered,
    StyleState::Checked,
    StyleState::Focused,
});

std::vector<std::string> buildPrefixes(const StyleContext& context)
{
    const std::string elementSuffix = std::string(elementString(context.element));
    const std::string variantSuffix = variantString(context.variant);

    std::vector<StyleState> activeStates;
    for (const StyleState stateFlag : statePriorityOrder) {
        if (context.state.testFlag(stateFlag)) {
            activeStates.push_back(stateFlag);
        }
    }

    std::vector<std::string> prefixes;

    // Helper that appends all variant+state combinations for a given base prefix
    // (componentWithElement). Mirrors the inner logic for each chain entry below.
    const auto appendPrefixEntries = [&](const std::string& componentWithElement) {
        if (!variantSuffix.empty()) {
            for (const StyleState stateFlag : activeStates) {
                prefixes.push_back(
                    componentWithElement + variantSuffix + std::string(stateString(stateFlag))
                );
            }
            prefixes.push_back(componentWithElement + variantSuffix);
        }

        for (const StyleState stateFlag : activeStates) {
            prefixes.push_back(componentWithElement + std::string(stateString(stateFlag)));
        }

        prefixes.push_back(componentWithElement);
    };

    // Component override (from the widget's "component" dynamic property) is tried first,
    // before any entry in the normal component chain.
    if (!context.componentOverride.empty()) {
        appendPrefixEntries(context.componentOverride + elementSuffix);
    }

    for (const std::string_view componentPrefix : componentChain(context.component)) {
        appendPrefixEntries(std::string(componentPrefix) + elementSuffix);
    }

    return prefixes;
}

// ─── Cache key packing ─────────────────────────────────────────────────────
//
//   bits  0– 7 : StyleComponent        (8 bits → 256 values)
//   bits  8–11 : StyleComponentElement (4 bits → 16 values)
//   bits 12–16 : StyleState            (5-bit bitmask, unchanged)
//   bits 17–23 : StyleProperty         (7 bits → 128 values; 0 for context-only keys)
//   bits 24–31 : componentOverrideId   (8 bits → 256 IDs, up from 64)
//   bits 32–47 : VariantSlots          (4 bits/slot × 4 slots; moved to high bits)
//   bits 48–63 : reserved

uint64_t packVariant(const VariantKey& variant)
{
    uint64_t packed = 0;
    for (size_t index = 0; index < variant.slots.size(); ++index) {
        packed |= static_cast<uint64_t>(variant.slots.at(index)) << (index * 4);
    }
    return packed;
}

// clang-format off
// Bit offsets within the packed 64-bit cache key.
constexpr uint64_t componentBitOffset = 0;
constexpr uint64_t elementBitOffset   = 8;   // component (8 bits) ends at bit 7
constexpr uint64_t stateBitOffset     = 12;  // element (4 bits) ends at bit 11
constexpr uint64_t propertyBitOffset  = 17;  // state (5-bit bitmask) ends at bit 16
constexpr uint64_t overrideBitOffset  = 24;  // property (7 bits) ends at bit 23
constexpr uint64_t variantBitOffset   = 32;  // override (8 bits) ends at bit 31
// clang-format on

uint64_t packContextKeyImpl(const StyleContext& context, uint8_t overrideId)
{
    // clang-format off
    return (static_cast<uint64_t>(context.component)                << componentBitOffset)
         | (static_cast<uint64_t>(context.element)                  << elementBitOffset)
         | (static_cast<uint64_t>(context.state.toUnderlyingType()) << stateBitOffset)
         | (static_cast<uint64_t>(overrideId)                       << overrideBitOffset)
         | (packVariant(context.variant)                            << variantBitOffset);
    // clang-format on
}

}  // namespace

uint64_t FreeCADStyle::packContextKey(const StyleContext& context) const
{
    const uint8_t overrideId = context.componentOverride.empty()
        ? static_cast<uint8_t>(0)
        : internComponentOverride(context.componentOverride);
    return packContextKeyImpl(context, overrideId);
}

uint64_t FreeCADStyle::packCacheKey(const StyleContext& context, StyleProperty property) const
{
    return packContextKey(context) | (static_cast<uint64_t>(property) << propertyBitOffset);
}

// ─── Tab position mapping ────────────────────────────────────────────────────

/**
 * @brief Maps a QTabBar::Shape to the canonical Position.
 *
 * Both Rounded and Triangular shapes at the same edge map to the same position.
 */
Position FreeCADStyle::tabPositionOf(QTabBar::Shape shape)
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

// ─── Context building ────────────────────────────────────────────────────────

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
    else if (qobject_cast<const QToolBar*>(widget)) {
        context.component = StyleComponent::ToolBar;
        context.element = element;
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

    // Component override — derived from the "component" widget property.
    // Allows a widget to opt into a custom token namespace (e.g. "ActionButton")
    // while still falling back to the standard component chain.
    if (widget) {
        const QString overrideName = widget->property("component").toString();
        if (!overrideName.isEmpty()) {
            context.componentOverride = overrideName.toStdString();
        }
    }

    // State — all active flags captured as a bitmask.
    if (option) {
        if (!(option->state & QStyle::State_Enabled)) {
            context.state |= StyleState::Disabled;
        }

        // State_Sunken means "button is being pressed" for buttons, but "has a sunken
        // frame appearance" for input widgets (QLineEdit always sets it). Only map it
        // to Pressed for button components to avoid masking the Focused state on inputs.
        const bool isButton = context.component == StyleComponent::PushButton
            || context.component == StyleComponent::ToolButton
            || context.component == StyleComponent::Select
            || context.component == StyleComponent::CheckBox
            || context.component == StyleComponent::RadioButton;
        if (isButton && (option->state & QStyle::State_Sunken)) {
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

    return context;
}

// ─── Token resolution ────────────────────────────────────────────────────────

std::optional<StyleParameters::Value> FreeCADStyle::resolve(
    const StyleContext& context,
    StyleProperty property
) const
{
    const uint64_t key = packCacheKey(context, property);

    if (const auto* cached = tokenCache.find(key)) {
        return *cached;
    }

    const std::vector<std::string> prefixes = buildPrefixes(context);
    const std::string_view propertySuffix = propertyString(property);

    std::optional<StyleParameters::Value> result;
    for (const std::string& prefix : prefixes) {
        auto candidate = resolve(prefix + std::string(propertySuffix));
        if (!candidate) {
            continue;  // not defined at this level — try next prefix
        }
        if (candidate->holds<StyleParameters::None>()) {
            break;  // explicitly reset — stop the chain, result stays nullopt
        }
        result = std::move(candidate);
        break;
    }

    tokenCache.store(key, result);
    return result;
}

FreeCADStyle::BoxStyleDefinition FreeCADStyle::resolveBoxStyle(const StyleContext& context) const
{
    const uint64_t key = packContextKey(context);

    if (const auto* cached = boxStyleCache.find(key)) {
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
    if (const auto borderRadius
        = resolve<StyleParameters::Corners>(northContext, StyleProperty::BorderRadius)) {
        result.borderRadius = rotated(Base::convertTo<CornerRadii>(*borderRadius), position);
    }
    if (const auto borderThickness
        = resolve<StyleParameters::Insets>(northContext, StyleProperty::BorderThickness)) {
        result.borderThickness = rotated(Base::convertTo<QMarginsF>(*borderThickness), position);
    }

    // Non-directional visual tokens resolved from the actual context.
    if (const auto overlay = resolve<Base::Color>(context, StyleProperty::Overlay)) {
        result.overlay = overlay->asValue<QColor>();
    }
    if (const auto borderColor = resolve<Base::Color>(context, StyleProperty::BorderColor)) {
        result.borderColor = borderColor->asValue<QColor>();
    }
    if (const auto borderOverlay = resolve<Base::Color>(context, StyleProperty::BorderOverlay)) {
        result.borderOverlay = borderOverlay->asValue<QColor>();
    }
    if (const auto innerShadow
        = resolve<StyleParameters::InnerShadow>(context, StyleProperty::InnerShadow)) {
        result.innerShadow = Base::convertTo<InnerShadow>(*innerShadow);
    }

    boxStyleCache.store(key, result);
    return result;
}

FreeCADStyle::BoxGeometryDefinition FreeCADStyle::resolveBoxGeometry(const StyleContext& context) const
{
    const uint64_t key = packContextKey(context);

    if (const auto* cached = boxGeometryCache.find(key)) {
        return *cached;
    }

    BoxGeometryDefinition result;

    if (const auto padding = resolve<StyleParameters::Insets>(context, StyleProperty::Padding)) {
        result.padding = Base::convertTo<QMarginsF>(*padding);
    }

    if (const auto height = resolve<StyleParameters::Numeric>(context, StyleProperty::Height)) {
        result.height = static_cast<int>(height->value);
    }

    if (const auto minWidth = resolve<StyleParameters::Numeric>(context, StyleProperty::MinWidth)) {
        result.minWidth = static_cast<int>(minWidth->value);
    }

    if (const auto resolvedWidth = resolve<StyleParameters::Numeric>(context, StyleProperty::Width)) {
        result.width = static_cast<int>(resolvedWidth->value);
    }

    if (const auto resolvedMaxWidth
        = resolve<StyleParameters::Numeric>(context, StyleProperty::MaxWidth)) {
        result.maxWidth = static_cast<int>(resolvedMaxWidth->value);
    }

    if (const auto resolvedMinHeight
        = resolve<StyleParameters::Numeric>(context, StyleProperty::MinHeight)) {
        result.minHeight = static_cast<int>(resolvedMinHeight->value);
    }

    if (const auto resolvedMaxHeight
        = resolve<StyleParameters::Numeric>(context, StyleProperty::MaxHeight)) {
        result.maxHeight = static_cast<int>(resolvedMaxHeight->value);
    }

    if (const auto spacing = resolve<StyleParameters::Numeric>(context, StyleProperty::IconSpacing)) {
        result.iconSpacing = static_cast<int>(spacing->value);
    }

    boxGeometryCache.store(key, result);
    return result;
}

// ─── Component override interning ────────────────────────────────────────────

uint8_t FreeCADStyle::internComponentOverride(const std::string& name) const
{
    if (const auto found = componentOverrideIds.find(name); found != componentOverrideIds.end()) {
        return found->second;
    }

    const uint8_t id = nextComponentOverrideId++;
    componentOverrideIds.emplace(name, id);
    return id;
}

void FreeCADStyle::clearTokenCache()
{
    tokenCache.clear();
    boxStyleCache.clear();
    boxGeometryCache.clear();
    componentOverrideIds.clear();
    nextComponentOverrideId = 1;
}

StyleContext FreeCADStyle::withNorthPosition(const StyleContext& context)
{
    StyleContext north = context;
    north.variant.set(VariantSlot::Position, Position::North);
    return north;
}
