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

#include "StyleContext.h"

namespace Gui::StyleParameters
{

// ─── StyleContext::Intern ─────────────────────────────────────────────────────

StyleContext::Intern& StyleContext::Intern::global()
{
    static Intern instance;
    return instance;
}

uint8_t StyleContext::Intern::intern(const std::string& name)
{
    if (const auto found = ids.find(name); found != ids.end()) {
        return found->second;
    }

    const uint8_t id = nextId++;
    ids.emplace(name, id);
    return id;
}

void StyleContext::Intern::clear()
{
    ids.clear();
    nextId = 1;
}

// ─── StyleContext cache key packing ───────────────────────────────────────────

/*static*/ uint64_t StyleContext::packVariant(const VariantKey& variant)
{
    uint64_t packed = 0;
    for (size_t index = 0; index < variant.slots.size(); ++index) {
        packed |= static_cast<uint64_t>(variant.slots.at(index)) << (index * 4);
    }
    return packed;
}

uint64_t StyleContext::cacheKey() const
{
    const uint8_t overrideId = componentOverride.empty()
        ? static_cast<uint8_t>(0)
        : Intern::global().intern(componentOverride);

    // clang-format off
    return (static_cast<uint64_t>(component)                << componentBitOffset)
         | (static_cast<uint64_t>(element)                  << elementBitOffset)
         | (static_cast<uint64_t>(state.toUnderlyingType()) << stateBitOffset)
         | (static_cast<uint64_t>(overrideId)               << overrideBitOffset)
         | (packVariant(variant)                            << variantBitOffset);
    // clang-format on
}

uint64_t StyleContext::cacheKey(StyleProperty property) const
{
    return cacheKey() | (static_cast<uint64_t>(property) << propertyBitOffset);
}

}  // namespace Gui::StyleParameters
