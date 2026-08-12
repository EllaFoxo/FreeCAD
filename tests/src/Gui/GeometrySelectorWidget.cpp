// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QAbstractItemModel>
#include <QEvent>
#include <QLayout>
#include <QListView>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QVariant>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Parameter.h>

#include <Gui/GeometrySelectorPopup.h>
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
        QCOMPARE(single.visualState(), VS::Selecting);
        single.selection()->stopSelecting();

        Gui::GeometrySelectorWidget multi(Gui::GeometryQuantity::AllowMultiple);
        multi.selection()->setReferences(
            {{.object = m_object, .subName = "Edge1"}, {.object = m_object, .subName = "Edge2"}}
        );
        QCOMPARE(multi.visualState(), VS::ReferenceList);
        multi.selection()->startSelecting();
        QCOMPARE(multi.visualState(), VS::Selecting);  // any committed count → one selecting state
        multi.selection()->stopSelecting();

        multi.selection()->setReferences({{.object = m_object, .subName = "Edge1"}});
        multi.selection()->startSelecting();
        QCOMPARE(multi.visualState(), VS::Selecting);
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

    // Selecting with committed references produces the dimming overlay with Done + Cancel.
    void test_overlayChromeForMultiReselect()  // NOLINT
    {
        Gui::GeometrySelectorWidget multi(Gui::GeometryQuantity::AllowMultiple);
        multi.selection()->setReferences(
            {{.object = m_object, .subName = "Edge1"}, {.object = m_object, .subName = "Edge2"}}
        );
        multi.selection()->startSelecting();
        QCoreApplication::processEvents();
        QVERIFY(multi.findChild<QWidget*>(QStringLiteral("gsw_overlay")) != nullptr);
        QVERIFY(multi.findChild<QWidget*>(QStringLiteral("gsw_confirm")) != nullptr);
        QVERIFY(multi.findChild<QWidget*>(QStringLiteral("gsw_cancel")) != nullptr);
        multi.selection()->stopSelecting();
    }

    // The widget names a component of its own, which chains to List — so it inherits the list
    // row and item tokens while keeping a form control's height.
    void test_usesListComponentOverride()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::AllowMultiple);
        QCOMPARE(widget.property("component").toString(), QStringLiteral("GeometrySelector"));
    }

    // Headless fallback: no inter-row gap is applied, so the reference rows abut.
    void test_defaultRowSpacingIsZeroHeadless()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::AllowMultiple);
        widget.selection()->setReferences(
            {{.object = m_object, .subName = "Edge1"}, {.object = m_object, .subName = "Edge2"}}
        );
        QCoreApplication::processEvents();
        auto* rowsContainer
            = widget.findChild<QWidget*>(QStringLiteral("gsw_reference_row"))->parentWidget();
        QVERIFY(rowsContainer != nullptr);
        QVERIFY(rowsContainer->layout() != nullptr);
        QCOMPARE(rowsContainer->layout()->spacing(), 0);
    }

    // Headless fallback: the outer frame inset carries no extra container padding, so the
    // content margins equal the frame thickness on every side.
    void test_defaultContainerPaddingIsFrameOnlyHeadless()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        const QMargins margins = widget.layout()->contentsMargins();
        QCOMPARE(margins.left(), margins.right());
        QCOMPARE(margins.top(), margins.bottom());
        QCOMPARE(margins.left(), margins.top());
    }

    // GeometrySelectorOption::fromReference derives the label from the object even headless.
    void test_optionFromReferenceDerivesLabel()  // NOLINT
    {
        const Gui::GeometrySelectorOption option = Gui::GeometrySelectorOption::fromReference(
            {.object = m_object, .subName = "Edge1"}
        );
        // Label is "Object.Sub" using the object's Label ("TestObj") and the subelement.
        QCOMPARE(option.label, QStringLiteral("TestObj.Edge1"));
        QCOMPARE(static_cast<int>(option.references.size()), 1);
        QCOMPARE(option.references.front().object, m_object);
        QCOMPARE(option.references.front().subName, std::string("Edge1"));
    }

    // A whole-object reference (empty subName) yields just the object label.
    void test_optionFromReferenceWholeObjectLabel()  // NOLINT
    {
        const Gui::GeometrySelectorOption option = Gui::GeometrySelectorOption::fromReference(
            {.object = m_object, .subName = ""}
        );
        QCOMPARE(option.label, QStringLiteral("TestObj"));
    }

    // fromReferences stores the whole group as one option; label comes from the first reference.
    void test_optionFromReferencesGroups()  // NOLINT
    {
        std::vector<Gui::GeometryReference> references = {
            {.object = m_object, .subName = "Edge1"},
            {.object = m_object, .subName = "Edge2"},
        };
        const Gui::GeometrySelectorOption option = Gui::GeometrySelectorOption::fromReferences(
            references
        );
        QCOMPARE(static_cast<int>(option.references.size()), 2);
        QCOMPARE(option.label, QStringLiteral("TestObj.Edge1"));
    }

    // The managed Custom entry has a non-empty label and no references.
    void test_optionCustomEntry()  // NOLINT
    {
        const Gui::GeometrySelectorOption custom = Gui::GeometrySelectorOption::customEntry();
        QVERIFY(!custom.label.isEmpty());
        QVERIFY(custom.references.empty());
    }

    // Setting options enters combo mode.
    void test_comboModeActiveWhenOptionsSet()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        QVERIFY(!widget.isComboMode());
        widget.setOptions(
            {Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"})}
        );
        QVERIFY(widget.isComboMode());
    }

    // Choosing a predefined option applies its references and emits currentIndexChanged.
    void test_setCurrentIndexAppliesReferences()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions({
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"}),
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge2"}),
        });
        QSignalSpy spy(&widget, &Gui::GeometrySelectorWidget::currentIndexChanged);
        widget.setCurrentIndex(1);
        QCOMPARE(widget.currentIndex(), 1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 1);
        QCOMPARE(static_cast<int>(widget.selection()->references().size()), 1);
        QCOMPARE(widget.selection()->references().front().subName, std::string("Edge2"));
    }

    // A logical option (no references) clears the selection but sets the index.
    void test_logicalOptionClearsReferences()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        Gui::GeometrySelectorOption logical;
        logical.label = QStringLiteral("Document origin");
        logical.userData = QStringLiteral("doc-origin");
        widget.setOptions({
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"}),
            logical,
        });
        widget.setCurrentIndex(1);
        QCOMPARE(widget.currentIndex(), 1);
        QVERIFY(widget.selection()->references().empty());
        QCOMPARE(widget.currentData().toString(), QStringLiteral("doc-origin"));
        QCOMPARE(widget.currentText(), QStringLiteral("Document origin"));
    }

    // Reverse match: references equal to an option's references select that option.
    void test_reverseMatchSelectsOption()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions({
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"}),
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge2"}),
        });
        widget.selection()->setReferences({{.object = m_object, .subName = "Edge2"}});
        QCOMPARE(widget.currentIndex(), 1);
    }

    // addOption reconciles: an option added after references are present reverse-matches.
    void test_addOptionReconcilesCurrentIndex()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.selection()->setReferences({{.object = m_object, .subName = "Edge2"}});
        widget.addOption(
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"})
        );
        widget.addOption(
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge2"})
        );
        // The second option matches the existing references ⇒ reverse-match selects index 1.
        QCOMPARE(widget.currentIndex(), 1);
    }

    // setCurrentIndex ignores an out-of-range index (no state change, no signal).
    void test_setCurrentIndexOutOfRangeIgnored()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions({
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"}),
        });
        QSignalSpy spy(&widget, &Gui::GeometrySelectorWidget::currentIndexChanged);
        widget.setCurrentIndex(99);
        QCOMPARE(widget.currentIndex(), -1);
        QCOMPARE(spy.count(), 0);
    }

    // Reverse match: non-matching references fall to the Custom index when Custom is enabled.
    void test_reverseMatchFallsToCustom()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setAllowCustom(true);
        widget.setOptions({
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"}),
        });
        widget.selection()->setReferences({{.object = m_object, .subName = "Face9"}});
        // Custom index == number of predefined options (1 here).
        QCOMPARE(widget.currentIndex(), 1);
        QVERIFY(widget.currentOption() == nullptr);
    }

    // Custom disabled ⇒ no Custom index; a non-matching load leaves the index unchanged.
    void test_reverseMatchNoCustomLeavesIndex()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions({
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"}),
        });
        widget.setCurrentIndex(0);
        widget.selection()->setReferences({{.object = m_object, .subName = "Face9"}});
        QCOMPARE(widget.currentIndex(), 0);  // unchanged: no Custom to fall to
    }

    // Choosing Custom starts a free-pick session.
    void test_customIndexStartsSelecting()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setAllowCustom(true);
        widget.setOptions({
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"}),
        });
        widget.setCurrentIndex(1);  // the Custom index
        QVERIFY(widget.selection()->isSelecting());
        QVERIFY(widget.currentOption() == nullptr);
        widget.selection()->stopSelecting();
    }

    // With Custom disabled, setCurrentIndex(-1) (the "nothing current" sentinel) must NOT
    // start a selection session: customIndex() is -1 when Custom is off, so the guard must
    // also check m_allowCustom.
    void test_setCurrentIndexMinusOneNoCustomDoesNotSelect()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions({
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"}),
        });
        widget.setCurrentIndex(0);
        widget.setCurrentIndex(-1);
        QCOMPARE(widget.currentIndex(), -1);
        QVERIFY(!widget.selection()->isSelecting());
    }

    // Custom disabled + nothing selected (currentIndex == -1): currentText/currentData must be
    // empty, not the managed Custom entry (customIndex() is -1 when Custom is off).
    void test_currentTextEmptyWhenNothingSelectedNoCustom()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions({
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"}),
        });
        QCOMPARE(widget.currentIndex(), -1);
        QVERIFY(widget.currentText().isEmpty());
        QVERIFY(!widget.currentData().isValid());
        QVERIFY(widget.currentOption() == nullptr);
    }

    // An empty external load leaves currentIndex unchanged.
    void test_emptyLoadLeavesIndexUnchanged()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions({
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"}),
        });
        widget.setCurrentIndex(0);
        widget.selection()->setReferences({});
        QCOMPARE(widget.currentIndex(), 0);
    }

    // The popup builds one row per option, plus a Custom row when enabled — with a rule between
    // the two groups. Rows are model rows now, not child widgets, so the count is read off the
    // view's model.
    void test_popupRowCount()  // NOLINT
    {
        std::vector<Gui::GeometrySelectorOption> options = {
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"}),
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge2"}),
        };

        Gui::GeometrySelectorPopup closed(options, {}, /*allowCustom=*/false, /*currentIndex=*/-1, nullptr);
        QCOMPARE(closed.optionCount(), 2);
        auto* closedView = closed.findChild<QListView*>();
        QVERIFY(closedView != nullptr);
        QCOMPARE(closedView->model()->rowCount(), 2);

        Gui::GeometrySelectorPopup
            withCustom(options, {}, /*allowCustom=*/true, /*currentIndex=*/-1, nullptr);
        QCOMPARE(withCustom.optionCount(), 3);
        auto* customView = withCustom.findChild<QListView*>();
        QVERIFY(customView != nullptr);
        QCOMPARE(customView->model()->rowCount(), 4);  // 2 options + rule + custom
        QCOMPARE(
            customView->model()->index(3, 0).data(Qt::DisplayRole).toString(),
            Gui::GeometrySelectorOption::customEntry().label
        );
    }

    // Navigation and activation come from the view, so the popup must still answer the keys a
    // dropdown answers — with no Gui::Application, and therefore no FreeCADStyle, at all.
    void test_popupKeyboardNavigatesAndActivates()  // NOLINT
    {
        std::vector<Gui::GeometrySelectorOption> options = {
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"}),
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge2"}),
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge3"}),
        };
        Gui::GeometrySelectorPopup popup(options, {}, /*allowCustom=*/false, /*currentIndex=*/0, nullptr);
        popup.show();
        QVERIFY(QTest::qWaitForWindowExposed(&popup));

        auto* view = popup.findChild<QListView*>();
        QVERIFY(view != nullptr);
        QCOMPARE(view->currentIndex().row(), 0);

        QSignalSpy spy(&popup, &Gui::GeometrySelectorPopup::optionActivated);

        // Routed through the QWindow, not the widget directly, so real focus dispatch
        // participates — QTest::keyClick(QWidget*, ...) delivers straight to the named widget
        // and never consults which widget actually has focus.
        QTest::keyClick(popup.windowHandle(), Qt::Key_Down);
        QCOMPARE(view->currentIndex().row(), 1);

        QTest::keyClick(popup.windowHandle(), Qt::Key_Return);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 1);
    }

    // Activating an index emits optionActivated with that index (Custom == options.size()).
    void test_popupActivateEmits()  // NOLINT
    {
        std::vector<Gui::GeometrySelectorOption> options = {
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"}),
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge2"}),
        };
        Gui::GeometrySelectorPopup popup(options, {}, /*allowCustom=*/true, /*currentIndex=*/0, nullptr);
        QSignalSpy spy(&popup, &Gui::GeometrySelectorPopup::optionActivated);

        popup.activateIndex(1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 1);

        popup.activateIndex(2);  // the Custom index
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 2);
    }

    // Out-of-range activation is ignored.
    void test_popupActivateOutOfRangeIgnored()  // NOLINT
    {
        std::vector<Gui::GeometrySelectorOption> options = {
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"}),
        };
        Gui::GeometrySelectorPopup popup(options, {}, /*allowCustom=*/false, /*currentIndex=*/-1, nullptr);
        QSignalSpy spy(&popup, &Gui::GeometrySelectorPopup::optionActivated);
        popup.activateIndex(5);
        popup.activateIndex(-1);
        QCOMPARE(spy.count(), 0);
    }

    // Combo mode reserves extra room on the right for the chevron; free-pick mode does not.
    // A non-empty options list turns the widget into a combo; an empty list restores free-pick.
    // The dropdown frame/arrow and its right-side reserve are the style's job (CC_ComboBox), so
    // there is nothing pixel-wise to assert in the headless harness — only the mode transition.
    void test_setOptionsTogglesComboMode()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        QVERIFY(!widget.isComboMode());

        widget.setOptions(
            {Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"})}
        );
        QVERIFY(widget.isComboMode());

        widget.setOptions({});
        QVERIFY(!widget.isComboMode());
    }

    // In combo mode a reference row is display-only: no remove (trash) button.
    void test_comboModeRowsHaveNoRemoveButton()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::AllowMultiple);
        widget.setAllowCustom(true);
        widget.setOptions(
            {Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"})}
        );
        // A non-matching load ⇒ Custom index ⇒ the references render as rows.
        widget.selection()->setReferences({{.object = m_object, .subName = "Face9"}});
        QCoreApplication::processEvents();
        auto* row = widget.findChild<QWidget*>(QStringLiteral("gsw_reference_row"));
        QVERIFY(row != nullptr);
        QCOMPARE(row->findChildren<QToolButton*>().size(), 0);
    }

    // Free-pick mode keeps the per-row remove button.
    void test_freePickModeRowsKeepRemoveButton()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::AllowMultiple);
        widget.selection()->setReferences({{.object = m_object, .subName = "Edge1"}});
        QCoreApplication::processEvents();
        auto* row = widget.findChild<QWidget*>(QStringLiteral("gsw_reference_row"));
        QVERIFY(row != nullptr);
        QVERIFY(row->findChildren<QToolButton*>().size() >= 1);
    }

    // Activating the control in combo mode opens the options popup rather than selecting.
    void test_comboActivationOpensPopup()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setAllowCustom(true);
        widget.setOptions(
            {Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"})}
        );
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));

        // Empty state ⇒ the prompt handles the click; click it to activate.
        auto* prompt = widget.findChild<QWidget*>(QStringLiteral("gsw_prompt"));
        QVERIFY(prompt != nullptr);
        QTest::mouseClick(prompt, Qt::LeftButton);
        QCoreApplication::processEvents();

        QVERIFY(widget.findChild<Gui::GeometrySelectorPopup*>() != nullptr);
        QVERIFY(!widget.selection()->isSelecting());  // did NOT start a free pick
        widget.hide();
    }

    // A dismissed popup (here via close(), same path as Escape/click-away) is destroyed, not
    // leaked — WA_DeleteOnClose frees it rather than leaving it parented to the widget.
    void test_popupDismissedIsDestroyed()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setAllowCustom(true);
        widget.setOptions({
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"}),
        });
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        auto* prompt = widget.findChild<QWidget*>(QStringLiteral("gsw_prompt"));
        QVERIFY(prompt != nullptr);
        QTest::mouseClick(prompt, Qt::LeftButton);
        QCoreApplication::processEvents();
        auto* popup = widget.findChild<Gui::GeometrySelectorPopup*>();
        QVERIFY(popup != nullptr);

        popup->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(widget.findChild<Gui::GeometrySelectorPopup*>(), nullptr);
        widget.hide();
    }

    // A current predefined logical option (empty references) is the combo-box Option state — it
    // must not fall through to the empty "Select geometry" prompt. The state is painted natively
    // (no child row); the reported label is the option's.
    void test_comboCurrentLogicalOptionRendersAsOptionState()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions({
            {.icon = {}, .label = QStringLiteral("Object origin"), .references = {}, .userData = {}},
        });
        widget.setCurrentIndex(0);

        QCOMPARE(widget.visualState(), Gui::GeometrySelectorWidget::VisualState::Option);
        QCOMPARE(widget.currentText(), QStringLiteral("Object origin"));
        QVERIFY(widget.findChild<QWidget*>(QStringLiteral("gsw_prompt")) == nullptr);
    }

    // The core forwards raw pick events only while a session is active, so a
    // consumer (the transform dialog) can read the picked point / link path the
    // {object, subName} reference model drops.
    void test_pickSelectionChangedForwardsRawEventWhileSelecting()  // NOLINT
    {
        struct PickProbe: Gui::GeometrySelection
        {
            using Gui::GeometrySelection::GeometrySelection;
            using Gui::GeometrySelection::onSelectionChanged;
        };

        PickProbe selection(Gui::GeometryQuantity::Single);

        int emitted = 0;
        Gui::SelectionChanges::MsgType seenType {};
        std::string seenObject;
        QObject::connect(
            &selection,
            &Gui::GeometrySelection::pickSelectionChanged,
            [&](const Gui::SelectionChanges& change) {
                ++emitted;
                seenType = change.Type;
                seenObject = change.pObjectName ? change.pObjectName : "";
            }
        );

        const Gui::SelectionChanges preselect(
            Gui::SelectionChanges::SetPreselect,
            m_docName,
            std::string("TestObj"),
            std::string("Edge1")
        );

        // Not selecting: no forward.
        selection.onSelectionChanged(preselect);
        QCOMPARE(emitted, 0);

        // Selecting: forwards the untouched event.
        selection.startSelecting();
        selection.onSelectionChanged(preselect);
        QCOMPARE(emitted, 1);
        QCOMPARE(seenType, Gui::SelectionChanges::SetPreselect);
        QCOMPARE(seenObject, std::string("TestObj"));
        selection.stopSelecting();
    }

    // A finished pick is remembered, and lands at the front of the group — the first index after
    // the predefined options.
    void test_aFinishedCustomPickIsRemembered()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions(logicalOptions(2));
        widget.setAllowCustom(true);

        pick(widget, "Edge1");

        QCOMPARE(widget.historySize(), 1);
        QCOMPARE(widget.currentIndex(), 2);
        QCOMPARE(widget.currentText(), labelOf("Edge1"));
    }

    // A cancelled session restores what it started from, so there is nothing new to remember.
    void test_aCancelledPickIsNotRemembered()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions(logicalOptions(2));
        widget.setAllowCustom(true);

        widget.selection()->startSelecting();
        widget.selection()->setReferences({{.object = m_object, .subName = "Edge1"}});
        widget.selection()->cancelSelecting();
        QCoreApplication::processEvents();

        QCOMPARE(widget.historySize(), 0);
    }

    // A cancel that starts and ends on an existing, unlisted selection (not merely on nothing)
    // must be recognised the same way: by comparing the session's end state to its start, not by
    // asking whether either happens to be empty.
    void test_aCancelledPickFromAnExistingSelectionIsNotRemembered()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions(logicalOptions(2));
        widget.setAllowCustom(true);
        widget.selection()->setReferences({{.object = m_object, .subName = "Edge1"}});
        QCoreApplication::processEvents();

        widget.selection()->startSelecting();
        widget.selection()->setReferences({{.object = m_object, .subName = "Edge2"}});
        widget.selection()->cancelSelecting();
        QCoreApplication::processEvents();

        QCOMPARE(widget.historySize(), 0);
    }

    // A pick a predefined option already stands for is on screen above; remembering it would
    // offer the same choice twice.
    void test_aPickMatchingAPredefinedOptionIsNotRemembered()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions(
            {Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"})}
        );
        widget.setAllowCustom(true);

        pick(widget, "Edge1");

        QCOMPARE(widget.historySize(), 0);
    }

    // Picking the same thing twice moves its entry rather than adding a second one.
    void test_repeatingAPickMovesItToTheFront()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions(logicalOptions(1));
        widget.setAllowCustom(true);

        pick(widget, "Edge1");
        pick(widget, "Edge2");
        pick(widget, "Edge1");

        QCOMPARE(widget.historySize(), 2);
        widget.setCurrentIndex(1);
        QCOMPARE(widget.currentText(), labelOf("Edge1"));
        widget.setCurrentIndex(2);
        QCOMPARE(widget.currentText(), labelOf("Edge2"));
    }

    // The oldest entry falls off the end.
    void test_historyIsTruncatedToItsLength()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions(logicalOptions(1));
        widget.setAllowCustom(true);
        widget.setHistoryLength(2);

        pick(widget, "Edge1");
        pick(widget, "Edge2");
        pick(widget, "Edge3");

        QCOMPARE(widget.historySize(), 2);
        widget.setCurrentIndex(1);
        QCOMPARE(widget.currentText(), labelOf("Edge3"));
        widget.setCurrentIndex(2);
        QCOMPARE(widget.currentText(), labelOf("Edge2"));
    }

    // Zero disables the group outright: nothing is remembered and Custom keeps its old index.
    void test_aZeroLengthDisablesHistory()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions(logicalOptions(2));
        widget.setAllowCustom(true);
        widget.setHistoryLength(0);

        pick(widget, "Edge1");

        QCOMPARE(widget.historySize(), 0);
        QCOMPARE(widget.currentIndex(), 2);  // the Custom index, unmoved
    }

    // A remembered pick is not a predefined option, and a caller that reads currentOption() to
    // decide a mode must see that: TaskTransform maps a null option to its Custom mode, and an
    // entry with empty userData would otherwise decode as the first enumerator.
    void test_currentOptionIsNullAtAHistoryIndex()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions(logicalOptions(2));
        widget.setAllowCustom(true);

        pick(widget, "Edge1");

        QCOMPARE(widget.currentIndex(), 2);
        QVERIFY(widget.currentOption() == nullptr);
        QVERIFY(!widget.currentData().isValid());
    }

    // Choosing a remembered pick applies its references without reopening a picking session.
    void test_choosingARememberedPickAppliesItWithoutSelecting()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions(logicalOptions(2));
        widget.setAllowCustom(true);
        pick(widget, "Edge1");

        widget.setCurrentIndex(0);
        QCOMPARE(widget.selection()->references().size(), std::size_t {0});

        widget.setCurrentIndex(2);

        QVERIFY(!widget.selection()->isSelecting());
        QCOMPARE(widget.selection()->references().size(), std::size_t {1});
        QCOMPARE(widget.selection()->references().front().subName, std::string("Edge1"));
    }

    // Deleting the object a remembered pick names drops the entry, so building the dropdown never
    // reaches through a dangling pointer.
    void test_deletingAnObjectForgetsThePicksThatNameIt()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions(logicalOptions(2));
        widget.setAllowCustom(true);

        App::DocumentObject* second = m_doc->addObject("App::FeatureTest", "OtherObj");
        widget.selection()->startSelecting();
        widget.selection()->setReferences({{.object = second, .subName = "Edge1"}});
        widget.selection()->stopSelecting();
        QCoreApplication::processEvents();
        QCOMPARE(widget.historySize(), 1);

        m_doc->removeObject("OtherObj");
        QCoreApplication::processEvents();

        QCOMPARE(widget.historySize(), 0);
        QVERIFY(widget.currentIndex() <= 2);
    }

    // Deleting the object behind one history entry can shift a different, still-selected entry
    // into its place; the current index must follow that shift by re-deriving from the
    // unaffected references, not merely fall back to Custom because the old index happens to
    // still be in range.
    void test_deletingAnObjectShiftsTheSurvivingSelectionsIndex()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions(logicalOptions(2));
        widget.setAllowCustom(true);

        App::DocumentObject* second = m_doc->addObject("App::FeatureTest", "SecondObj");
        App::DocumentObject* third = m_doc->addObject("App::FeatureTest", "ThirdObj");

        widget.selection()->startSelecting();
        widget.selection()->setReferences({{.object = second, .subName = "Edge1"}});
        widget.selection()->stopSelecting();
        QCoreApplication::processEvents();

        widget.selection()->startSelecting();
        widget.selection()->setReferences({{.object = third, .subName = "Edge1"}});
        widget.selection()->stopSelecting();
        QCoreApplication::processEvents();

        // history is now [third, second]; select the older, surviving entry explicitly.
        widget.setCurrentIndex(3);
        QCOMPARE(widget.selection()->references().front().object, second);

        m_doc->removeObject("ThirdObj");
        QCoreApplication::processEvents();

        QCOMPARE(widget.historySize(), 1);
        QCOMPARE(widget.currentIndex(), 2);  // the surviving entry moved to the front
    }

    // The length a selector starts with comes from the user's preference, so one setting reaches
    // every selector without a caller doing anything.
    void test_theLengthDefaultsToThePreference()  // NOLINT
    {
        ParameterGrp::handle group = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Selection"
        );
        const long previous = group->GetInt("GeometrySelectorHistoryLength", 5);
        const auto guard = qScopeGuard([group, previous] {
            group->SetInt("GeometrySelectorHistoryLength", previous);
        });
        group->SetInt("GeometrySelectorHistoryLength", 2);

        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);

        QCOMPARE(widget.historyLength(), 2);
    }

    // Shrinking the length below the current index cannot leave that index pointing past the end.
    void test_shrinkingTheLengthPullsTheIndexBack()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        widget.setOptions(logicalOptions(1));
        widget.setAllowCustom(true);
        widget.setHistoryLength(3);
        pick(widget, "Edge1");
        pick(widget, "Edge2");
        pick(widget, "Edge3");
        widget.setCurrentIndex(3);  // the oldest remembered pick

        widget.setHistoryLength(1);

        QCOMPARE(widget.historySize(), 1);
        QVERIFY(widget.currentIndex() <= widget.historySize() + 1);
        // Pinned exactly: pulled all the way back to the Custom entry, not merely to some index
        // that happens to still be in range.
        QCOMPARE(widget.currentIndex(), widget.historySize() + 1);
    }

