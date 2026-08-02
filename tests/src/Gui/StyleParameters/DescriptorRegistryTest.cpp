// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <Gui/StyleParameters/ParameterDescriptorRegistry.h>

using namespace Gui::StyleParameters;

namespace
{

ParameterDescriptorRegistry builtinRegistry()
{
    ParameterDescriptorRegistry registry;
    populateBuiltinDescriptors(registry);
    return registry;
}

}  // namespace

TEST(DescriptorRegistryTest, ParsesTransparencyVariantOnAnyComponent)
{
    const ParameterDescriptorRegistry registry = builtinRegistry();

    const auto parsed = registry.parse("ToolBarTransparentBackground");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->component, "ToolBar");
    EXPECT_EQ(parsed->property, "Background");
    ASSERT_TRUE(parsed->variants.contains("TransparencyMode"));
    EXPECT_EQ(parsed->variants.at("TransparencyMode"), "Transparent");
}

TEST(DescriptorRegistryTest, ParsedAndContextPrefixesAgree)
{
    const ParameterDescriptorRegistry registry = builtinRegistry();

    StyleContext context;
    context.component = StyleComponent::ToolBar;
    context.variant.set(VariantSlot::TransparencyMode, TransparencyMode::Transparent);

    const auto parsed = registry.parse("ToolBarTransparentBackground");
    ASSERT_TRUE(parsed.has_value());

    EXPECT_EQ(registry.buildPrefixes(context), registry.buildPrefixesFromParsed(*parsed));
}
