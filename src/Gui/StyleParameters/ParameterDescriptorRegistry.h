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

#pragma once

#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <FCGlobal.h>
#include "StyleContext.h"

namespace Gui::StyleParameters
{

/**
 * @brief Distinguishes how a variant participates in prefix building.
 *
 * Variant-kind dimensions (ButtonType, Size, Position) are concatenated into a
 * single variant string that appears before the state suffix. State-kind
 * dimensions contribute individual state-suffix strings and allow multiple
 * simultaneous values.
 */
enum class ParameterVariantKind : uint8_t
{
    Variant,
    State,
};

/**
 * @brief Describes one dimension in a component's parameter name structure.
 *
 * Variants are registered globally and shared across descriptors. The `values`
 * list contains only the non-default (non-empty) string tokens for this variant.
 * When a variant value is absent from a name, that dimension defaults to its
 * baseline (empty string).
 */
struct GuiExport ParameterVariant
{
    /// Unique name for this dimension, e.g. "ButtonType", "Size", "State".
    std::string name;
    /// Whether this variant is a variant or a state dimension.
    ParameterVariantKind kind = ParameterVariantKind::Variant;
    /// Non-default values (in the order they appear in token names), e.g. {"Primary","Link"}.
    std::vector<std::string> values;
};

/**
 * @brief Describes the parameter namespace structure and inheritance for one component.
 *
 * The `variants` list names the dimensions that appear (in order) in token names
 * for this component. The `inherits` list defines the fallback chain — when a
 * token is not found under this component's prefix, the chain is walked in the
 * given order.
 */
struct GuiExport ParameterDescriptor
{
    /// Component token namespace name, e.g. "Button", "FormControl".
    std::string name;
    /// Variant names in the order they appear in the parameter name.
    std::vector<std::string> variants;
    /// Parent component names to fall back to, most-specific first, e.g. {"FormControl"}.
    std::vector<std::string> inherits;
};

/**
 * @brief Result of parsing a parameter name string into its structural parts.
 *
 * `variantValues` contains only the variants that were matched — absent variants
 * are not present in the map (they represent the default / baseline value).
 */
struct GuiExport ParsedParameterName
{
    /// The registered component name, e.g. "Button".
    std::string component;
    /// Matched variant values keyed by variant name, e.g. {{"Size","Small"},{"State","Focused"}}.
    std::map<std::string, std::string> variants;
    /// The property suffix, e.g. "BorderColor".
    std::string property;
};

/**
 * @brief Registry of parameter descriptors and variant definitions.
 *
 * Stores structural descriptions of each component's parameter namespace and
 * how components inherit from one another. Provides:
 *  - Name-based parsing: decompose "ButtonSmallFocusedBorderColor" into
 *    {component="Button", variantValues={Size→"Small", State→"Focused"}, property="BorderColor"}.
 *  - Prefix building from a parsed name (for name-based synthesis).
 *  - Component chain lookup by StyleComponent enum (for context-based resolution).
 */
class GuiExport ParameterDescriptorRegistry
{
public:
    /**
     * @brief Registers a variant definition (a named dimension with its valid values).
     *
     * Replaces any previously registered variant with the same name.
     */
    void registerVariant(ParameterVariant variant);

    /**
     * @brief Registers a component descriptor.
     *
     * Optionally associates a StyleComponent enum value with this descriptor so
     * that chain() can be used for context-based resolution.
     * Replaces any previously registered descriptor with the same name.
     *
     * @param descriptor    The descriptor to register.
     * @param component     Optional enum mapping; pass StyleComponent::COUNT to omit.
     */
    void registerDescriptor(
        ParameterDescriptor descriptor,
        StyleComponent component = StyleComponent::COUNT
    );

