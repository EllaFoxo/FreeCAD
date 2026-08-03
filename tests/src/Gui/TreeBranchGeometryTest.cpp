// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <QLine>
#include <QList>
#include <QRect>
#include <QStyle>

#include <Gui/FreeCADStyle.h>

namespace
{

// One indent cell of a 20px-indented tree on a 24px row, at depth 1.
constexpr QRect cell(20, 48, 20, 24);

// Centres land on half-pixel coordinates so a 1px stroke covers one pixel column
// rather than straddling two.
constexpr qreal centerX = 30.5;
constexpr qreal centerY = 60.5;

QList<QLineF> segmentsFor(QStyle::State state, bool topLevel = false)
{
    return Gui::FreeCADStyle::branchSegments(cell, state, topLevel);
}

}  // namespace

// A non-last child continues the vertical past its own row and elbows into the item.
TEST(TreeBranchGeometryTest, ItemWithFollowingSiblingDrawsThroughVerticalAndStub)
{
    const QList<QLineF> segments = segmentsFor(QStyle::State_Item | QStyle::State_Sibling);

    ASSERT_EQ(segments.size(), 2);
    EXPECT_EQ(segments.at(0), QLineF(centerX, 48, centerX, 72));
    EXPECT_EQ(segments.at(1), QLineF(centerX, centerY, 40, centerY));
}

// A last child closes the vertical at the elbow instead of running it to the next row.
TEST(TreeBranchGeometryTest, LastChildStopsTheVerticalAtTheElbow)
{
    const QList<QLineF> segments = segmentsFor(QStyle::State_Item);

    ASSERT_EQ(segments.size(), 2);
    EXPECT_EQ(segments.at(0), QLineF(centerX, 48, centerX, centerY));
    EXPECT_EQ(segments.at(1), QLineF(centerX, centerY, 40, centerY));
}

// An ancestor level carries the guide past rows that are not its own children.
TEST(TreeBranchGeometryTest, AncestorWithSiblingDrawsOnlyTheGuide)
{
    const QList<QLineF> segments = segmentsFor(QStyle::State_Sibling);

    ASSERT_EQ(segments.size(), 1);
    EXPECT_EQ(segments.at(0), QLineF(centerX, 48, centerX, 72));
}

// Past the last child of a level there is nothing left to connect.
TEST(TreeBranchGeometryTest, ExhaustedAncestorDrawsNothing)
{
    EXPECT_TRUE(segmentsFor(QStyle::State_None).isEmpty());
}

// A root has no parent to reach toward, so its cell stays empty whatever the flags say.
TEST(TreeBranchGeometryTest, TopLevelCellDrawsNothing)
{
    EXPECT_TRUE(segmentsFor(QStyle::State_Item | QStyle::State_Sibling, true).isEmpty());
    EXPECT_TRUE(segmentsFor(QStyle::State_Sibling, true).isEmpty());
}
