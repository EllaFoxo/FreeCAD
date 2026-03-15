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
#include <QTextEdit>
#include <QLinearGradient>
#include <QPainterPath>
#include <QStyleOption>
#include <QRadialGradient>
#include <QStyleOption>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QRadioButton>
#include <QListView>
#include <QStyleOptionViewItem>
#include <QTreeView>
#include <QTabBar>
#include <QToolBar>

#include <Base/Color.h>
#include <Base/Converter.h>
#include <Base/Exception.h>

#include "Application.h"
#include "QuantitySpinBox_p.h"
#include "ThemeReloadEvent.h"
#include "Utilities.h"
#include "StyleParameters/Corners.h"
#include "StyleParameters/Gradient.h"
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
// Given component="Button", variant="Primary", active states={Hovered, Focused}
// the result is:
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
// Packs a (StyleContext, StyleProperty, componentOverrideId) tuple into a
// uint32_t for use as an unordered_map key. Bit layout:
//
//   bits  0– 3 : StyleComponent        (4 bits, up to 16 values)
//   bits  4– 5 : StyleComponentElement (2 bits, up to 4 values)
//   bits  6–10 : StyleState            (5-bit bitmask)
//   bits 11–15 : StyleProperty         (5 bits, up to 32 values)
//   bits 16–24 : VariantSlots          (3 bits each × 3 slots, starting at bit 16)
//   bits 25–31 : componentOverrideId   (7 bits; 0 = no override, 1–127 interned)
//
// 3 bits per slot: ButtonType (3 values), ControlSize (3 values), Position (4 values)
// all fit in 3 bits (≤ 7). 7-bit override IDs allow 127 unique custom component names.
//
// Adding a new VariantSlot or enum value does not require changing this function
// as long as each slot's value count stays ≤ 7.

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

// ─── Tab bar utilities ──────────────────────────────────────────────────────

/**
 * @brief Maps a QTabBar::Shape to the canonical Position.
 *
 * Both Rounded and Triangular shapes at the same edge map to the same position.
 */
Position tabPositionOf(QTabBar::Shape shape)
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
    const auto rotatePoint = [position](const QPointF& p) -> QPointF {
        switch (position) {
            case Position::South: return {p.x(),       1.0 - p.y()};
            case Position::East:  return {1.0 - p.y(), p.x()      };
            case Position::West:  return {p.y(),       1.0 - p.x()};
            default:              return p;
        }
    };

    QLinearGradient result(rotatePoint(linear->start()), rotatePoint(linear->finalStop()));
    result.setStops(linear->stops());
    result.setCoordinateMode(linear->coordinateMode());
    result.setSpread(linear->spread());

    return result;
}
// clang-format on


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