    /**
     * @brief Parses a parameter name into its structural parts.
     *
     * Uses longest-match for the component prefix, then greedily matches variant
     * values in descriptor-defined order. Returns nullopt if the name does not
     * start with any registered component prefix, or if no property suffix
     * remains after variant matching.
     *
     * @param name  Full parameter name, e.g. "ButtonSmallFocusedBorderColor".
     * @return Parsed result, or nullopt if the name is unrecognised.
     */
    std::optional<ParsedParameterName> parse(const std::string& name) const;

    /**
     * @brief Builds the ordered fallback prefix list for a StyleContext.
     *
     * Converts the enum-based context (component, element, variant, state) to
     * the same ordered prefix list that buildPrefixesFromParsed() produces for
     * an equivalent parsed name. Uses the registered component chain and the
     * canonical variant/state string tables.
     *
     * @param context  The styling context to convert.
     * @return Ordered prefix strings (without the property suffix).
     */
    std::vector<std::string> buildPrefixes(const StyleContext& context) const;

    /**
     * @brief Builds the ordered fallback prefix list from a parsed parameter name.
     *
     * Generates all variant+state combinations across the component's inheritance
     * chain, in the same priority order as buildPrefixes().
     *
     * @param parsed  Result of a previous parse() call.
     * @return Ordered prefix strings (without the property suffix).
     */
    std::vector<std::string> buildPrefixesFromParsed(const ParsedParameterName& parsed) const;

    /**
     * @brief Returns the full chain of token-prefix names for a StyleComponent enum value.
     *
     * The chain contains the component's own name followed by all inherited
     * component names in priority order, mirroring the old componentChains map.
     * Returns an empty span if the component has no registered descriptor.
     *
     * @param component  The StyleComponent enum value to look up.
     */
    std::span<const std::string> chain(StyleComponent component) const;

    /**
     * @brief Returns the descriptor for a given component name, or nullptr.
     */
    const ParameterDescriptor* descriptor(const std::string& name) const;

    /**
     * @brief Returns all registered variant definitions.
     */
    const std::map<std::string, ParameterVariant>& variants() const;

private:
    std::map<std::string, ParameterVariant> _variants;
    std::map<std::string, ParameterDescriptor> _descriptors;

    // Each StyleComponent enum value maps to the full chain of prefix names
    // (component's own name + all inherited names), built lazily when the
    // descriptor is registered with an explicit component enum value.
    std::map<StyleComponent, std::vector<std::string>> _componentChains;

    // Sorted list of component names (longest first) for greedy prefix matching.
    mutable std::vector<std::string> _sortedNames;
    mutable bool _sortedNamesDirty = true;

    const std::vector<std::string>& sortedNames() const;

    // Builds the prefix entries for one component-with-element base string,
    // given the variant values and the set of active state strings.
    void appendPrefixEntries(
        std::vector<std::string>& prefixes,
        const std::string& componentBase,
        const std::string& variantStr,
        const std::vector<std::string>& activeStates
    ) const;

    // Returns the component name followed by its direct inherits list, in order.
    // Callers are responsible for providing a fully-flattened inherits list in
    // the descriptor — this function does not recurse into grandparent chains.
    std::vector<std::string> resolveChainNames(const std::string& name) const;
};

/**
 * @brief Registers all built-in variant dimensions (ButtonType, ControlSize, Position, State)
 *        from the canonical tables in StyleContext.cpp into the given registry.
 *
 * Call this once during registry initialization instead of manually duplicating
 * the variant value strings.
 */
GuiExport void registerBuiltinVariants(ParameterDescriptorRegistry& registry);

/**
 * @brief Registers all built-in component descriptors and variant dimensions into the registry.
 *
 * Populates the full FreeCAD design system component hierarchy: FormControl, Button, ToolButton,
 * LineEdit, etc., with their variant dimensions and inheritance chains. Calls
 * registerBuiltinVariants() internally.
 *
 * Call once during registry initialisation (e.g. from ParameterManager setup).
 */
GuiExport void populateBuiltinDescriptors(ParameterDescriptorRegistry& registry);

}  // namespace Gui::StyleParameters
