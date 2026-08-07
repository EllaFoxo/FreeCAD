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

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include <Base/Bitmask.h>
#include <FCGlobal.h>

class QWidget;

namespace Gui::StyleParameters
{

/**
 * @brief Widget components that participate in style token resolution.
 *
 * Add new components before COUNT. The string representation of each component
 * (used in token names) is defined in FreeCADStyle.cpp::componentString().
 */
enum class StyleComponent : uint8_t
{
    None,
    PushButton,
    ToolButton,
    LineEdit,          // QLineEdit + QAbstractSpinBox edit frame
    TextEdit,          // QPlainTextEdit, QTextEdit and derivatives
    Select,            // QComboBox (non-editable), inherits Button styles
    ComboBox,          // QComboBox (editable), inherits LineEdit styles
    List,              // QListWidget, QListView
    DropdownList,      // QListView inside a QComboBox popup
    Tree,              // QTreeWidget, QTreeView
    CheckBox,          // QCheckBox indicator
    RadioButton,       // QRadioButton indicator
    TabBar,            // QTabBar
    TabWidget,         // QTabWidget pane (PE_FrameTabWidget)
    ToolBar,           // QToolBar
    ToolBarButton,     // QToolButton in QToolBar - semantically different
    MenuBar,           // QMenuBar
    Header,            // QHeaderView sections
    InternalButton,    // Flat action button painted inside custom composite widgets
    GeometrySelector,  // Gui::GeometrySelectorWidget composite field, inherits List
    GroupBox,          // QGroupBox frame and title
    Menu,              // QMenu popup: context menus, menu bar and tool button dropdowns
    // Add new components before COUNT
    COUNT
};

/**
 * @brief Sub-elements of widget component that participate in style token resolution.
 *
 * Add new components before COUNT. These do not directly correspond to any component, rather allow
 * to specify part of the component that we are interested in, like Item of List.
 *
 * The cache key gives this enum 4 bits (16 values). Adding a 17th requires repacking
 * StyleContext::cacheKey(); the key is 64-bit and has room, but it is not automatic.
 */
enum class StyleComponentElement : uint8_t
{
    Root,         // Main component
    Item,         // Item of the component (useful for lists, trees etc.)
    Row,          // Row of the component (useful for lists, trees etc.)
    Indicator,    // Checkbox for items
    Tab,          // Individual tab of a TabBar
    Base,         // Base strip of a TabBar (PE_FrameTabBarBase)
    Menu,         // Dropdown menu strip of a MenuButtonPopup ToolButton
    CloseButton,  // Tab close button (QAbstractButton child of QTabBar)
    Branch,       // Connector lines in the indent column of a tree view
    Title,        // Title of a framed container, drawn on its top edge
    Separator,    // Separator rule, and the label of an addSection() header, inside a menu
    Arrow,        // Submenu arrow of a menu item
    Shortcut,     // Accelerator column of a menu item
    // Add new components before COUNT
    COUNT,
};

/**
 * @brief Visual type variant for button-like components.
 *
 * Derived from Qt widget properties:
 *   - Primary : QPushButton::isDefault() or QStyleOptionButton::DefaultButton feature
 *   - Link    : QPushButton::isFlat(), QToolButton::autoRaise(), or property("flat") == true
 *   - Default : everything else
 *
 * Add new types before COUNT.
 */
enum class ButtonType : uint8_t
{
    Default,
    Primary,
    Link,
    // Add new button types before COUNT
    COUNT
};

/**
 * @brief Size variant, derived from the "controlSize" widget property.
 *
 * Add new sizes before COUNT.
 */
enum class ControlSize : uint8_t
{
    Default,
    Internal,  // compact 18px controls embedded inside composite widgets
    Small,
    Big,
    // Add new sizes before COUNT
    COUNT
};

/**
 * @brief Edge position — the edge at which a component attaches.
 *
 * North (0) is canonical. Geometric tokens are resolved with North and rotated to the
 * actual position; visual tokens are resolved with the actual position so per-position
 * colour overrides (e.g. TabBarTabSouthBackground) work naturally.
 *
 * Add new positions before COUNT.
 */
enum class Position : uint8_t
{
    North = 0,  // canonical; maps to "" in token names (default variant)
    East,       // "East"
    South,      // "South"
    West,       // "West"
    // Add new positions before COUNT
    COUNT
};

/**
 * @brief Interaction state bitmask — multiple flags may be active simultaneously.
 *
 * Token resolution expands active flags into a fallback prefix list in priority
 * order (highest priority first): Disabled > Pressed > Hovered > Checked > Focused.
 */
enum class StyleState : uint8_t
{
    Normal = 0,
    Focused = 1 << 0,
    Checked = 1 << 1,
    Hovered = 1 << 2,
    Pressed = 1 << 3,
    Disabled = 1 << 4,
};

/**
 * @brief Row parity variant for item-view components.
 *
 * Set to Alternate when QStyleOptionViewItem::Alternate is active so that
 * alternate-row tokens (e.g. ListAlternateRowBackground) are tried before
 * falling back to the default-row tokens.
 *
 * Add new types before COUNT.
 */
enum class RowType : uint8_t
{
    Default,
    Alternate,
    // Add new row types before COUNT
    COUNT
};

/**
 * @brief Frame treatment variant for framed containers.
 *
 * Flat mirrors QStyleOptionFrame::Flat, which QGroupBox sets from setFlat(true).
 *
 * Add new frame types before COUNT.
 */
enum class FrameType : uint8_t
{
    Default,
    Flat,
    // Add new frame types before COUNT
    COUNT
};

/**
 * @brief Selects the treatment a component uses when it must not paint an opaque surface.
 *
 * Transparent is reached two ways, which mean different things and must not be
 * conflated. A toolbar hosted in the status bar or as a QMenuBar corner widget
 * blends into an otherwise opaque host, so it suppresses its own chrome but the
 * surface beneath its children is still solid. A widget carrying the propagated
 * transparency tag genuinely sits over a see-through surface, and that is the only
 * form that its children inherit.
 *
 * Add new modes before COUNT.
 */
enum class TransparencyMode : uint8_t
{
    Normal,
    Transparent,
    // Add new modes before COUNT
    COUNT
};

/**
 * @brief Registry of variant dimensions used in token names.
 *
 * Each slot corresponds to one enum dimension (ButtonType, ControlSize, …).
 * Adding a new variant dimension requires:
 *   1. Adding a slot entry before COUNT here.
 *   2. Defining the values enum with Default = 0 and COUNT as the last value.
 *   3. Adding a string table and entry in ParameterDescriptorRegistry.cpp::variantSlotNames.
 *   4. Setting the slot in FreeCADStyle.cpp::contextOf().
 *   5. Placing the entry last if the slot applies to every component (see globalSlots in
 *      ParameterDescriptorRegistry.cpp); a static_assert there enforces it.
 */
enum class VariantSlot : uint8_t
{
    ButtonType,
    ControlSize,
    Position,
    RowType,           // Alternate row parity for item-view components
    FrameType,         // Flat frame treatment for framed containers
    TransparencyMode,  // Transparent background for status bar / menu bar corner toolbars
    // Add new variant dimensions before COUNT
    COUNT
};

/**
 * @brief Holds one uint8_t value per VariantSlot, defaulting to 0 (the Default
 *        value of each dimension's enum).
 *
 * The array size is determined at compile time by VariantSlot::COUNT, so adding
 * a new slot automatically expands the array — no manual size management needed.
 */
struct VariantKey
{
    std::array<uint8_t, size_t(VariantSlot::COUNT)> slots = {};

