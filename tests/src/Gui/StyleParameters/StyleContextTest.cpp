// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include <Gui/StyleParameters/StyleContext.h>

using namespace Gui::StyleParameters;

namespace
{

// A context with every dimension set to something non-default, so a field that overlaps its
// neighbour shows up as a collision rather than as a coincidence.
StyleContext populatedContext()
{
    StyleContext context;
    context.component = StyleComponent::List;
    context.element = StyleComponentElement::Row;
    context.state |= StyleState::Selected;
    context.variant.set(VariantSlot::ControlSize, 1);
    return context;
}

// The layout cacheKey() must produce, written out as a second, independent copy of what
// StyleContext's private offsets say. Changing the production constants without changing these
// is meant to fail: the duplication is the mechanism, not an oversight.
constexpr uint64_t componentShift = 0;
constexpr uint64_t elementShift = 8;
constexpr uint64_t stateShift = 12;
constexpr uint64_t propertyShift = 20;
constexpr uint64_t overrideShift = 28;
constexpr uint64_t variantShift = 36;
constexpr uint64_t variantSlotWidth = 4;

}  // namespace

TEST(StyleContextTest, PacksEachDimensionAtItsDocumentedOffset)
{
    // One dimension at a time on an otherwise-default context, whose key is zero. The whole key is
    // then the field under test, so a field packed at a neighbour's offset fails outright — unlike
    // an inequality check, which two overlapping fields still satisfy for most pairs of values.
    EXPECT_EQ(StyleContext {}.cacheKey(), uint64_t {0})
        << "a context with every dimension at its default must pack to an empty key";

    StyleContext component;
    component.component = StyleComponent::Tree;
    EXPECT_EQ(component.cacheKey(), static_cast<uint64_t>(StyleComponent::Tree) << componentShift);

    StyleContext element;
    element.element = StyleComponentElement::Shortcut;
    EXPECT_EQ(element.cacheKey(), static_cast<uint64_t>(StyleComponentElement::Shortcut) << elementShift);

    StyleContext state;
    state.state |= StyleState::Selected;
    state.state |= StyleState::Disabled;
    EXPECT_EQ(state.cacheKey(), static_cast<uint64_t>(state.state.toUnderlyingType()) << stateShift);

    EXPECT_EQ(
        StyleContext {}.cacheKey(StyleProperty::PlacementOffset),
        static_cast<uint64_t>(StyleProperty::PlacementOffset) << propertyShift
    );

    // intern() is idempotent, so asking for the id is a reading of the intern table rather than a
    // restatement of whatever cacheKey() happened to pack.
    StyleContext override;
    override.componentOverride = "CacheKeyLayoutProbe";
    const uint64_t overrideId = StyleContext::Intern::global().intern("CacheKeyLayoutProbe");
    EXPECT_EQ(override.cacheKey(), overrideId << overrideShift);

    // Every slot, not just the ends: checking only the first and last would take the uniform
    // stride on trust, when that uniformity is exactly what the packing could get wrong. Value 1
    // is valid for every dimension, all of which have at least a Default and one other value.
    for (size_t slot = 0; slot < size_t(VariantSlot::COUNT); ++slot) {
        StyleContext variant;
        variant.variant.slots.at(slot) = 1;
        EXPECT_EQ(variant.cacheKey(), uint64_t {1} << (variantShift + variantSlotWidth * slot))
            << "variant slot " << slot;
    }
}

TEST(StyleContextTest, TheKeyFieldsAreDisjoint)
{
    // Summing the single-dimension keys agrees with the combined key only when no two fields share
    // a bit — OR-ing loses what addition carries. This catches an overlap without naming a single
    // offset, so it still holds if the layout is deliberately rearranged.
    const std::string probe = "CacheKeyDisjointProbe";

    StyleContext component;
    component.component = StyleComponent::Tree;

    StyleContext element;
    element.element = StyleComponentElement::Shortcut;

    StyleContext state;
    state.state |= StyleState::Disabled;

    StyleContext override;
    override.componentOverride = probe;

    StyleContext variant;
    variant.variant.set(VariantSlot::TransparencyMode, TransparencyMode::Transparent);

    StyleContext combined;
    combined.component = component.component;
    combined.element = element.element;
    combined.state = state.state;
    combined.componentOverride = probe;
    combined.variant = variant.variant;

    const uint64_t sum = component.cacheKey() + element.cacheKey() + state.cacheKey()
        + override.cacheKey() + variant.cacheKey()
        + StyleContext {}.cacheKey(StyleProperty::PlacementOffset);

    EXPECT_EQ(combined.cacheKey(StyleProperty::PlacementOffset), sum);
}

TEST(StyleContextTest, StatesThatDifferProduceDifferentKeys)
{
    StyleContext selected = populatedContext();

    StyleContext checked = populatedContext();
    checked.state = {};
    checked.state |= StyleState::Checked;

    EXPECT_NE(selected.cacheKey(), checked.cacheKey());
}

TEST(StyleContextTest, EveryDimensionMovesTheKeyIndependently)
{
    const StyleContext base = populatedContext();

    StyleContext otherComponent = base;
    otherComponent.component = StyleComponent::Tree;

    StyleContext otherElement = base;
    otherElement.element = StyleComponentElement::Item;

    StyleContext otherState = base;
    otherState.state |= StyleState::Hovered;

    StyleContext otherVariant = base;
    otherVariant.variant.set(VariantSlot::ControlSize, 2);

    StyleContext otherOverride = base;
    otherOverride.componentOverride = "SomeWidget";

    const std::vector<uint64_t> keys {
        base.cacheKey(),
        otherComponent.cacheKey(),
        otherElement.cacheKey(),
        otherState.cacheKey(),
        otherVariant.cacheKey(),
        otherOverride.cacheKey(),
    };

    for (size_t outer = 0; outer < keys.size(); ++outer) {
        for (size_t inner = outer + 1; inner < keys.size(); ++inner) {
            EXPECT_NE(keys.at(outer), keys.at(inner))
                << "cache key fields " << outer << " and " << inner << " overlap";
        }
    }
}

TEST(StyleContextTest, ThePropertyDimensionDoesNotCollideWithTheStateField)
{
    // The repack that made room for StyleState::Selected moved propertyBitOffset. If the state
    // field ran into it, a high-numbered property would alias a state flag.
    const StyleContext context = populatedContext();

    std::vector<uint64_t> keys;
    for (int property = 0; property < static_cast<int>(StyleProperty::COUNT); ++property) {
        keys.push_back(context.cacheKey(static_cast<StyleProperty>(property)));
    }

    const std::set<uint64_t> distinct(keys.begin(), keys.end());
    EXPECT_EQ(distinct.size(), keys.size());
}
