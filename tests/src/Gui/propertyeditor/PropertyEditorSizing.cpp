// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QTest>
#include <QWidget>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
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

    // Content height tracks the rows the view would have to show, which is exactly what
    // QTreeView::viewportSizeHint() reports, plus the frame the box is drawn around.
    void test_populatedModelReportsViewportHeightPlusFrame()  // NOLINT
    {
        PropertyEditor editor;
        buildUpSubject(editor);

        QVERIFY(editor.model()->rowCount() > 0);
        QCOMPARE(editor.contentHeight(), editor.viewportSizeHint().height() + 2 * editor.frameWidth());
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