    template<typename EnumT>
    void set(VariantSlot slot, EnumT value)
    {
        slots.at(size_t(slot)) = uint8_t(value);
    }

    uint8_t get(VariantSlot slot) const
    {
        return slots.at(size_t(slot));
    }

    bool operator==(const VariantKey&) const = default;
};

/**
 * @brief Style properties that can be resolved from tokens.
 *
 * Each value corresponds to a token suffix (e.g. Padding → "Padding").
 * Add new properties before COUNT.
 */
enum class StyleProperty : uint8_t
{
    Width,
    MinWidth,
    MaxWidth,
    Height,
    MinHeight,
    MaxHeight,
    BorderThickness,
    BorderRadius,
    BorderColor,
    Padding,
    Margin,
    Spacing,
    Overlap,
    IconSize,
    IconSpacing,
    FontSize,
    FontWeight,
    Background,
    TextColor,
    InnerShadow,
    IconColor,
    BackgroundEffect,
    BorderColorEffect,
    FrameWidth,
    IsTransparent,
    // Add new properties before COUNT
    COUNT,
};

/**
 * @brief Fully describes the styling context for a widget in a given state.
 *
 * Built once per draw call via FreeCADStyle::contextOf(), then passed to
 * resolve() and resolveBoxBackground() for cached token lookup.
 */
struct GuiExport StyleContext
{
    StyleComponent component = StyleComponent::None;
    StyleComponentElement element = StyleComponentElement::Root;
    VariantKey variant = {};
    Base::Flags<StyleState> state;
    /**
     * @brief Optional component name override, read from the widget's "component" dynamic property.
     *
     * When non-empty, this string is prepended to the normal component chain as an additional
     * prefix, allowing widgets to opt into custom token namespaces (e.g. "ActionButton") while
     * still falling back to the standard chain (Button → FormControl).
     */
    std::string componentOverride;

