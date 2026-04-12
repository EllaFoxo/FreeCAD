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

#include <string_view>

#include "Application.h"
#include "Utilities.h"

using namespace Gui::StyleParameters;

StyleParameterOverrides DynamicPropertyProvider::overridesFor(const QWidget* widget) const
{
    StyleParameterOverrides overrides;
    if (!widget) {
        return overrides;
    }

    const auto qByteArrayToStringView = [](const QByteArray& ba) {
        return std::string_view(ba.constData(), ba.size());
    };

    const std::string_view prefix {propertyPrefix};

    for (const QByteArray& propertyName : widget->dynamicPropertyNames()) {
        const std::string_view name = qByteArrayToStringView(propertyName);

        if (!name.starts_with(prefix)) {
            continue;
        }

        const QVariant variant = widget->property(propertyName.constData());
        if (!variant.isValid() || !variant.canConvert<Value>()) {
            continue;
        }

        overrides.emplace(std::string(name.substr(prefix.size())), variant.value<Value>());
    }

    return overrides;
}
namespace
{

std::optional<Value> tryGuessBackground(const QWidget* widget)
{
    const auto* styleParameterManager = Gui::Application::Instance->styleParameterManager();

    for (const QWidget* ancestor = widget->parentWidget(); ancestor != nullptr;
         ancestor = ancestor->parentWidget()) {

        if (const auto& overrides = styleParameterManager->getOverrides(ancestor);
            overrides.contains("CurrentPaneBackground")) {
            return overrides.at("CurrentPaneBackground");
        }

        if (ancestor->autoFillBackground()) {
            return Base::convertTo<Value>(ancestor->palette().brush(QPalette::Window));
        }
    }

    return std::nullopt;
}

bool isTabFamily(const QWidget* widget)
{
    return qobject_cast<const QTabBar*>(widget) != nullptr
        || qobject_cast<const QTabWidget*>(widget) != nullptr;
}

}  // namespace

StyleParameterOverrides ComputedBackgroundProvider::overridesFor(const QWidget* widget) const
{
    if (!widget || !isTabFamily(widget)) {
        return {};
    }

    StyleParameterOverrides overrides;

    if (auto value = tryGuessBackground(widget)) {
        overrides.emplace("CurrentPaneBackground", *std::move(value));
    }

    return overrides;
}
