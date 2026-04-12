// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2026 Kacper Donat <kacper@kadet.net>                     *
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

#ifndef GUI_STYLEPARAMETERS_DYNAMICSTYLEPARAMETERPROVIDER_H
#define GUI_STYLEPARAMETERS_DYNAMICSTYLEPARAMETERPROVIDER_H

#include <string>
#include <unordered_map>

#include <FCGlobal.h>

#include "Value.h"

class QWidget;

namespace Gui::StyleParameters
{

using StyleParameterOverrides = std::unordered_map<std::string, Value>;

/**
 * @brief Supplies per-widget style token overrides that are contextual (per-widget).
 *
 * Providers are consulted once per widget at polish time. Each provider
 * returns the complete set of overrides it contributes for that widget;
 * `ParameterManager` merges all providers' results in priority order and
 * caches the outcome until the widget is unpolished.
 *
 * Typical uses:
 *  - exposing explicit per-widget overrides set via Qt dynamic properties,
 *  - computing values from the widget's environment (e.g. mirroring the
 *    parent frame's background so an active tab blends in),
 */
class GuiExport DynamicStyleParameterProvider
{
public:
    DynamicStyleParameterProvider() = default;
    virtual ~DynamicStyleParameterProvider() = default;

    FC_DISABLE_COPY_MOVE(DynamicStyleParameterProvider)

    /**
     * @brief Returns every override this provider supplies for the widget.
     *
     * Implementations must be deterministic for the widget's current state
     * and should return an empty map (not a partial one) when nothing applies.
     */
    virtual StyleParameterOverrides overridesFor(const QWidget* widget) const = 0;

    /**
     * @brief Stable sort key; lower priorities are merged first and their
     *        entries win over later (higher-priority) providers.
     */
    virtual int priority() const
    {
        return 0;
    }
};

/**
 * @brief Exposes per-widget token overrides set via Qt dynamic properties.
 *
 * Widgets opt in by setting a dynamic property whose name starts with
 * `fcStyle.` followed by the fully-qualified token name, e.g.
 *
 * @code
 * tabBar->setProperty("fcStyle.TabBarPaneBackground",
 *                     QVariant::fromValue(Base::valueFrom(myBrush)));
 * @endcode
 *
 * The QVariant must carry a `Gui::StyleParameters::Value`. This provider
 * has the highest priority so explicit overrides always beat computed
 * fallbacks.
 */
class GuiExport DynamicPropertyProvider: public DynamicStyleParameterProvider
{
public:
    /// Prefix applied to Qt dynamic property names recognised by this provider.
    static constexpr const char* propertyPrefix = "fcStyle.";

    StyleParameterOverrides overridesFor(const QWidget* widget) const override;

    int priority() const override
    {
        return 0;
    }
};

/**
 * @brief Provider that infers a widget's surrounding background at paint time.
 *
 * Runs at a lower priority than `DynamicPropertyProvider`, so an explicit
 * override on the widget itself always wins.
 */
class GuiExport ComputedBackgroundProvider: public DynamicStyleParameterProvider
{
public:
    StyleParameterOverrides overridesFor(const QWidget* widget) const override;

    int priority() const override
    {
        return 100;  // NOLINT(*-avoid-magic-numbers)
    }
};

}  // namespace Gui::StyleParameters

#endif  // GUI_STYLEPARAMETERS_DYNAMICSTYLEPARAMETERPROVIDER_H