    /**
     * @brief The widget this context describes, when there is one.
     *
     * Carried so token resolution can find the style overrides declared on the widget or
     * inherited from its ancestors. Which overrides those are, and how they are identified, is
     * internal to FreeCADStyle and ParameterManager.
     */
    const QWidget* widget = nullptr;

    bool operator==(const StyleContext&) const = default;

    /**
     * @brief Interns componentOverride strings to compact uint8_t IDs for cache key packing.
     *
     * Id 0 is reserved for "no override". Each unique string is assigned a new id
     * starting from 1 on first encounter. Call clear() when invalidating caches
     * that embed these ids (e.g. on theme change).
     */
    struct GuiExport Intern
    {
        /// Returns the process-wide singleton intern table.
        static Intern& global();

        uint8_t intern(const std::string& name);
        void clear();

    private:
        std::unordered_map<std::string, uint8_t> ids;
        uint8_t nextId = 1;
    };

    /**
     * @brief Returns a 64-bit context-only cache key (property bits left zero).
     *
     * The key does not cover `widget`. It identifies an entry *within* one override-set cache
     * bin; the bin itself is chosen by the caller from the widget.
     */
    uint64_t cacheKey() const;

    /// Returns a full 64-bit cache key including the property dimension.
    uint64_t cacheKey(StyleProperty property) const;

private:
    // clang-format off
    static constexpr uint8_t componentBitOffset = 0;
    static constexpr uint8_t elementBitOffset   = 8;
    static constexpr uint8_t stateBitOffset     = 12;
    static constexpr uint8_t propertyBitOffset  = 17;
    static constexpr uint8_t overrideBitOffset  = 24;
    static constexpr uint8_t variantBitOffset   = 32;
    // clang-format on

    static uint64_t packVariant(const VariantKey& variant);
};

/**
 * @brief Returns the token-name suffix string for a style property.
 *
 * Returns an empty string_view for unknown properties.
 */
GuiExport std::string_view propertyString(StyleProperty property);

}  // namespace Gui::StyleParameters

ENABLE_BITMASK_OPERATORS(Gui::StyleParameters::StyleState)