int FreeCADStyle::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const
{
    if (qobject_cast<const QRadioButton*>(widget)) {
        const StyleContext context = contextOf(widget, option);

        if (metric == PM_ExclusiveIndicatorWidth) {
            if (const auto width = resolve<StyleParameters::Numeric>(context, StyleProperty::Width)) {
                return static_cast<int>(width->value);
            }
        }

        if (metric == PM_ExclusiveIndicatorHeight) {
            if (const auto height = resolve<StyleParameters::Numeric>(context, StyleProperty::Height)) {
                return static_cast<int>(height->value);
            }
        }

        if (metric == PM_CheckBoxLabelSpacing) {
            if (const auto spacing = resolve<StyleParameters::Numeric>("CheckBoxSpacing")) {
                return static_cast<int>(spacing->value);
            }
        }
    }

    if (qobject_cast<const QCheckBox*>(widget)) {
        const StyleContext context = contextOf(widget, option);

        if (metric == PM_IndicatorWidth) {
            if (const auto width = resolve<StyleParameters::Numeric>(context, StyleProperty::Width)) {
                return static_cast<int>(width->value);
            }
        }

        if (metric == PM_IndicatorHeight) {
            if (const auto height = resolve<StyleParameters::Numeric>(context, StyleProperty::Height)) {
                return static_cast<int>(height->value);
            }
        }

        if (metric == PM_CheckBoxLabelSpacing) {
            if (const auto spacing = resolve<StyleParameters::Numeric>("CheckBoxSpacing")) {
                return static_cast<int>(spacing->value);
            }
        }
    }

    if (qobject_cast<const QToolButton*>(widget) && metric == PM_MenuButtonIndicator) {
        const StyleContext context = contextOf(widget, option);
        if (const auto token = resolve<StyleParameters::Numeric>(context, StyleProperty::MenuWidth)) {
            return static_cast<int>(token->value);
        }
    }

    if (metric == PM_ToolBarItemSpacing) {
        if (const auto spacing = resolve<StyleParameters::Numeric>("ToolBarItemSpacing")) {
            return static_cast<int>(spacing->value);
        }
    }

    if (metric == PM_ToolBarItemMargin) {
        if (const auto spacing = resolve<StyleParameters::Numeric>("ToolBarItemMargin")) {
            return static_cast<int>(spacing->value);
        }
    }

    // PM_TabBarTabOverlap is a pure painting hint: it tells CE_TabBarTabShape how many pixels
    // to extend (positive) or shrink (negative) the trailing edge of each non-last tab's paint
    // rect. QTabBar's layoutTabs() does NOT query this metric; the visual spacing is achieved
    // entirely by adjusting the paint rect in drawTabBarTab.
    //
    // TabBarTabSpacing uses gap semantics (positive = gap, negative = overlap), so
    // overlap = -spacing. Default -1px → overlap 1 → 1px trailing extension hides shared border.
    if (metric == PM_TabBarTabOverlap) {
        if (const auto spacing = resolve<StyleParameters::Numeric>("TabBarTabSpacing")) {
            return static_cast<int>(-spacing->value);
        }
    }

    // PM_TabBarTabHSpace / PM_TabBarTabVSpace feed into
    // QCommonStyle::sizeFromContents(CT_TabBarTab). Driving padding through these metrics preserves
    // Qt's close-button width, minimum-size constraints, and all other CT_TabBarTab logic. North
    // position is used because QTabBar::tabSizeHint() transposes the returned size for East/West
    // tabs itself. State is preserved (e.g. checked tabs use TabBarTabCheckedPadding).
    if (metric == PM_TabBarTabHSpace || metric == PM_TabBarTabVSpace) {
        StyleContext context = contextOf(widget, option, StyleComponentElement::Tab);
        context.variant.set(VariantSlot::Position, Position::North);
        if (const auto padding = resolve<StyleParameters::Insets>(context, StyleProperty::Padding)) {
            const QMarginsF margins = Base::convertTo<QMarginsF>(*padding);
            if (metric == PM_TabBarTabHSpace) {
                return static_cast<int>(margins.left() + margins.right());
            }
            return static_cast<int>(margins.top() + margins.bottom());
        }
    }

    // PM_TabBarBaseHeight / PM_TabBarBaseOverlap are only meaningful for a standalone QTabBar
    // that actually draws its base strip (PE_FrameTabBarBase). QCommonStyle also queries
    // PM_TabBarBaseOverlap via SE_TabWidgetTabPane with widget = QTabWidget to compute the
    // pane inset — returning our large overlap there would push the frame into the tab row.
    // Guard on widget being a QTabBar so the QTabWidget pane calculation gets 0 (flush).
    if (qobject_cast<const QTabBar*>(widget)) {
        if (metric == PM_TabBarBaseHeight) {
            const auto height = resolve<StyleParameters::Numeric>("TabBarBaseHeight");
            const auto overlap = resolve<StyleParameters::Numeric>("TabBarBaseOverlap");
            if (height) {
                const int overlapPx = overlap ? static_cast<int>(overlap->value) : 0;
                return static_cast<int>(height->value) + overlapPx;
            }
        }
        if (metric == PM_TabBarBaseOverlap) {
            if (const auto overlap = resolve<StyleParameters::Numeric>("TabBarBaseOverlap")) {
                return static_cast<int>(overlap->value);
            }
        }
    }
    else if (metric == PM_TabBarBaseOverlap) {
        return 1;
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
        const auto* frameOption = qstyleoption_cast<const QStyleOptionFrame*>(option);
        if (frameOption && frameOption->lineWidth == 0) {
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
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);

            QColor dotColor = option->palette.text().color();
            if (const auto tickColor = resolve<Base::Color>(context, StyleProperty::TickColor)) {
                dotColor = tickColor->asValue<QColor>();
            }

            constexpr qreal dotPaddingRatio = 0.2;  // fallback: fraction of indicator width
            qreal padding = static_cast<qreal>(option->rect.width()) * dotPaddingRatio;
            if (const auto paddings
                = resolve<StyleParameters::Insets>(context, StyleProperty::Padding)) {
                padding = paddings->left().value;
            }

            painter->setBrush(dotColor);
            painter->drawEllipse(QRectF(option->rect).adjusted(padding, padding, -padding, -padding));
            painter->restore();
        }
        return;
    }

    if (element == PE_IndicatorCheckBox) {
        const StyleContext context = contextOf(widget, option, StyleComponentElement::Indicator);
        drawBoxBackground(painter, option->rect, resolveBoxStyle(context));

        const bool isChecked = option->state & QStyle::State_On;
        const bool isPartial = option->state & QStyle::State_NoChange;

        if (isChecked || isPartial) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);

            // Tick colour: from design token, falling back to the Fusion palette role so
            // it follows the active theme even without an explicit token.
            QColor markColor = option->palette.text().color();
            if (const auto tickColor = resolve<Base::Color>(context, StyleProperty::TickColor)) {
                markColor = tickColor->asValue<QColor>();
            }

            // Inner mark padding: from design token, falling back to a proportional default.
            constexpr qreal checkPaddingRatio = 0.2;  // fallback: fraction of box width
            constexpr qreal checkPenWidthRatio = 0.15;  // stroke width as fraction of inner rect width
            constexpr qreal checkMinPenWidth = 1.5;     // minimum stroke width in pixels

            qreal padding = static_cast<qreal>(option->rect.width()) * checkPaddingRatio;
            if (const auto paddings
                = resolve<StyleParameters::Insets>(context, StyleProperty::Padding)) {
                padding = paddings->left().value;
            }

            const QRectF innerRect
                = QRectF(option->rect).adjusted(padding, padding, -padding, -padding);
            const qreal penWidth = qMax(checkMinPenWidth, innerRect.width() * checkPenWidthRatio);

            if (isChecked) {
                // Proportional anchor points for the check mark path (relative to inner rect).
                constexpr qreal checkMidY = 0.5;   // vertical mid-point of the left arm
                constexpr qreal checkKneeX = 0.4;  // horizontal position of the knee (valley)

                QPainterPath checkPath;
                checkPath.moveTo(innerRect.left(), innerRect.top() + (innerRect.height() * checkMidY));
                checkPath.lineTo(
                    innerRect.left() + (innerRect.width() * checkKneeX),
                    innerRect.bottom()
                );
                checkPath.lineTo(innerRect.right(), innerRect.top());
                painter->strokePath(
                    checkPath,
                    QPen(markColor, penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)
                );
            }
            else {
                // Partial check: horizontal dash
                painter->setPen(QPen(markColor, penWidth, Qt::SolidLine, Qt::RoundCap));
                painter->drawLine(
                    QPointF(innerRect.left(), innerRect.center().y()),
                    QPointF(innerRect.right(), innerRect.center().y())
                );
            }

            painter->restore();
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

