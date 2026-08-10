// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <set>
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

}  // namespace

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
