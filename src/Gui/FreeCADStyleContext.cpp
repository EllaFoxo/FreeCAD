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
    {StyleComponent::PushButton,  {"Button", "FormControl"}},
    {StyleComponent::ToolButton,  {"ToolButton", "Button", "FormControl"}},
    {StyleComponent::LineEdit,    {"LineEdit", "FormControl"}},
    {StyleComponent::TextEdit,    {"TextEdit", "LineEdit", "FormControl"}},
    {StyleComponent::Select,      {"Select", "Button", "FormControl"}},
    {StyleComponent::ComboBox,    {"ComboBox", "LineEdit", "FormControl"}},
    {StyleComponent::List,        {"List"}},
    {StyleComponent::Tree,        {"Tree", "List"}},
    {StyleComponent::CheckBox,    {"CheckBox", "FormControl"}},
    {StyleComponent::RadioButton, {"RadioButton", "CheckBox", "FormControl"}},
    {StyleComponent::TabBar,      {"TabBar"}},
    {StyleComponent::TabWidget,   {"TabWidget"}},
};
// clang-format on

std::span<const std::string_view> componentChain(StyleComponent component)
{
    return std::span<const std::string_view>(lookup(componentChains, component));
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
    {StyleProperty::IconSize,        "IconSize"},
    {StyleProperty::IconSpacing,     "IconSpacing"},
    {StyleProperty::FontSize,        "FontSize"},
    {StyleProperty::FontWeight,      "FontWeight"},
    {StyleProperty::Background,      "Background"},
    {StyleProperty::TextColor,       "TextColor"},
    {StyleProperty::Overlay,         "Overlay"},
    {StyleProperty::OverlayOpacity,  "OverlayOpacity"},
    {StyleProperty::InnerShadow,     "InnerShadow"},
    {StyleProperty::TickColor,       "TickColor"},
    {StyleProperty::MenuWidth,       "MenuWidth"},
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
//   bits  0– 3 : StyleComponent        (4 bits, up to 16 values)
//   bits  4– 5 : StyleComponentElement (2 bits, up to 4 values)
//   bits  6–10 : StyleState            (5-bit bitmask)
//   bits 11–15 : StyleProperty         (5 bits, up to 32 values)
//   bits 16–24 : VariantSlots          (3 bits each × 3 slots, starting at bit 16)
//   bits 25–31 : componentOverrideId   (7 bits; 0 = no override, 1–127 interned)

uint32_t packVariant(const VariantKey& variant)
{
    uint32_t packed = 0;
    for (size_t index = 0; index < variant.slots.size(); ++index) {
        packed |= static_cast<uint32_t>(variant.slots.at(index)) << (index * 3);
    }
    return packed;
}

// clang-format off
// Bit offsets within the packed cache key.
constexpr uint32_t componentBitOffset = 0;
constexpr uint32_t elementBitOffset   = 4;   // component (4 bits) ends at bit 3
constexpr uint32_t stateBitOffset     = 7;   // element (3 bits) ends at bit 6
constexpr uint32_t propertyBitOffset  = 12;  // state (5-bit bitmask) ends at bit 11
constexpr uint32_t variantBitOffset   = 17;  // property (5 bits) ends at bit 16
constexpr uint32_t overrideBitOffset  = 26;  // variant slots (3 × 3 bits) end at bit 25
// clang-format on

uint32_t packCacheKey(const StyleContext& context, StyleProperty property, uint8_t overrideId)
{
    // clang-format off
    return (static_cast<uint32_t>(context.component)                << componentBitOffset)
         | (static_cast<uint32_t>(context.element)                  << elementBitOffset)
         | (static_cast<uint32_t>(context.state.toUnderlyingType()) << stateBitOffset)
         | (static_cast<uint32_t>(property)                         << propertyBitOffset)
         | (packVariant(context.variant)                            << variantBitOffset)
         | (static_cast<uint32_t>(overrideId)                       << overrideBitOffset);
    // clang-format on
}

}  // namespace

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
        context.component = StyleComponent::ToolButton;
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
        context.component = StyleComponent::List;
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
    const uint8_t overrideId = context.componentOverride.empty()
        ? uint8_t(0)
        : internComponentOverride(context.componentOverride);
    const uint32_t key = packCacheKey(context, property, overrideId);

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

    if (const auto borderOverlay = resolve<Base::Color>(context, StyleProperty::BorderOverlay)) {
        result.borderOverlay = borderOverlay->asValue<QColor>();
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
    componentOverrideIds.clear();
    nextComponentOverrideId = 1;
}