QSize FreeCADStyle::sizeFromContents(
    ContentsType type,
    const QStyleOption* option,
    const QSize& size,
    const QWidget* widget
) const
{
    if (type == CT_PushButton) {
        const StyleContext context = contextOf(widget, option);
        const auto* btnOption = qstyleoption_cast<const QStyleOptionButton*>(option);
        const BoxGeometryDefinition geometry = resolveBoxGeometry(context);

        int width = size.width() + geometry.paddingH();
        int height = size.height() + geometry.paddingV();

        // Fix icon-text spacing contribution (Qt hardcodes qtBuiltInIconGap px).
        if (btnOption && !btnOption->icon.isNull() && !btnOption->text.isEmpty()) {
            width += geometry.iconGapDelta();
        }

        if (geometry.height) {
            height = *geometry.height;
        }
        if (geometry.minWidth) {
            width = std::max(width, *geometry.minWidth);
        }

        return {width, height};
    }

    if (type == CT_ComboBox) {
        const StyleContext context = contextOf(widget, option);
        const auto* comboOption = qstyleoption_cast<const QStyleOptionComboBox*>(option);
        const BoxGeometryDefinition geometry = resolveBoxGeometry(context);
        QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);

        // QComboBox::sizeHint bakes iconSize.width() + qtBuiltInIconGap into the content
        // size it passes here when the current item has an icon.  Replace that gap with
        // the token value, matching the layout used in drawComboBoxLabel.
        if (comboOption && !comboOption->currentIcon.isNull()) {
            result.rwidth() += geometry.iconGapDelta();
        }

        if (geometry.height) {
            result.setHeight(*geometry.height);
        }
        return result;
    }

    if (type == CT_TabBarTab) {
        const auto* tabOption = qstyleoption_cast<const QStyleOptionTab*>(option);
        if (tabOption && !tabOption->icon.isNull() && !tabOption->text.isEmpty()) {
            StyleContext geometryContext = contextOf(widget, option, StyleComponentElement::Tab);
            geometryContext.variant.set(VariantSlot::Position, Position::North);
            const BoxGeometryDefinition geometry = resolveBoxGeometry(geometryContext);
            QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
            result.rwidth() += geometry.iconGapDelta();
            return result;
        }
    }

    if (type == CT_LineEdit || type == CT_SpinBox) {
        const BoxGeometryDefinition geometry = resolveBoxGeometry(contextOf(widget, option));
        QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
        if (geometry.height) {
            result.setHeight(*geometry.height);
        }
        return result;
    }

    if (type == CT_ToolButton) {
        const StyleContext context = contextOf(widget, option);
        const auto* tbOption = qstyleoption_cast<const QStyleOptionToolButton*>(option);
        const BoxGeometryDefinition geometry = resolveBoxGeometry(context);

        const bool hasIconOrArrow = tbOption
            && (!tbOption->icon.isNull() || tbOption->arrowType != Qt::NoArrow);
        const bool needsCustomLayout = hasIconOrArrow && tbOption && !tbOption->text.isEmpty()
            && (tbOption->toolButtonStyle == Qt::ToolButtonTextBesideIcon
                || tbOption->toolButtonStyle == Qt::ToolButtonTextUnderIcon);

        int width = size.width() + geometry.paddingH();
        int height = size.height() + geometry.paddingV();

        if (needsCustomLayout && tbOption->toolButtonStyle == Qt::ToolButtonTextBesideIcon) {
            // Qt hardcodes qtBuiltInIconGap px as the icon-text gap in QToolButton::sizeHint.
            // Replace that with our spacing so the widget is wide enough.
            // width += geometry.iconGapDelta();
        }

        // For icon-only toolbar buttons, skip the fixed-height token so squareness emerges
        // naturally from the uniform padding. The height token still applies to all other
        // ToolButton contexts (form-control-style buttons with text, small/big variants, etc).
        const std::optional<Qt::Orientation> toolbarOrientation = toolbarOrientationOf(widget);
        const bool isToolbarIconOnly = toolbarOrientation.has_value() && tbOption
            && tbOption->toolButtonStyle == Qt::ToolButtonIconOnly;

        if (geometry.height && !isToolbarIconOnly) {
            height = *geometry.height;
        }

        const int menuWidth = proxy()->pixelMetric(PM_MenuButtonIndicator, option, widget);
        const bool hasMenu = tbOption->features & QStyleOptionToolButton::MenuButtonPopup;

        // QToolButton::sizeHint() adds PM_MenuButtonIndicator to the width for
        // MenuButtonPopup buttons before calling sizeFromContents, regardless of toolbar
        // orientation. For vertical toolbars the strip goes below the icon, so we move
        // the indicator contribution from width to height.
        if (tbOption && hasMenu && toolbarOrientation == Qt::Vertical) {
            width -= menuWidth;
            height += menuWidth;
        }

        if (geometry.minWidth) {
            width = std::max(
                width,
                *geometry.minWidth + ((hasMenu && toolbarOrientation != Qt::Vertical) ? menuWidth : 0)
            );
        }

        return {width, height};
    }

    if (type == CT_ItemViewItem) {
        const StyleContext context = contextOf(widget, option, StyleComponentElement::Item);
        const bool isItemComponent = context.element == StyleComponentElement::Item;
        if (!isItemComponent) {
            return QProxyStyle::sizeFromContents(type, option, size, widget);
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
            baseSize = QProxyStyle::sizeFromContents(type, option, size, widget);
        }

        return {baseSize.width() + geometry.paddingH(), baseSize.height() + geometry.paddingV()};
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
        adjustedOption.rect = vopt->rect.adjusted(
            static_cast<int>(geometry.padding.left()),
            static_cast<int>(geometry.padding.top()),
            -static_cast<int>(geometry.padding.right()),
            -static_cast<int>(geometry.padding.bottom())
        );
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

QRect FreeCADStyle::subControlRect(
    ComplexControl complexControl,
    const QStyleOptionComplex* option,
    SubControl subControl,
    const QWidget* widget
) const
{
    if (complexControl == CC_ComboBox) {
        const auto* comboOption = qstyleoption_cast<const QStyleOptionComboBox*>(option);
        if (comboOption) {
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
                    return QRect(
                        contentRect.left(),
                        contentRect.top(),
                        editRight - contentRect.left() + 1,
                        contentRect.height()
                    );
                case SC_ComboBoxArrow:
                    return QRect(arrowLeft, contentRect.top(), arrowWidth, contentRect.height());
                default:
                    break;
            }
        }
    }

    if (complexControl == CC_SpinBox) {
        const auto* spinOption = qstyleoption_cast<const QStyleOptionSpinBox*>(option);
        if (spinOption) {
            const BoxGeometryDefinition geometry = resolveBoxGeometry(contextOf(widget, option));
            const QRect outerRect = option->rect;
            const QRect contentRect = geometry.contentRect(outerRect);

            // Borrow the button width from the base style; only the position changes.
            const bool hasButtons = spinOption->buttonSymbols != QAbstractSpinBox::NoButtons;
            const int buttonWidth = hasButtons
                ? QProxyStyle::subControlRect(complexControl, option, SC_SpinBoxUp, widget).width()
                : 0;

            const int buttonLeft = contentRect.right() - buttonWidth + 1;
            const int editRight = hasButtons ? buttonLeft - 1 : contentRect.right();

            switch (subControl) {
                case SC_SpinBoxFrame:
                    return outerRect;
                case SC_SpinBoxEditField:
                    return QRect(
                        contentRect.left(),
                        contentRect.top(),
                        editRight - contentRect.left() + 1,
                        contentRect.height()
                    );
                case SC_SpinBoxUp: {
                    if (!hasButtons) {
                        return {};
                    }
                    const int halfHeight = contentRect.height() / 2;
                    return QRect(buttonLeft, contentRect.top(), buttonWidth, halfHeight);
                }
                case SC_SpinBoxDown: {
                    if (!hasButtons) {
                        return {};
                    }
                    const int halfHeight = contentRect.height() / 2;
                    return QRect(
                        buttonLeft,
                        contentRect.top() + halfHeight,
                        buttonWidth,
                        contentRect.height() - halfHeight
                    );
                }
                default:
                    break;
            }
        }
    }

    if (complexControl == CC_ToolButton) {
        if (const auto* tbOption = qstyleoption_cast<const QStyleOptionToolButton*>(option)) {
            if (tbOption->features & QStyleOptionToolButton::MenuButtonPopup) {
                const StyleContext context = contextOf(widget, option);
                const QRect rect = option->rect;

                int menuWidth = proxy()->pixelMetric(PM_MenuButtonIndicator, option, widget);
                if (const auto token
                    = resolve<StyleParameters::Numeric>(context, StyleProperty::MenuWidth)) {
                    menuWidth = static_cast<int>(token->value);
                }

                const bool isVertical = toolbarOrientationOf(widget) == Qt::Vertical;

                switch (subControl) {
                    case SC_ToolButton:
                        if (isVertical) {
                            return {rect.left(), rect.top(), rect.width(), rect.height() - menuWidth};
                        }
                        return {rect.left(), rect.top(), rect.width() - menuWidth, rect.height()};
                    case SC_ToolButtonMenu:
                        if (isVertical) {
                            return {
                                rect.left(),
                                rect.bottom() - menuWidth + 1,
                                rect.width(),
                                menuWidth,
                            };
                        }
                        return {rect.right() - menuWidth + 1, rect.top(), menuWidth, rect.height()};
                    default:
                        break;
                }
            }
        }
    }

    return QProxyStyle::subControlRect(complexControl, option, subControl, widget);
}

