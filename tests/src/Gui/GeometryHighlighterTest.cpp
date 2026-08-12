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
}  // namespace

TEST_F(GeometryHighlighterTest, setHighlightedReplacesRatherThanAccumulates)
{
    GeometryHighlightModel model;

    model.setHighlighted(HighlightRole::Reference, {{.object = _objectA, .subName = "Face1"}});
    model.setHighlighted(HighlightRole::Reference, {{.object = _objectB, .subName = "Face2"}});

    const std::vector<GeometryReference> effective = model.effective(HighlightRole::Reference);
    ASSERT_EQ(effective.size(), 1U);
    EXPECT_EQ(effective.front().object, _objectB);
    EXPECT_EQ(effective.front().subName, "Face2");
}

TEST_F(GeometryHighlighterTest, hoveredReferenceIsExcludedFromReferenceRole)
{
    GeometryHighlightModel model;
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
    GeometryHighlightModel model;
    const GeometryReference only {.object = _objectA, .subName = "Face1"};

    model.setHighlighted(HighlightRole::Reference, {only});
    model.setHighlighted(HighlightRole::Hovered, {only});
    ASSERT_TRUE(model.effective(HighlightRole::Reference).empty());

    model.clear(HighlightRole::Hovered);

    EXPECT_EQ(model.effective(HighlightRole::Reference), std::vector {only});
}

TEST_F(GeometryHighlighterTest, dropObjectRemovesItFromEveryRole)
{
    GeometryHighlightModel model;
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
    App::DocumentInitFlags createFlags;
    createFlags.createView = false;
    const std::string otherName = App::GetApplication().getUniqueDocumentName("geomhl_other");
    App::Document* otherDocument
        = App::GetApplication().newDocument(otherName.c_str(), "testUser", createFlags);
    App::DocumentObject* otherObject = otherDocument->addObject("App::FeatureTest", "ObjectC");
    const GeometryReference survivor {.object = otherObject, .subName = "Face3"};

    GeometryHighlightModel model;
    model.setHighlighted(
        HighlightRole::Reference,
        {{.object = _objectA, .subName = "Face1"}, {.object = _objectB, .subName = "Face2"}, survivor}
    );

    model.dropDocument(_doc);

    // Only the closed document's references go; another document's are untouched.
    EXPECT_EQ(model.effective(HighlightRole::Reference), std::vector {survivor});

    App::GetApplication().closeDocument(otherName.c_str());
}

TEST_F(GeometryHighlighterTest, clearEmptiesEveryRole)
{
    GeometryHighlightModel model;

    model.setHighlighted(HighlightRole::Reference, {{.object = _objectA, .subName = "Face1"}});
    model.setHighlighted(HighlightRole::Hovered, {{.object = _objectB, .subName = "Face2"}});

    model.clear();

    EXPECT_TRUE(model.effective(HighlightRole::Reference).empty());
    EXPECT_TRUE(model.effective(HighlightRole::Hovered).empty());
}

#include <Base/ServiceProvider.h>
#include <Gui/StyleParameters.h>
#include <Gui/StyleParameters/ParameterManager.h>

// The six colours the highlighter resolves. A face is see-through so the surface under
// it still reads; an edge and a vertex stay solid so the outline stays crisp.
TEST_F(GeometryHighlighterTest, theSixHighlightColourTokensResolve)
{
    auto* parameters = Base::provideService<Gui::StyleParameters::ParameterManager>();
    ASSERT_NE(parameters, nullptr);

    const Base::Color referenceFace = parameters->resolve(
        Gui::StyleParameters::GeometryHighlightReferenceFaceColor
    );
    const Base::Color referenceEdge = parameters->resolve(
        Gui::StyleParameters::GeometryHighlightReferenceEdgeColor
    );
    const Base::Color referencePoint = parameters->resolve(
        Gui::StyleParameters::GeometryHighlightReferencePointColor
    );
    const Base::Color hoveredFace = parameters->resolve(
        Gui::StyleParameters::GeometryHighlightHoveredFaceColor
    );
    const Base::Color hoveredEdge = parameters->resolve(
        Gui::StyleParameters::GeometryHighlightHoveredEdgeColor
    );
    const Base::Color hoveredPoint = parameters->resolve(
        Gui::StyleParameters::GeometryHighlightHoveredPointColor
    );

    EXPECT_LT(referenceFace.a, 1.0F);
    EXPECT_LT(hoveredFace.a, 1.0F);
    EXPECT_FLOAT_EQ(referenceEdge.a, 1.0F);
    EXPECT_FLOAT_EQ(referencePoint.a, 1.0F);
    EXPECT_FLOAT_EQ(hoveredEdge.a, 1.0F);
    EXPECT_FLOAT_EQ(hoveredPoint.a, 1.0F);
}
