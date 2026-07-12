// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <src/App/InitApplication.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Gui/GeometrySelection.h>
#include <Gui/Selection/Selection.h>
#include <Gui/Selection/SelectionFilter.h>

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

TEST_F(GeometrySelectionTest, startSelectingInstallsGateAndEmitsEntered)
{
    GeometrySelection selection(GeometryQuantity::Single);
    selection.setSelectionFilter(QStringLiteral("SELECT App::FeatureTest"));

    int entered = 0;
    QObject::connect(&selection, &GeometrySelection::selectionModeEntered, [&entered] { ++entered; });

    selection.startSelecting();

    EXPECT_TRUE(selection.isSelecting());
    EXPECT_EQ(entered, 1);
    EXPECT_NE(Gui::Selection().getSelectionGate(_doc), nullptr);

    selection.stopSelecting();
}

TEST_F(GeometrySelectionTest, stopSelectingRemovesGateAndEmitsExited)
{
    GeometrySelection selection(GeometryQuantity::Single);
    selection.setSelectionFilter(QStringLiteral("SELECT App::FeatureTest"));

    int exited = 0;
    QObject::connect(&selection, &GeometrySelection::selectionModeExited, [&exited] { ++exited; });

    selection.startSelecting();
    selection.stopSelecting();

    EXPECT_FALSE(selection.isSelecting());
    EXPECT_EQ(exited, 1);
    EXPECT_EQ(Gui::Selection().getSelectionGate(_doc), nullptr);
}

TEST_F(GeometrySelectionTest, destructionWhileSelectingEmitsExitedAndRemovesGate)
{
    int exited = 0;
    {
        GeometrySelection selection(GeometryQuantity::Single);
        selection.setSelectionFilter(QStringLiteral("SELECT App::FeatureTest"));
        QObject::connect(&selection, &GeometrySelection::selectionModeExited, [&exited] { ++exited; });
        selection.startSelecting();
        ASSERT_NE(Gui::Selection().getSelectionGate(_doc), nullptr);
    }
    EXPECT_EQ(exited, 1);
    EXPECT_EQ(Gui::Selection().getSelectionGate(_doc), nullptr);
}

namespace
{

/// Test helper: exposes simulatePick() to call onSelectionChanged() directly,
/// bypassing Gui::Selection().addSelection() which requires a live Gui::Application.
class PickableSelection: public GeometrySelection
{
public:
    using GeometrySelection::GeometrySelection;

    void simulatePick(const std::string& docName, const char* objectName, const char* subName = "")
    {
        Gui::SelectionChanges
            msg(Gui::SelectionChanges::AddSelection, docName.c_str(), objectName, subName);
        onSelectionChanged(msg);
    }
};

class AppendingSelection: public PickableSelection
{
public:
    AppendingSelection()
        : PickableSelection(GeometryQuantity::AllowMultiple)
    {}

protected:
    bool appendRequested() const override
    {
        return true;  // simulate Ctrl held, headless
    }
};

}  // namespace

TEST_F(GeometrySelectionTest, singleModePickReplaces)
{
    PickableSelection selection(GeometryQuantity::Single);
    selection.startSelecting();

    selection.simulatePick(_docName, _objectA->getNameInDocument());
    selection.simulatePick(_docName, _objectB->getNameInDocument());

    ASSERT_EQ(selection.references().size(), 1U);
    EXPECT_EQ(selection.references().front().object, _objectB);

    selection.stopSelecting();
}

TEST_F(GeometrySelectionTest, ignoresPicksWhenNotSelecting)
{
    PickableSelection selection(GeometryQuantity::Single);

    selection.simulatePick(_docName, _objectA->getNameInDocument());

    EXPECT_TRUE(selection.references().empty());
}

TEST_F(GeometrySelectionTest, allowMultipleAppendsWhenRequested)
{
    AppendingSelection selection;
    selection.startSelecting();

    selection.simulatePick(_docName, _objectA->getNameInDocument());
    selection.simulatePick(_docName, _objectB->getNameInDocument());

    ASSERT_EQ(selection.references().size(), 2U);
    EXPECT_EQ(selection.references()[0].object, _objectA);
    EXPECT_EQ(selection.references()[1].object, _objectB);

    selection.stopSelecting();
}

TEST_F(GeometrySelectionTest, startStopTogglesObserverAttachment)
{
    GeometrySelection selection(GeometryQuantity::Single);
    EXPECT_FALSE(selection.isSelectionAttached());

    selection.startSelecting();
    EXPECT_TRUE(selection.isSelectionAttached());

    selection.stopSelecting();
    EXPECT_FALSE(selection.isSelectionAttached());
}
