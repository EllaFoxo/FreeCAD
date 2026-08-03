// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QScrollBar>
#include <QTest>
#include <QWidget>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/PropertyView.h>
#include <Gui/SoFCDB.h>
#include <Gui/propertyeditor/PropertyEditor.h>

#include <src/App/InitApplication.h>

using Gui::PropertyEditor::PropertyEditor;

class TestPropertyEditorSizing: public QObject
{
    Q_OBJECT

public:
    TestPropertyEditorSizing()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        // Property items register themselves through the same call the real startup process
        // uses; without it the model can't turn the test object's properties into rows.
        if (!Gui::SoFCDB::isInitialized()) {
            Gui::SoFCDB::init();
        }
    }

private Q_SLOTS:

    void initTestCase()  // NOLINT
    {
        // Keep this fixture headless: a view-backed document would stand up a 3D view and
        // its Coin scene graph, which this offscreen test harness cannot support.
        App::DocumentInitFlags createFlags;
        createFlags.createView = false;
        document = App::GetApplication().newDocument("PropertyEditorSizing", nullptr, createFlags);
        object = document->addObject("App::FeatureTest", "Subject");
        QVERIFY(object != nullptr);
    }

    void cleanupTestCase()  // NOLINT
    {
        App::GetApplication().closeDocument("PropertyEditorSizing");
    }

    // The editor carries the token namespace the transparent panel styling resolves through.
    void test_declaresPropertyEditorComponent()  // NOLINT
    {
        PropertyEditor editor;

        QCOMPARE(editor.property("component").toString(), QStringLiteral("PropertyEditor"));
    }

    // An empty model needs no panel at all, so nothing is painted with nothing selected.
    void test_emptyModelHasNoContentHeight()  // NOLINT
    {
        PropertyEditor editor;

        QCOMPARE(editor.contentHeight(), 0);
    }

    // Content height must reflect what is actually on screen: once populated it covers more
    // than a single row plus the frame, and collapsing rows away shrinks it again.
    void test_populatedModelReportsMeaningfulHeight()  // NOLINT
    {
        PropertyEditor editor;
        buildUpSubject(editor);
        editor.expandAll();

        QVERIFY(editor.model()->rowCount() > 0);
        int expandedHeight = editor.contentHeight();
        QVERIFY(expandedHeight > 2 * editor.frameWidth());

        editor.collapseAll();
        int collapsedHeight = editor.contentHeight();

        QVERIFY(collapsedHeight > 0);
        QVERIFY(collapsedHeight < expandedHeight);
    }

    // Content height must not depend on how far the view happens to be scrolled — only on
    // what rows exist and are expanded — or a docked, scrolled editor would report the wrong
    // size the moment it becomes transparent.
    void test_contentHeightIsIndependentOfScrollPosition()  // NOLINT
    {
        PropertyEditor editor;
        buildUpSubject(editor);
        editor.expandAll();
        editor.resize(300, 100);

        int heightAtTop = editor.contentHeight();

        editor.verticalScrollBar()->setValue(editor.verticalScrollBar()->maximum());
        QVERIFY(editor.verticalScrollBar()->value() > 0);
        int heightWhenScrolled = editor.contentHeight();

        QCOMPARE(heightWhenScrolled, heightAtTop);
    }

    // QTreeView's layout code is not guaranteed to emit expanded()/collapsed() for every row
    // expandAll()/collapseAll() touch (it only does when a row was not already recorded as
    // expanded), so the cap has to be refreshed as part of those calls themselves. Signals
    // are blocked around each call so the pre-existing per-row connects cannot be the reason
    // the cap ends up right — only the explicit refresh in expandAll()/collapseAll() can.
    void test_expandAllAndCollapseAllKeepTheCapInSync()  // NOLINT
    {
        QWidget root;
        auto* editor = new PropertyEditor(&root);
        buildUpSubject(*editor);
        editor->collapseAll();

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, true);
        int collapsedCap = editor->maximumHeight();

        editor->blockSignals(true);
        editor->expandAll();
        editor->blockSignals(false);
        int expandedCap = editor->maximumHeight();

        QVERIFY(expandedCap > collapsedCap);
        QCOMPARE(expandedCap, editor->contentHeight());

        editor->blockSignals(true);
        editor->collapseAll();
        editor->blockSignals(false);
        int recollapsedCap = editor->maximumHeight();

        QCOMPARE(recollapsedCap, collapsedCap);
    }

    // Over an opaque surface nothing is capped — the editor fills whatever it is given.
    void test_opaqueEditorIsUncapped()  // NOLINT
    {
        QWidget root;
        auto* editor = new PropertyEditor(&root);
        buildUpSubject(*editor);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, false);

        QCOMPARE(editor->maximumHeight(), QWIDGETSIZE_MAX);
    }

    // Over a transparent surface the editor caps itself, so the panel stops at its rows.
    void test_transparentEditorCapsToContent()  // NOLINT
    {
        QWidget root;
        auto* editor = new PropertyEditor(&root);
        buildUpSubject(*editor);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, true);

        QVERIFY(editor->maximumHeight() < QWIDGETSIZE_MAX);
        QCOMPARE(editor->maximumHeight(), editor->contentHeight());
    }

    // Flipping the tag back must lift the cap, or a panel that leaves the overlay stays short.
    void test_capIsLiftedWhenTransparencyIsRevoked()  // NOLINT
    {
        QWidget root;
        auto* editor = new PropertyEditor(&root);
        buildUpSubject(*editor);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, true);
        // The cap must actually have engaged here, or lifting it below proves nothing.
        QVERIFY(editor->maximumHeight() < QWIDGETSIZE_MAX);

        style.updateTransparency(&root, false);

        QCOMPARE(editor->maximumHeight(), QWIDGETSIZE_MAX);
    }

    // Losing every row collapses the cap to nothing without needing a transparency change.
    void test_clearingContentCollapsesTheCap()  // NOLINT
    {
        QWidget root;
        auto* editor = new PropertyEditor(&root);
        buildUpSubject(*editor);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, true);
        QVERIFY(editor->maximumHeight() > 0);

        editor->buildUp({});

        QCOMPARE(editor->maximumHeight(), 0);
    }

    // A capped editor sits at the top of its tab page rather than floating in the middle.
    // QStackedLayout sets the page's geometry directly, so QWidget::setGeometry clamps the
    // height against the maximum while keeping the top-left corner. Introducing a page layout
    // here would route through QWidgetItem instead, which centres the excess.
    void test_cappedEditorAnchorsToTopOfTabPage()  // NOLINT
    {
        Gui::PropertyView propertyView;
        PropertyEditor* dataEditor = propertyView.propertyEditorData;
        buildUpSubject(*dataEditor);

        const int cappedHeight = dataEditor->contentHeight();
        QVERIFY(cappedHeight > 0);

        // Set the view to be transparent so the editor caps to its content height.
        Gui::FreeCADStyle style;
        style.updateTransparency(&propertyView, true);

        propertyView.resize(400, cappedHeight + 200);
        propertyView.show();
        QVERIFY(QTest::qWaitForWindowExposed(&propertyView));

        // The editor should be at the top of its tab page at y=0, not centered vertically.
        QCOMPARE(dataEditor->y(), 0);
        QCOMPARE(dataEditor->height(), cappedHeight);
    }

private:
    // Fills the editor from the test document object so the model has real rows.
    void buildUpSubject(PropertyEditor& editor)
    {
        std::map<std::string, App::Property*> properties;
        object->getPropertyMap(properties);

        Gui::PropertyEditor::PropertyModel::PropertyList list;
        for (const auto& [name, property] : properties) {
            list.emplace_back(name, std::vector<App::Property*> {property});
        }

        editor.buildUp(std::move(list));
    }

    App::Document* document = nullptr;
    App::DocumentObject* object = nullptr;
};

QTEST_MAIN(TestPropertyEditorSizing)

#include "PropertyEditorSizing.moc"
