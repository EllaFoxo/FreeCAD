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

#include <gtest/gtest.h>

#include <Gui/StyleParameters/StyleOverrides.h>

using namespace Gui::StyleParameters;

TEST(OverrideRegistryTest, EmptySetIsIdZeroAndIsNotStored)
{
    OverrideRegistry registry;

    EXPECT_EQ(registry.intern(OverrideSet {}), OverrideRegistry::emptyId);
    EXPECT_EQ(registry.size(), 0U);
    EXPECT_TRUE(registry.get(OverrideRegistry::emptyId).empty());
}

TEST(OverrideRegistryTest, IdenticalSetsShareOneId)
{
    OverrideRegistry registry;

    const uint32_t first = registry.intern({{"PaneBackground", "@ListBackground"}});
    const uint32_t second = registry.intern({{"PaneBackground", "@ListBackground"}});

    EXPECT_EQ(first, second);
    EXPECT_NE(first, OverrideRegistry::emptyId);
    EXPECT_EQ(registry.size(), 1U);
}

TEST(OverrideRegistryTest, DifferentSetsGetDifferentIds)
{
    OverrideRegistry registry;

    const uint32_t first = registry.intern({{"PaneBackground", "@ListBackground"}});
    const uint32_t second = registry.intern({{"PaneBackground", "@TreeBackground"}});

    EXPECT_NE(first, second);
    EXPECT_EQ(registry.size(), 2U);
}

// The set is ordered, so two widgets that declared the same overrides in a different order are
// still one set and still share a resolution cache.
TEST(OverrideRegistryTest, InsertionOrderDoesNotAffectIdentity)
{
    OverrideRegistry registry;

    OverrideSet forward;
    forward.emplace("Alpha", "1px");
    forward.emplace("Beta", "2px");

    OverrideSet reversed;
    reversed.emplace("Beta", "2px");
    reversed.emplace("Alpha", "1px");

    EXPECT_EQ(registry.intern(forward), registry.intern(reversed));
    EXPECT_EQ(registry.size(), 1U);
}

TEST(OverrideRegistryTest, GetReturnsTheStoredContent)
{
    OverrideRegistry registry;

    const uint32_t identifier = registry.intern({{"PaneBackground", "@ListBackground"}});

    ASSERT_EQ(registry.get(identifier).size(), 1U);
    EXPECT_EQ(registry.get(identifier).at("PaneBackground"), "@ListBackground");
}

// A widget can outlive nothing here, but a stale or garbage id must not index out of bounds.
TEST(OverrideRegistryTest, UnknownIdReturnsTheEmptySet)
{
    OverrideRegistry registry;
    registry.intern({{"PaneBackground", "@ListBackground"}});

    EXPECT_TRUE(registry.get(9999).empty());
}