void FreeCADStyle::drawComplexControl(
    ComplexControl control,
    const QStyleOptionComplex* option,
    QPainter* painter,
    const QWidget* widget
) const
{
    if (control == CC_SpinBox) {
        if (const auto* spinOption = qstyleoption_cast<const QStyleOptionSpinBox*>(option)) {
            // Draw our styled background + border for the full frame.
            if (spinOption->frame && (spinOption->subControls & SC_SpinBoxFrame)) {
                const QRect frameRect
                    = proxy()->subControlRect(CC_SpinBox, option, SC_SpinBoxFrame, widget);
                drawComponent(painter, frameRect, widget, option);
            }

            // Draw spin button arrows on a transparent background (Breeze-style: no
            // separate button fill). We do not delegate to the base style at all — it
            // would re-draw its own frame and button backgrounds on top of ours.
            if (spinOption->buttonSymbols != QAbstractSpinBox::NoButtons) {
                const bool isPlusMinus = spinOption->buttonSymbols == QAbstractSpinBox::PlusMinus;

                const auto drawSpinButton = [&](SubControl subControl,
                                                PrimitiveElement arrowIndicator,
                                                PrimitiveElement plusMinusIndicator) {
                    if (!(spinOption->subControls & subControl)) {
                        return;
                    }
                    QStyleOptionSpinBox buttonOption = *spinOption;
                    buttonOption.rect = proxy()->subControlRect(CC_SpinBox, option, subControl, widget);
                    // Clear the sunken flag unless this specific button is active.
                    if (!(spinOption->activeSubControls & subControl)) {
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

            return;
        }
    }

    if (control == CC_ComboBox) {
        if (const auto* comboOption = qstyleoption_cast<const QStyleOptionComboBox*>(option)) {
            // Draw our styled background + border for the full frame.
            drawComponent(painter, option->rect, widget, option);

            // Draw the dropdown arrow indicator over the transparent button area.
            // QComboBox::paintEvent draws CE_ComboBoxLabel separately; it uses our
            // subControlRect(SC_ComboBoxEditField) for the text area.
            if (comboOption->subControls & SC_ComboBoxArrow) {
                QStyleOptionComboBox arrowOption = *comboOption;
                arrowOption.rect
                    = proxy()->subControlRect(CC_ComboBox, option, SC_ComboBoxArrow, widget);
                proxy()->drawPrimitive(PE_IndicatorArrowDown, &arrowOption, painter, widget);
            }

            return;
        }
    }

    if (control == CC_ToolButton) {
        if (const auto* tbOption = qstyleoption_cast<const QStyleOptionToolButton*>(option)) {
            const bool hasMenuButton = tbOption->features & QStyleOptionToolButton::MenuButtonPopup;
            const bool isVertical = toolbarOrientationOf(widget) == Qt::Vertical;

            // Resolves a BoxStyleDefinition and, for MenuButtonPopup buttons, zeroes the
            // border thickness and corner radii on the edge that joins the two halves.
            // This prevents a double border at the seam and keeps corners square where
            // the main button and the menu strip meet.
            // isTrailing = true  → main button (join is on its trailing/right or bottom edge)
            // isTrailing = false → menu strip  (join is on its leading/left or top edge)
            const auto seamed = [&](const StyleContext& context,
                                    bool isTrailing) -> BoxStyleDefinition {
                BoxStyleDefinition style = resolveBoxStyle(context);
                if (!hasMenuButton) {
                    return style;
                }

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
            };

            // Draw the main button area. Strip State_Sunken when only the menu strip is the
            // active subcontrol so that clicking the dropdown does not depress the main area.
            // When the menu strip is being pressed Qt may clear State_MouseOver from the overall
            // state. Use activeSubControls instead: it is non-zero whenever the mouse is over
            // any part of the split button, so the main half keeps its hover look.
            const QRect mainRect
                = proxy()->subControlRect(CC_ToolButton, option, SC_ToolButton, widget);
            QStyleOptionToolButton mainOption = *tbOption;
            if (!(tbOption->activeSubControls & SC_ToolButton)) {
                mainOption.state &= ~State_Sunken;
            }
            if (hasMenuButton && tbOption->activeSubControls) {
                mainOption.state |= State_MouseOver;
            }
            drawBoxBackground(painter, mainRect, seamed(contextOf(widget, &mainOption), true));

            if (hasMenuButton) {
                // Draw the dropdown arrow strip with its own interactive state.
                const QRect menuRect
                    = proxy()->subControlRect(CC_ToolButton, option, SC_ToolButtonMenu, widget);
                QStyleOptionToolButton menuOption = *tbOption;
                if (!(tbOption->activeSubControls & SC_ToolButtonMenu)) {
                    menuOption.state &= ~State_Sunken;
                }
                drawBoxBackground(painter, menuRect, seamed(contextOf(widget, &menuOption), false));

                // Arrow direction follows toolbar orientation.
                QStyleOptionToolButton arrowOption = *tbOption;
                arrowOption.rect = menuRect;
                proxy()->drawPrimitive(PE_IndicatorArrowDown, &arrowOption, painter, widget);
            }
            else if (tbOption->features & QStyleOptionToolButton::HasMenu) {
                // Instant/delayed popup: draw a small arrow indicator in the bottom-right corner.
                const int arrowSize = proxy()->pixelMetric(PM_MenuButtonIndicator, option, widget);
                QStyleOptionToolButton arrowOption = *tbOption;
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
            QStyleOptionToolButton labelOption = *tbOption;
            labelOption.rect = mainRect;
            labelOption.subControls &= ~SC_ToolButtonMenu;
            proxy()->drawControl(CE_ToolButtonLabel, &labelOption, painter, widget);

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
    const bool needsCustomLayout = hasIconOrArrow && !option->text.isEmpty()
        && (tbStyle == Qt::ToolButtonTextBesideIcon || tbStyle == Qt::ToolButtonTextUnderIcon);

    if (!needsCustomLayout) {
        // Icon-only with a real (non-arrow) icon: draw it ourselves so the
        // token-based icon color is applied. Text-only, arrow-only, and
        // ToolButtonTextOnly always delegate — we have nothing to colour there.
        const bool hasRealIcon = !option->icon.isNull() && option->arrowType == Qt::NoArrow
            && tbStyle != Qt::ToolButtonTextOnly;
        if (!hasRealIcon) {
            QProxyStyle::drawControl(CE_ToolButtonLabel, option, painter, widget);
            return;
        }

        QRect shiftedRect = contentRect;
        if (option->state & (State_Sunken | State_On)) {
            shiftedRect.translate(
                proxy()->pixelMetric(PM_ButtonShiftHorizontal, option, widget),
                proxy()->pixelMetric(PM_ButtonShiftVertical, option, widget)
            );
        }

        const QIcon::State iconState = (option->state & State_On) ? QIcon::On : QIcon::Off;
        QIcon::Mode iconMode = QIcon::Normal;
        if (!(option->state & State_Enabled)) {
            iconMode = QIcon::Disabled;
        }
        else if ((option->state & State_MouseOver) && (option->state & State_AutoRaise)) {
            iconMode = QIcon::Active;
        }

        QColor iconColor = option->palette.buttonText().color();
        if (const auto color = resolve<Base::Color>(context, StyleProperty::IconColor)) {
            iconColor = color->asValue<QColor>();
        }
        else if (const auto color = resolve<Base::Color>(context, StyleProperty::TextColor)) {
            iconColor = color->asValue<QColor>();
        }

        const QPixmap pixmap = IconManager::instance().render(
            option->icon,
            {
                .size = shiftedRect.size().boundedTo(option->iconSize),
                .dpr = painter->device()->devicePixelRatio(),
                .color = iconColor,
                .mode = iconMode,
                .state = iconState,
            }
        );

        if (!pixmap.isNull()) {
            proxy()->drawItemPixmap(painter, shiftedRect, Qt::AlignCenter, pixmap);
        }
        return;
    }

    const int iconSpacing = geometry.iconSpacing;

    // Apply pressed/checked shift — we manage layout so we do this ourselves.
    QRect shiftedContentRect = contentRect;
    if (option->state & (State_Sunken | State_On)) {
        shiftedContentRect.translate(
            proxy()->pixelMetric(PM_ButtonShiftHorizontal, option, widget),
            proxy()->pixelMetric(PM_ButtonShiftVertical, option, widget)
        );
    }

    const bool hasArrow = option->arrowType != Qt::NoArrow;

    QPixmap pixmap;
    QSize pixmapSize = option->iconSize;
    if (!hasArrow && !option->icon.isNull()) {
        const QIcon::State iconState = (option->state & State_On) ? QIcon::On : QIcon::Off;
        QIcon::Mode iconMode = QIcon::Normal;
        if (!(option->state & State_Enabled)) {
            iconMode = QIcon::Disabled;
        }
        else if ((option->state & State_MouseOver) && (option->state & State_AutoRaise)) {
            iconMode = QIcon::Active;
        }
        QColor iconColor = option->palette.buttonText().color();
        if (const auto color = resolve<Base::Color>(context, StyleProperty::IconColor)) {
            iconColor = color->asValue<QColor>();
        }
        else if (const auto color = resolve<Base::Color>(context, StyleProperty::TextColor)) {
            iconColor = color->asValue<QColor>();
        }
        pixmap = IconManager::instance().render(
            option->icon,
            {
                .size = shiftedContentRect.size().boundedTo(option->iconSize),
                .dpr = painter->device()->devicePixelRatio(),
                .color = iconColor,
                .mode = iconMode,
                .state = iconState,
            }
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

    int textFlags = Qt::TextShowMnemonic;
    if (!proxy()->styleHint(SH_UnderlineShortcut, option, widget)) {
        textFlags |= Qt::TextHideMnemonic;
    }

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
    QRect shiftedContentRect = contentRect;
    if (option->state & (State_Sunken | State_On)) {
        shiftedContentRect.translate(
            proxy()->pixelMetric(PM_ButtonShiftHorizontal, option, widget),
            proxy()->pixelMetric(PM_ButtonShiftVertical, option, widget)
        );
    }

    const int iconSpacing = geometry.iconSpacing;

    const QIcon::State iconState = (option->state & State_On) ? QIcon::On : QIcon::Off;
    const QIcon::Mode iconMode = (option->state & State_Enabled) ? QIcon::Normal : QIcon::Disabled;
    QColor iconColor = option->palette.buttonText().color();
    if (const auto color = resolve<Base::Color>(context, StyleProperty::IconColor)) {
        iconColor = color->asValue<QColor>();
    }
    else if (const auto color = resolve<Base::Color>(context, StyleProperty::TextColor)) {
        iconColor = color->asValue<QColor>();
    }
    const QPixmap pixmap = IconManager::instance().render(
        option->icon,
        {
            .size = shiftedContentRect.size().boundedTo(option->iconSize),
            .dpr = painter->device()->devicePixelRatio(),
            .color = iconColor,
            .mode = iconMode,
            .state = iconState,
        }
    );
    const QSize pixmapSize = pixmap.size() / painter->device()->devicePixelRatio();

    // Center the icon+text group horizontally in the content rect.
    const int textWidth = option->fontMetrics.horizontalAdvance(option->text);
    const int groupWidth = pixmapSize.width() + iconSpacing + textWidth;
    const int groupLeft = shiftedContentRect.left() + (shiftedContentRect.width() - groupWidth) / 2;

    const QRect iconRect(
        groupLeft,
        shiftedContentRect.top() + (shiftedContentRect.height() - pixmapSize.height()) / 2,
        pixmapSize.width(),
        pixmapSize.height()
    );
    const QRect textRect(
        groupLeft + pixmapSize.width() + iconSpacing,
        shiftedContentRect.top(),
        shiftedContentRect.right() - (groupLeft + pixmapSize.width() + iconSpacing),
        shiftedContentRect.height()
    );

    int textFlags = Qt::TextShowMnemonic | Qt::AlignVCenter | Qt::AlignLeft;
    if (!proxy()->styleHint(SH_UnderlineShortcut, option, widget)) {
        textFlags |= Qt::TextHideMnemonic;
    }

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

    int textFlags = Qt::TextShowMnemonic | Qt::AlignVCenter | Qt::AlignLeft;
    if (!proxy()->styleHint(SH_UnderlineShortcut, option, widget)) {
        textFlags |= Qt::TextHideMnemonic;
    }

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

    // Position context: visual tokens resolved with actual position (supports per-position overrides).
    const StyleContext positionContext = contextOf(widget, option, StyleComponentElement::Tab);

    // North context: geometric tokens always resolved canonical, then rotated.
    StyleContext northContext = positionContext;
    northContext.variant.set(VariantSlot::Position, Position::North);

    BoxStyleDefinition style = resolveBoxStyle(positionContext);

    if (const auto corners
        = resolve<StyleParameters::Corners>(northContext, StyleProperty::BorderRadius)) {
        style.borderRadius = rotated(Base::convertTo<CornerRadii>(*corners), position);
    }
    if (const auto thickness
        = resolve<StyleParameters::Insets>(northContext, StyleProperty::BorderThickness)) {
        style.borderThickness = rotated(Base::convertTo<QMarginsF>(*thickness), position);
    }

    // Apply trailing-edge spacing: PM_TabBarTabOverlap is a purely visual hint that tells the
    // style how much to extend (positive) or shrink (negative) each non-last tab's paint rect
    // on its trailing edge. QTabBar's layoutTabs() does NOT use this metric; all spacing is
    // achieved here by adjusting the rect before drawing.
    //
    // With TabBarTabSpacing = -1px (overlap = 1): paint rect extends 1px into the next tab's
    // space, so the selected tab's background covers the shared border → seamless appearance.
    // With TabBarTabSpacing = 4px (overlap = -4): paint rect shrinks by 4px, leaving a visible
    // gap between the tab background and the start of the next tab.
    const bool isLastOrOnly = option->position == QStyleOptionTab::End
        || option->position == QStyleOptionTab::OnlyOneTab;
    const int tabOverlap = isLastOrOnly ? 0
                                        : proxy()->pixelMetric(PM_TabBarTabOverlap, option, widget);
    const bool isVertical = (position == Position::East || position == Position::West);

    QRect drawRect = option->rect;
    if (tabOverlap != 0) {
        if (isVertical) {
            drawRect = drawRect.adjusted(0, 0, 0, tabOverlap);
        }
        else {
            drawRect = drawRect.adjusted(0, 0, tabOverlap, 0);
        }
    }

    drawBoxBackground(painter, drawRect, style);
}

void FreeCADStyle::drawTabBarTabLabel(
    QPainter* painter,
    const QStyleOptionTab* option,
    const QWidget* widget
) const
{
    const Position position = tabPositionOf(option->shape);
    const bool isVertical = (position == Position::East || position == Position::West);

    // Vertical tabs require a rotated painter set up by Qt — delegate to parent.
    if (isVertical) {
        QProxyStyle::drawControl(CE_TabBarTabLabel, option, painter, widget);
        return;
    }

    const bool hasIcon = !option->icon.isNull();
    const bool hasText = !option->text.isEmpty();

    // Only customise when both icon and text are present; delegate otherwise.
    if (!hasIcon || !hasText) {
        QProxyStyle::drawControl(CE_TabBarTabLabel, option, painter, widget);
        return;
    }

    // Geometry is always resolved in the canonical North context (PM_TabBarTabHSpace/VSpace does
    // the same: the tab size is computed in North space and transposed by QTabBar for East/West).
    StyleContext geometryContext = contextOf(widget, option, StyleComponentElement::Tab);
    geometryContext.variant.set(VariantSlot::Position, Position::North);
    const BoxGeometryDefinition geometry = resolveBoxGeometry(geometryContext);

    const QRect contentRect = geometry.contentRect(option->rect);

    const QIcon::State iconState = (option->state & State_On) ? QIcon::On : QIcon::Off;
    const QIcon::Mode iconMode = (option->state & State_Enabled) ? QIcon::Normal : QIcon::Disabled;
    const QPixmap pixmap = option->icon.pixmap(
        option->iconSize,
        painter->device()->devicePixelRatio(),
        iconMode,
        iconState
    );
    const QSize pixmapSize = pixmap.size() / painter->device()->devicePixelRatio();

    const int iconSpacing = geometry.iconSpacing;

    const QRect iconRect(
        contentRect.left(),
        contentRect.top() + (contentRect.height() - pixmapSize.height()) / 2,
        pixmapSize.width(),
        pixmapSize.height()
    );
    const QRect textRect = contentRect.adjusted(pixmapSize.width() + iconSpacing, 0, 0, 0);

    int textFlags = Qt::TextShowMnemonic;
    if (!proxy()->styleHint(SH_UnderlineShortcut, option, widget)) {
        textFlags |= Qt::TextHideMnemonic;
    }

    painter->save();

    proxy()->drawItemPixmap(painter, iconRect, Qt::AlignCenter, pixmap);
    proxy()->drawItemText(
        painter,
        QStyle::visualRect(option->direction, contentRect, textRect),
        textFlags | Qt::AlignLeft | Qt::AlignVCenter,
        option->palette,
        option->state & State_Enabled,
        option->text,
        QPalette::ButtonText
    );

    painter->restore();
}

FreeCADStyle::BoxStyleDefinition FreeCADStyle::resolveBaseStripStyle(
    const StyleContext& positionContext
) const
{
    const auto position = static_cast<Position>(positionContext.variant.get(VariantSlot::Position));

    StyleContext northContext = positionContext;
    northContext.variant.set(VariantSlot::Position, Position::North);

    // Visual tokens (BorderColor, Overlay, etc.) from position context; background and geometric
    // tokens from North canonical and rotated — treated uniformly since the gradient is directional.
    BoxStyleDefinition style = resolveBoxStyle(positionContext);

    if (const auto background = resolve(northContext, StyleProperty::Background)) {
        style.background = rotated(Base::convertTo<QBrush>(*background), position);
    }
    if (const auto thickness
        = resolve<StyleParameters::Insets>(northContext, StyleProperty::BorderThickness)) {
        style.borderThickness = rotated(Base::convertTo<QMarginsF>(*thickness), position);
    }

    return style;
}

void FreeCADStyle::drawTabBarBase(
    QPainter* painter,
    const QStyleOptionTabBarBase* option,
    const QWidget* widget
) const
{
    const StyleContext positionContext = contextOf(widget, option, StyleComponentElement::Base);
    drawBoxBackground(painter, option->rect, resolveBaseStripStyle(positionContext));
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

    const int stripHeight = [&]() -> int {
        if (const auto height = resolve<StyleParameters::Numeric>(stripContext, StyleProperty::Height)) {
            return static_cast<int>(height->value);
        }
        return 0;
    }();

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

    BoxStyleDefinition stripStyle = resolveBaseStripStyle(stripContext);
    // The pane box already draws the border; suppress the strip's own border to avoid doubling.
    stripStyle.borderColor = std::nullopt;
    stripStyle.borderThickness = std::nullopt;

    drawBoxBackground(painter, stripRect, stripStyle);
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

    if (const auto spacing = resolve<StyleParameters::Numeric>(context, StyleProperty::IconSpacing)) {
        result.iconSpacing = static_cast<int>(spacing->value);
    }

    return result;
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
    if (const auto numeric = resolve<StyleParameters::Numeric>("SeparatorThickness")) {
        thickness = static_cast<int>(numeric->value);
    }
    if (const auto color = resolve<Base::Color>("SeparatorColor")) {
        const QRect lineRect = isHorizontal
            ? QRect(rect.left(), rect.center().y() - (thickness / 2), rect.width(), thickness)
            : QRect(rect.center().x() - (thickness / 2), rect.top(), thickness, rect.height());
        painter->fillRect(lineRect, color->asValue<QColor>());
    }
}

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
}

void FreeCADStyle::unpolish(QWidget* widget)
{
    if (qobject_cast<QTabBar*>(widget)) {
        widget->removeEventFilter(this);
    }
    QProxyStyle::unpolish(widget);
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
        if (auto* groupBox = qobject_cast<QGroupBox*>(obj)) {
            if (auto* layout = groupBox->layout()) {
                layout->setContentsMargins(0, 0, 0, 0);
            }
        }

        // Apply token padding to QTextEdit / QPlainTextEdit via the document margin.
        // This pads the text content relative to the viewport while leaving scrollbars
        // flush with the frame edge (unlike viewport-margin approaches).
        const auto applyTextEditDocumentMargin = [this](QWidget* widget, QTextDocument* document) {
            const StyleContext context = contextOf(widget);
            const BoxGeometryDefinition geometry = resolveBoxGeometry(context);
            document->setDocumentMargin(geometry.padding.left());
        };

        if (auto* textEdit = qobject_cast<QTextEdit*>(obj)) {
            applyTextEditDocumentMargin(textEdit, textEdit->document());
        }
        else if (auto* plainTextEdit = qobject_cast<QPlainTextEdit*>(obj)) {
            applyTextEditDocumentMargin(plainTextEdit, plainTextEdit->document());
        }
    }

    // Force tab bar repaint on mouse move/leave so our cursor-position hover check in
    // contextOf() sees up-to-date state. Qt's internal WA_Hover tracking for QTabBar does
    // not consistently trigger repaints in all configurations.
    if (qobject_cast<QTabBar*>(obj)) {
        if (event->type() == QEvent::MouseMove || event->type() == QEvent::Leave
            || event->type() == QEvent::HoverMove || event->type() == QEvent::HoverLeave) {
            static_cast<QWidget*>(obj)->update();
        }
    }

    return QObject::eventFilter(obj, event);
}