private:
    // Predefined options that carry no geometry, the way TaskTransform's do: their meaning is in
    // userData, so none of them can ever match a picked reference set.
    std::vector<Gui::GeometrySelectorOption> logicalOptions(int count) const
    {
        std::vector<Gui::GeometrySelectorOption> options;
        for (int index = 0; index < count; ++index) {
            options.push_back({
                .icon = {},
                .label = QStringLiteral("Logical %1").arg(index),
                .references = {},
                .userData = index,
            });
        }
        return options;
    }

    // One complete pick: a session that ends holding @p subName.
    void pick(Gui::GeometrySelectorWidget& widget, const char* subName) const
    {
        widget.selection()->startSelecting();
        widget.selection()->setReferences({{.object = m_object, .subName = subName}});
        widget.selection()->stopSelecting();
        QCoreApplication::processEvents();
    }

    // The label the widget derives for a reference, asked for the same way it derives it, so the
    // assertion pins the entry rather than a hardcoded naming scheme.
    QString labelOf(const char* subName) const
    {
        return Gui::GeometrySelectorOption::fromReferences(
                   {{.object = m_object, .subName = subName}}
        ).label;
    }

    std::string m_docName;
    App::Document* m_doc = nullptr;
    App::DocumentObject* m_object = nullptr;
};

QTEST_MAIN(TestGeometrySelectorWidget)

#include "GeometrySelectorWidget.moc"
