// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <src/App/InitApplication.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyLinks.h>
#include <Gui/GeometryHighlighter.h>
#include <Gui/GeometrySelection.h>
#include <Gui/GeometrySelectionGate.h>
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

TEST_F(GeometrySelectionTest, destructionWhileSelectingRemovesGateWithoutEmitting)
{
    int exited = 0;
    {
        GeometrySelection selection(GeometryQuantity::Single);
        selection.setSelectionFilter(QStringLiteral("SELECT App::FeatureTest"));
        QObject::connect(&selection, &GeometrySelection::selectionModeExited, [&exited] { ++exited; });
        selection.startSelecting();
        ASSERT_NE(Gui::Selection().getSelectionGate(_doc), nullptr);
    }
    // Destruction must unwind the session (gate removed) but must NOT emit: the
    // signals are wired to the owning widget, which during real teardown may already
    // be half-destroyed, so emitting from the destructor would abort.
    EXPECT_EQ(exited, 0);
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

    void simulateDeselect(const std::string& docName, const char* objectName, const char* subName = "")
    {
        Gui::SelectionChanges
            msg(Gui::SelectionChanges::RmvSelection, docName.c_str(), objectName, subName);
        onSelectionChanged(msg);
    }

    void simulateClear(const std::string& docName)
    {
        Gui::SelectionChanges msg(Gui::SelectionChanges::ClrSelection, docName.c_str());
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

TEST_F(GeometrySelectionTest, singleModePickReplacesAndEndsSession)
{
    PickableSelection selection(GeometryQuantity::Single);
    selection.setReferences({{.object = _objectA, .subName = ""}});
    selection.startSelecting();

    selection.simulatePick(_docName, _objectB->getNameInDocument());

    // The replacing pick swaps the reference and completes the session.
    ASSERT_EQ(selection.references().size(), 1U);
    EXPECT_EQ(selection.references().front().object, _objectB);
    EXPECT_FALSE(selection.isSelecting());
}

TEST_F(GeometrySelectionTest, cancelSelectingRestoresPreviousReference)
{
    PickableSelection selection(GeometryQuantity::Single);
    selection.setReferences({{.object = _objectA, .subName = ""}});
    selection.startSelecting();

    selection.cancelSelecting();

    ASSERT_EQ(selection.references().size(), 1U);
    EXPECT_EQ(selection.references().front().object, _objectA);
    EXPECT_FALSE(selection.isSelecting());
}

TEST_F(GeometrySelectionTest, cancelSelectingRevertsAppendedPicks)
{
    AppendingSelection selection;
    selection.setReferences({{.object = _objectA, .subName = ""}});
    selection.startSelecting();

    selection.simulatePick(_docName, _objectB->getNameInDocument());
    ASSERT_EQ(selection.references().size(), 2U);

    selection.cancelSelecting();

    ASSERT_EQ(selection.references().size(), 1U);
    EXPECT_EQ(selection.references().front().object, _objectA);
    EXPECT_FALSE(selection.isSelecting());
}

TEST_F(GeometrySelectionTest, allowMultipleDeselectRemovesMatchingReference)
{
    PickableSelection selection(GeometryQuantity::AllowMultiple);
    selection.setReferences({{.object = _objectA, .subName = ""}, {.object = _objectB, .subName = ""}});
    selection.startSelecting();

    // Ctrl-clicking an already-selected item deselects it in the 3D view, which
    // must drop the matching reference while the session stays open.
    selection.simulateDeselect(_docName, _objectA->getNameInDocument());

    ASSERT_EQ(selection.references().size(), 1U);
    EXPECT_EQ(selection.references().front().object, _objectB);
    EXPECT_TRUE(selection.isSelecting());

    selection.stopSelecting();
}

TEST_F(GeometrySelectionTest, singleModeIgnoresDeselect)
{
    PickableSelection selection(GeometryQuantity::Single);
    selection.setReferences({{.object = _objectA, .subName = ""}});
    selection.startSelecting();

    // A single-value selector has no per-item toggle; a deselect is a no-op.
    selection.simulateDeselect(_docName, _objectA->getNameInDocument());

    ASSERT_EQ(selection.references().size(), 1U);
    EXPECT_EQ(selection.references().front().object, _objectA);

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

TEST_F(GeometrySelectionTest, bindReadsInitialPropertyValue)
{
    auto* prop = static_cast<App::PropertyLinkSub*>(
        _objectA->addDynamicProperty("App::PropertyLinkSub", "TestLink")
    );
    prop->setValue(_objectB, std::vector<std::string> {"Edge1"});

    GeometrySelection selection(GeometryQuantity::Single);
    selection.bind(*prop);

    ASSERT_EQ(selection.references().size(), 1U);
    EXPECT_EQ(selection.references().front().object, _objectB);
    EXPECT_EQ(selection.references().front().subName, "Edge1");
}

TEST_F(GeometrySelectionTest, autoApplyWritesBackToProperty)
{
    auto* prop = static_cast<App::PropertyLinkSub*>(
        _objectA->addDynamicProperty("App::PropertyLinkSub", "TestLink")
    );

    GeometrySelection selection(GeometryQuantity::Single);
    selection.bind(*prop);
    selection.setReferences({{.object = _objectB, .subName = "Face2"}});

    EXPECT_EQ(prop->getValue(), _objectB);
    ASSERT_EQ(prop->getSubValues().size(), 1U);
    EXPECT_EQ(prop->getSubValues().front(), "Face2");
}

TEST_F(GeometrySelectionTest, wholeObjectWritesEmptySubList)
{
    auto* prop = static_cast<App::PropertyLinkSub*>(
        _objectA->addDynamicProperty("App::PropertyLinkSub", "TestLink")
    );

    GeometrySelection selection(GeometryQuantity::Single);
    selection.bind(*prop);
    selection.setReferences({{.object = _objectB, .subName = ""}});

    // A whole-object reference must not leave a spurious empty subelement.
    EXPECT_EQ(prop->getValue(), _objectB);
    EXPECT_TRUE(prop->getSubValues().empty());
}

TEST_F(GeometrySelectionTest, bindDropsWhenOwningObjectDeleted)
{
    auto* prop = static_cast<App::PropertyLinkSub*>(
        _objectA->addDynamicProperty("App::PropertyLinkSub", "TestLink")
    );

    GeometrySelection selection(GeometryQuantity::Single);
    selection.bind(*prop);
    ASSERT_TRUE(selection.isBound());

    // Deleting the property's owning object (as an aborted create command would)
    // must drop the binding, so no later signal can touch the freed property.
    _doc->removeObject(_objectA->getNameInDocument());
    _objectA = nullptr;

    EXPECT_FALSE(selection.isBound());
    // A subsequent model change must be a safe no-op rather than a stale write.
    selection.setReferences({{.object = _objectB, .subName = ""}});
    EXPECT_FALSE(selection.apply());
}

TEST_F(GeometrySelectionTest, stagedApplyDefersWrite)
{
    auto* prop = static_cast<App::PropertyLinkSub*>(
        _objectA->addDynamicProperty("App::PropertyLinkSub", "TestLink")
    );

    GeometrySelection selection(GeometryQuantity::Single);
    selection.bind(*prop);
    selection.setAutoApply(false);
    selection.setReferences({{.object = _objectB, .subName = ""}});

    EXPECT_EQ(prop->getValue(), nullptr);  // not written yet
    EXPECT_TRUE(selection.apply());
    EXPECT_EQ(prop->getValue(), _objectB);
}

TEST_F(GeometrySelectionTest, kindGateAllowsFacesRejectsEdges)
{
    Gui::GeometryKindGate gate(Gui::GeometryKind::Face, nullptr);
    EXPECT_TRUE(gate.allow(_doc, _objectA, "Face3"));
    EXPECT_FALSE(gate.allow(_doc, _objectA, "Edge3"));
}

TEST_F(GeometrySelectionTest, kindGateWholeObjectAllowsEmptySub)
{
    Gui::GeometryKindGate gate(Gui::GeometryKind::WholeObject, nullptr);
    EXPECT_TRUE(gate.allow(_doc, _objectA, ""));
    EXPECT_FALSE(gate.allow(_doc, _objectA, "Face1"));
}

TEST_F(GeometrySelectionTest, referencesArePushedToTheHighlighter)
{
    GeometrySelection selection(GeometryQuantity::AllowMultiple);

    selection.setReferences(
        {{.object = _objectA, .subName = "Face1"}, {.object = _objectB, .subName = "Face2"}}
    );

    const auto highlighted = selection.highlighter()->model().effective(Gui::HighlightRole::Reference);
    EXPECT_EQ(highlighted.size(), 2U);
}

TEST_F(GeometrySelectionTest, setHoveredReferenceMarksThatOneHovered)
{
    GeometrySelection selection(GeometryQuantity::AllowMultiple);
    selection.setReferences(
        {{.object = _objectA, .subName = "Face1"}, {.object = _objectB, .subName = "Face2"}}
    );

    selection.setHoveredReference(1);

    const auto hovered = selection.highlighter()->model().effective(Gui::HighlightRole::Hovered);
    ASSERT_EQ(hovered.size(), 1U);
    EXPECT_EQ(hovered.front().object, _objectB);
    // The hovered reference is drawn once, in the hovered style.
    EXPECT_EQ(selection.highlighter()->model().effective(Gui::HighlightRole::Reference).size(), 1U);
}

TEST_F(GeometrySelectionTest, setHoveredReferenceClearsWithMinusOne)
{
    GeometrySelection selection(GeometryQuantity::Single);
    selection.setReferences({{.object = _objectA, .subName = "Face1"}});
    selection.setHoveredReference(0);

    selection.setHoveredReference(-1);

    EXPECT_TRUE(selection.highlighter()->model().effective(Gui::HighlightRole::Hovered).empty());
}

TEST_F(GeometrySelectionTest, setHoveredReferenceIgnoresAnOutOfRangeIndex)
{
    GeometrySelection selection(GeometryQuantity::Single);
    selection.setReferences({{.object = _objectA, .subName = "Face1"}});

    // Establish a real hover first: without it a setHoveredReference() that did
    // nothing at all would satisfy the assertion below.
    selection.setHoveredReference(0);
    ASSERT_EQ(selection.highlighter()->model().effective(Gui::HighlightRole::Hovered).size(), 1U);

    selection.setHoveredReference(7);

    EXPECT_TRUE(selection.highlighter()->model().effective(Gui::HighlightRole::Hovered).empty());
}

// Seeding is what normally changes the effective Reference set at a session boundary,
// but it needs a live Gui::Application. Emptying the role behind the selection's back
// stands in for it: only a republish from the boundary itself can put it back.
TEST_F(GeometrySelectionTest, enteringSelectionModeRefreshesTheHighlight)
{
    GeometrySelection selection(GeometryQuantity::AllowMultiple);
    selection.setReferences({{.object = _objectA, .subName = ""}, {.object = _objectB, .subName = ""}});
    selection.highlighter()->clear(Gui::HighlightRole::Reference);
    ASSERT_TRUE(selection.highlighter()->model().effective(Gui::HighlightRole::Reference).empty());

    selection.startSelecting();

    EXPECT_EQ(selection.highlighter()->model().effective(Gui::HighlightRole::Reference).size(), 2U);

    selection.stopSelecting();
}

TEST_F(GeometrySelectionTest, leavingSelectionModeRefreshesTheHighlight)
{
    GeometrySelection selection(GeometryQuantity::AllowMultiple);
    selection.setReferences({{.object = _objectA, .subName = ""}, {.object = _objectB, .subName = ""}});
    selection.startSelecting();
    selection.highlighter()->clear(Gui::HighlightRole::Reference);
    ASSERT_TRUE(selection.highlighter()->model().effective(Gui::HighlightRole::Reference).empty());

    selection.stopSelecting();

    EXPECT_EQ(selection.highlighter()->model().effective(Gui::HighlightRole::Reference).size(), 2U);
}

TEST_F(GeometrySelectionTest, clearingTheViewportSelectionRefreshesTheHighlight)
{
    PickableSelection selection(GeometryQuantity::AllowMultiple);
    selection.setReferences({{.object = _objectA, .subName = ""}, {.object = _objectB, .subName = ""}});
    selection.startSelecting();
    // Stands in for the seeded selection having taken the references over.
    selection.highlighter()->clear(Gui::HighlightRole::Reference);

    // A click on empty space mid-session deselects them, so nothing paints them any
    // more and the Reference role has to take them back at once — not when the
    // deferred confirm eventually ends the session.
    selection.simulateClear(_docName);

    EXPECT_EQ(selection.highlighter()->model().effective(Gui::HighlightRole::Reference).size(), 2U);

    selection.stopSelecting();
}

TEST_F(GeometrySelectionTest, viewportDeselectRefreshesTheHighlight)
{
    PickableSelection selection(GeometryQuantity::AllowMultiple);
    selection.setReferences({{.object = _objectA, .subName = ""}});
    selection.startSelecting();
    selection.highlighter()->clear(Gui::HighlightRole::Reference);

    // A deselect matching no reference leaves the model alone, but it still changes
    // what the 3D selection paints, so the Reference role is republished anyway.
    selection.simulateDeselect(_docName, _objectB->getNameInDocument());

    EXPECT_EQ(selection.highlighter()->model().effective(Gui::HighlightRole::Reference).size(), 1U);

    selection.stopSelecting();
}

TEST_F(GeometrySelectionTest, clearingReferencesClearsTheHighlight)
{
    GeometrySelection selection(GeometryQuantity::Single);
    selection.setReferences({{.object = _objectA, .subName = "Face1"}});

    selection.clear();

    EXPECT_TRUE(selection.highlighter()->model().effective(Gui::HighlightRole::Reference).empty());
}
