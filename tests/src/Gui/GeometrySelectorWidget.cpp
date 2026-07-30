// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QEvent>
#include <QLayout>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QVariant>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

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

    // The widget resolves the List token chain, not LineEdit.
    void test_usesListComponentOverride()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::AllowMultiple);
        QCOMPARE(widget.property("component").toString(), QStringLiteral("List"));
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

    // The popup builds one row per option, plus a Custom row when enabled.
    void test_popupRowCount()  // NOLINT
    {
        std::vector<Gui::GeometrySelectorOption> options = {
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"}),
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge2"}),
        };
        Gui::GeometrySelectorPopup closed(options, /*allowCustom=*/false, /*currentIndex=*/-1, nullptr);
        QCOMPARE(closed.optionCount(), 2);
        QCOMPARE(closed.findChildren<QWidget*>(QStringLiteral("gsw_option_row")).size(), 2);
        QVERIFY(closed.findChild<QWidget*>(QStringLiteral("gsw_option_custom")) == nullptr);

        Gui::GeometrySelectorPopup withCustom(options, /*allowCustom=*/true, /*currentIndex=*/-1, nullptr);
        QCOMPARE(withCustom.optionCount(), 3);
        QVERIFY(withCustom.findChild<QWidget*>(QStringLiteral("gsw_option_custom")) != nullptr);
    }

    // Activating an index emits optionActivated with that index (Custom == options.size()).
    void test_popupActivateEmits()  // NOLINT
    {
        std::vector<Gui::GeometrySelectorOption> options = {
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"}),
            Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge2"}),
        };
        Gui::GeometrySelectorPopup popup(options, /*allowCustom=*/true, /*currentIndex=*/0, nullptr);
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
        Gui::GeometrySelectorPopup popup(options, /*allowCustom=*/false, /*currentIndex=*/-1, nullptr);
        QSignalSpy spy(&popup, &Gui::GeometrySelectorPopup::optionActivated);
        popup.activateIndex(5);
        popup.activateIndex(-1);
        QCOMPARE(spy.count(), 0);
    }

    // Combo mode reserves extra room on the right for the chevron; free-pick mode does not.
    void test_comboModeReservesChevronMargin()  // NOLINT
    {
        Gui::GeometrySelectorWidget widget(Gui::GeometryQuantity::Single);
        const QMargins plain = widget.layout()->contentsMargins();
        QCOMPARE(plain.right(), plain.left());  // symmetric in free-pick mode (headless)

        widget.setOptions(
            {Gui::GeometrySelectorOption::fromReference({.object = m_object, .subName = "Edge1"})}
        );
        const QMargins combo = widget.layout()->contentsMargins();
        QVERIFY(combo.right() > combo.left());  // chevron strip reserved on the right
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

private:
    std::string m_docName;
    App::Document* m_doc = nullptr;
    App::DocumentObject* m_object = nullptr;
};

QTEST_MAIN(TestGeometrySelectorWidget)

#include "GeometrySelectorWidget.moc"
