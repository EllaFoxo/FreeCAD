// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <src/App/InitApplication.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Gui/GeometrySelection.h>

using Gui::GeometryQuantity;
using Gui::GeometryReference;
using Gui::GeometrySelection;

namespace
{
class GeometrySelectionTest: public ::testing::Test
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
        _docName = App::GetApplication().getUniqueDocumentName("geomsel_test");
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

TEST_F(GeometrySelectionTest, setReferencesReplacesModelAndEmits)
{
    GeometrySelection selection(GeometryQuantity::Single);
    int emitted = 0;
    QObject::connect(&selection, &GeometrySelection::referencesChanged, [&emitted] { ++emitted; });

    selection.setReferences({{.object = _objectA, .subName = "Edge1"}});

    ASSERT_EQ(selection.references().size(), 1U);
    EXPECT_EQ(selection.references().front().object, _objectA);
    EXPECT_EQ(selection.references().front().subName, "Edge1");
    EXPECT_EQ(emitted, 1);
}

TEST_F(GeometrySelectionTest, clearEmptiesModelAndEmits)
{
    GeometrySelection selection(GeometryQuantity::AllowMultiple);
    selection.setReferences({{.object = _objectA, .subName = ""}, {.object = _objectB, .subName = ""}});

    int emitted = 0;
    QObject::connect(&selection, &GeometrySelection::referencesChanged, [&emitted] { ++emitted; });

    selection.clear();

    EXPECT_TRUE(selection.references().empty());
    EXPECT_EQ(emitted, 1);
}

TEST_F(GeometrySelectionTest, removeReferenceDropsByIndex)
{
    GeometrySelection selection(GeometryQuantity::AllowMultiple);
    selection.setReferences(
        {{.object = _objectA, .subName = "Edge1"}, {.object = _objectB, .subName = "Edge2"}}
    );

    selection.removeReference(0);

    ASSERT_EQ(selection.references().size(), 1U);
    EXPECT_EQ(selection.references().front().object, _objectB);
}
