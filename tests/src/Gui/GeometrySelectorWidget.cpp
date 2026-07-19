// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QTest>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

#include <Gui/GeometrySelectorWidget.h>

#include <src/App/InitApplication.h>

// Gui::Application::Instance is NOT created in this test harness. The widget
// must degrade gracefully — no icon, just the name text.

class TestGeometrySelectorWidget: public QObject
{
    Q_OBJECT

public:
    TestGeometrySelectorWidget()
    {
        tests::initApplication();
    }

private Q_SLOTS:

    void init()
    {
        App::DocumentInitFlags flags;
        flags.createView = false;
        m_docName = App::GetApplication().getUniqueDocumentName("gsw_test");
        m_doc = App::GetApplication().newDocument(m_docName.c_str(), "testUser", flags);
        m_object = m_doc->addObject("App::FeatureTest", "TestObj");
    }

    void cleanup()
    {
        if (App::GetApplication().getDocument(m_docName.c_str())) {
            App::GetApplication().closeDocument(m_docName.c_str());
        }
        m_doc = nullptr;
        m_object = nullptr;
    }

    // (a) Construct the widget; verify selection() is non-null.
    void test_constructionExposesCoreSelection()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        QVERIFY(widget.selection() != nullptr);
    }

    // (b) Set a reference on the core; widget must survive the transition.
    void test_setReferencesDoesNotCrash()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.selection()->setReferences({{.object = m_object, .subName = "Edge1"}});
        QCoreApplication::processEvents();
        QVERIFY(widget.selection() != nullptr);
        QCOMPARE(static_cast<int>(widget.selection()->references().size()), 1);
    }

    // (c) Trigger selection mode programmatically; verify isSelecting() flips.
    void test_startSelectingEntersSelectingState()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        QVERIFY(!widget.selection()->isSelecting());
        widget.selection()->startSelecting();
        QVERIFY(widget.selection()->isSelecting());
        widget.selection()->stopSelecting();
        QVERIFY(!widget.selection()->isSelecting());
    }

    // (d) Clear empties the reference list.
    void test_clearEmptiesReferences()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.selection()->setReferences({{.object = m_object, .subName = "Edge1"}});
        QCOMPARE(static_cast<int>(widget.selection()->references().size()), 1);
        widget.selection()->clear();
        QCoreApplication::processEvents();
        QCOMPARE(static_cast<int>(widget.selection()->references().size()), 0);
    }

    // visualState() classifies purely from mode + references + isSelecting.
    void test_visualStateClassification()  // NOLINT
    {
        using VS = Gui::GeometrySelectorWidget::VisualState;

        Gui::GeometrySelectorWidget single(Gui::GeometryQuantity::Single);
        QCOMPARE(single.visualState(), VS::Empty);
        single.selection()->setReferences({{.object = m_object, .subName = "Edge1"}});
        QCOMPARE(single.visualState(), VS::ReferenceList);
        single.selection()->startSelecting();
        QCOMPARE(single.visualState(), VS::SelectingInline);
        single.selection()->stopSelecting();

        Gui::GeometrySelectorWidget multi(Gui::GeometryQuantity::AllowMultiple);
        multi.selection()->setReferences(
            {{.object = m_object, .subName = "Edge1"}, {.object = m_object, .subName = "Edge2"}}
        );
        QCOMPARE(multi.visualState(), VS::ReferenceList);
        multi.selection()->startSelecting();
        QCOMPARE(multi.visualState(), VS::SelectingOverlay);  // ≥2 committed → overlay
        multi.selection()->stopSelecting();

        multi.selection()->setReferences({{.object = m_object, .subName = "Edge1"}});
        multi.selection()->startSelecting();
        QCOMPARE(multi.visualState(), VS::SelectingInline);  // 1 committed → inline
        multi.selection()->stopSelecting();
    }

    // n=1 and n≥2 render through the same ReferenceListView with one row each.
    void test_referenceListRendersOneRowPerReference()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::AllowMultiple);
        widget.selection()->setReferences(
            {{.object = m_object, .subName = "Edge1"},
             {.object = m_object, .subName = "Edge2"},
             {.object = m_object, .subName = "Edge3"}}
        );
        QCoreApplication::processEvents();
        const auto rows = widget.findChildren<QWidget*>(QStringLiteral("gsw_reference_row"));
        QCOMPARE(rows.size(), 3);

        widget.selection()->setReferences({{.object = m_object, .subName = "Edge1"}});
        QCoreApplication::processEvents();
        QCOMPARE(widget.findChildren<QWidget*>(QStringLiteral("gsw_reference_row")).size(), 1);
    }

    // ≥4 references cap the list height so the 4th row is only partially visible.
    void test_referenceListCapsHeight()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::AllowMultiple);
        std::vector<Gui::GeometryReference> many;
        for (int index = 0; index < 6; ++index) {
            many.push_back({.object = m_object, .subName = "Edge" + std::to_string(index)});
        }
        widget.selection()->setReferences(many);
        QCoreApplication::processEvents();
        auto* list = widget.findChild<QWidget*>(QStringLiteral("gsw_reference_list"));
        QVERIFY(list != nullptr);
        QVERIFY(list->maximumHeight() < QWIDGETSIZE_MAX);
        QVERIFY(list->sizeHint().height() > list->maximumHeight());  // content exceeds cap
    }

    // Confirm affordance appears only in AllowMultiple selecting; never in Single.
    void test_confirmAffordancePerMode()  // NOLINT
    {
        Gui::GeometrySelectorWidget single(Gui::GeometryQuantity::Single);
        single.selection()->startSelecting();
        QCoreApplication::processEvents();
        QVERIFY(single.findChild<QWidget*>(QStringLiteral("gsw_confirm")) == nullptr);
        QVERIFY(single.findChild<QWidget*>(QStringLiteral("gsw_cancel")) != nullptr);
        single.selection()->stopSelecting();

        Gui::GeometrySelectorWidget multi(Gui::GeometryQuantity::AllowMultiple);
        multi.selection()->startSelecting();  // 0 committed → inline chrome
        QCoreApplication::processEvents();
        QVERIFY(multi.findChild<QWidget*>(QStringLiteral("gsw_confirm")) != nullptr);
        QVERIFY(multi.findChild<QWidget*>(QStringLiteral("gsw_cancel")) != nullptr);
        multi.selection()->stopSelecting();
    }

    // ≥2 committed references while selecting produce the overlay with Done + Cancel.
    void test_overlayChromeForMultiReselect()  // NOLINT
    {
        Gui::GeometrySelectorWidget multi(Gui::GeometryQuantity::AllowMultiple);
        multi.selection()->setReferences(
            {{.object = m_object, .subName = "Edge1"}, {.object = m_object, .subName = "Edge2"}}
        );
        multi.selection()->startSelecting();
        QCoreApplication::processEvents();
        QVERIFY(multi.findChild<QWidget*>(QStringLiteral("gsw_overlay")) != nullptr);
        QVERIFY(multi.findChild<QWidget*>(QStringLiteral("gsw_done")) != nullptr);
        QVERIFY(multi.findChild<QWidget*>(QStringLiteral("gsw_cancel")) != nullptr);
        multi.selection()->stopSelecting();
    }

private:
    std::string m_docName;
    App::Document* m_doc = nullptr;
    App::DocumentObject* m_object = nullptr;
};

QTEST_MAIN(TestGeometrySelectorWidget)

#include "GeometrySelectorWidget.moc"
