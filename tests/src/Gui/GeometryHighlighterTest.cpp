// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <src/App/InitApplication.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Gui/GeometryHighlighter.h>

using Gui::GeometryHighlightModel;
using Gui::GeometryReference;
using Gui::HighlightRole;

namespace
{
class GeometryHighlighterTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        App::DocumentInitFlags createFlags;
        createFlags.createView = false;
        _docName = App::GetApplication().getUniqueDocumentName("geomhl_test");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser", createFlags);
        _objectA = _doc->addObject("App::FeatureTest", "ObjectA");
        _objectB = _doc->addObject("App::FeatureTest", "ObjectB");
    }

    void TearDown() override
    {
        if (App::GetApplication().getDocument(_docName.c_str())) {
            App::GetApplication().closeDocument(_docName.c_str());
        }
    }

    std::string _docName;
    App::Document* _doc {};
    App::DocumentObject* _objectA {};
    App::DocumentObject* _objectB {};
};

/// A model whose selection predicate reports nothing as selected.
GeometryHighlightModel makeModel()
{
    return GeometryHighlightModel([](const GeometryReference&) { return false; });
}
}  // namespace

TEST_F(GeometryHighlighterTest, setHighlightedReplacesRatherThanAccumulates)
{
    GeometryHighlightModel model = makeModel();

    model.setHighlighted(HighlightRole::Reference, {{.object = _objectA, .subName = "Face1"}});
    model.setHighlighted(HighlightRole::Reference, {{.object = _objectB, .subName = "Face2"}});

    const std::vector<GeometryReference> effective = model.effective(HighlightRole::Reference);
    ASSERT_EQ(effective.size(), 1U);
    EXPECT_EQ(effective.front().object, _objectB);
    EXPECT_EQ(effective.front().subName, "Face2");
}

TEST_F(GeometryHighlighterTest, hoveredReferenceIsExcludedFromReferenceRole)
{
    GeometryHighlightModel model = makeModel();
    const GeometryReference first {.object = _objectA, .subName = "Face1"};
    const GeometryReference second {.object = _objectB, .subName = "Face2"};

    model.setHighlighted(HighlightRole::Reference, {first, second});
    model.setHighlighted(HighlightRole::Hovered, {second});

    const std::vector<GeometryReference> effective = model.effective(HighlightRole::Reference);
    ASSERT_EQ(effective.size(), 1U);
    EXPECT_EQ(effective.front(), first);
    EXPECT_EQ(model.effective(HighlightRole::Hovered), std::vector {second});
}

TEST_F(GeometryHighlighterTest, clearingHoverRestoresTheReference)
{
    GeometryHighlightModel model = makeModel();
    const GeometryReference only {.object = _objectA, .subName = "Face1"};

    model.setHighlighted(HighlightRole::Reference, {only});
    model.setHighlighted(HighlightRole::Hovered, {only});
    ASSERT_TRUE(model.effective(HighlightRole::Reference).empty());

    model.clear(HighlightRole::Hovered);

    EXPECT_EQ(model.effective(HighlightRole::Reference), std::vector {only});
}

TEST_F(GeometryHighlighterTest, selectedReferenceIsExcludedFromReferenceRole)
{
    const GeometryReference selected {.object = _objectA, .subName = "Face1"};
    const GeometryReference other {.object = _objectB, .subName = "Face2"};

    GeometryHighlightModel model([selected](const GeometryReference& reference) {
        return reference == selected;
    });
    model.setHighlighted(HighlightRole::Reference, {selected, other});

    const std::vector<GeometryReference> effective = model.effective(HighlightRole::Reference);
    ASSERT_EQ(effective.size(), 1U);
    EXPECT_EQ(effective.front(), other);
}

TEST_F(GeometryHighlighterTest, selectedReferenceIsStillHighlightedWhenHovered)
{
    const GeometryReference selected {.object = _objectA, .subName = "Face1"};

    GeometryHighlightModel model([](const GeometryReference&) { return true; });
    model.setHighlighted(HighlightRole::Hovered, {selected});

    EXPECT_EQ(model.effective(HighlightRole::Hovered), std::vector {selected});
}

TEST_F(GeometryHighlighterTest, dropObjectRemovesItFromEveryRole)
{
    GeometryHighlightModel model = makeModel();
    const GeometryReference doomed {.object = _objectA, .subName = "Face1"};
    const GeometryReference survivor {.object = _objectB, .subName = "Face2"};

    model.setHighlighted(HighlightRole::Reference, {doomed, survivor});
    model.setHighlighted(HighlightRole::Hovered, {doomed});

    model.dropObject(_objectA);

    EXPECT_EQ(model.effective(HighlightRole::Reference), std::vector {survivor});
    EXPECT_TRUE(model.effective(HighlightRole::Hovered).empty());
}

TEST_F(GeometryHighlighterTest, dropDocumentRemovesEveryReferenceInThatDocument)
{
    GeometryHighlightModel model = makeModel();

    model.setHighlighted(
        HighlightRole::Reference,
        {{.object = _objectA, .subName = "Face1"}, {.object = _objectB, .subName = "Face2"}}
    );

    model.dropDocument(_doc);

    EXPECT_TRUE(model.effective(HighlightRole::Reference).empty());
}

TEST_F(GeometryHighlighterTest, clearEmptiesEveryRole)
{
    GeometryHighlightModel model = makeModel();

    model.setHighlighted(HighlightRole::Reference, {{.object = _objectA, .subName = "Face1"}});
    model.setHighlighted(HighlightRole::Hovered, {{.object = _objectB, .subName = "Face2"}});

    model.clear();

    EXPECT_TRUE(model.effective(HighlightRole::Reference).empty());
    EXPECT_TRUE(model.effective(HighlightRole::Hovered).empty());
}
