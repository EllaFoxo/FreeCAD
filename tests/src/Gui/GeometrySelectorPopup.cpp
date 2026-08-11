// SPDX-License-Identifier: LGPL-2.1-or-later

#include <vector>

#include <Inventor/SoDB.h>

#include <QListView>
#include <QScrollBar>
#include <QTest>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

#include <Gui/GeometrySelectorPopup.h>
#include <Gui/GeometrySelectorWidget.h>

#include "DropdownStyleFixture.h"

// The popup resolves the same DropdownList tokens a combo box's popup does, so the fixture's
// theme is the whole vocabulary these tests assert in: #ff0000 selected, #0000ff hovered,
// #101010 surface, #00ff00 surface edge.
class TestGeometrySelectorPopup: public QObject, private DropdownStyleFixture
{
    Q_OBJECT

private Q_SLOTS:

    // With the fixture's live Gui::Application, App::GetApplication().newDocument() below also
    // builds a Gui::Document, and that builds a Thumbnail's SoOrthographicCamera — the first
    // Coin node this binary constructs. SoDB::init() is idempotent (mirrors InventorBuilder.cpp),
    // so doing it once here is what every other Coin-touching test does.
    void initTestCase()
    {
        SoDB::init();
    }

    void cleanupTestCase()
    {
        SoDB::finish();
    }

    void init()
    {
        App::DocumentInitFlags flags;
        flags.createView = false;
        m_docName = App::GetApplication().getUniqueDocumentName("gsp_test");
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

    // The chosen entry is marked the way every other dropdown marks it — a background, not a
    // glyph — and no other row is.
    void test_theChosenEntryIsFilledFromTheDropdownRowToken()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup popup(optionsOf(4), /*allowCustom=*/false, /*currentIndex=*/2, nullptr);
        showPopup(popup);

        QListView* view = viewOf(popup);
        const QImage canvas = renderOf(*view->viewport());

        QCOMPARE(canvas.pixelColor(rowCentre(*view, 2)), QColor(0xff, 0x00, 0x00));
        QVERIFY(canvas.pixelColor(rowCentre(*view, 3)) != QColor(0xff, 0x00, 0x00));
    }

    // Two rows can carry a highlight at once: the pointer's, and the entry the control holds.
    void test_aHoveredRowDoesNotStealTheChosenEntrysMark()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup popup(optionsOf(4), /*allowCustom=*/false, /*currentIndex=*/2, nullptr);
        showPopup(popup);

        QListView* view = viewOf(popup);
        // The precondition hover depends on. Stated rather than assumed — though note it holds
        // even without GeometrySelectorPopup's own explicit setMouseTracking(true) call: any
        // live FreeCADStyle sets Qt::WA_MouseTracking on every QAbstractItemView it polishes,
        // and QAbstractScrollArea::event() forwards that to the viewport on its own
        // (QEvent::MouseTrackingChange, qabstractscrollarea.cpp). So this assertion cannot by
        // itself catch that line going missing; it only documents that the mechanism the row
        // colours below depend on is in place.
        QVERIFY(view->viewport()->hasMouseTracking());
        hover(popup, *view, 0);

        const QImage canvas = renderOf(*view->viewport());

        QCOMPARE(canvas.pixelColor(rowCentre(*view, 2)), QColor(0xff, 0x00, 0x00));
        QCOMPARE(canvas.pixelColor(rowCentre(*view, 0)), QColor(0x00, 0x00, 0xff));
    }

    // The arrow keys move a cursor, not the selection. Without the chosen-row tag the mark
    // would follow the keys and the popup would forget which entry it holds.
    void test_arrowingDoesNotMoveTheChosenEntrysMark()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup popup(optionsOf(4), /*allowCustom=*/false, /*currentIndex=*/2, nullptr);
        showPopup(popup);

        QListView* view = viewOf(popup);
        // The QWidget overload of keyClick calls qt_sendSpontaneousEvent directly on the named
        // widget and never consults focus, so it would pass whether or not the popup's focus
        // proxy actually routed the key to the view. Going through the window is what a real key
        // press does, and is the only form that can catch a focus-routing regression.
        QTest::keyClick(popup.windowHandle(), Qt::Key_Down);
        QCOMPARE(view->currentIndex().row(), 3);

        const QImage canvas = renderOf(*view->viewport());

        QCOMPARE(canvas.pixelColor(rowCentre(*view, 2)), QColor(0xff, 0x00, 0x00));
        QCOMPARE(canvas.pixelColor(rowCentre(*view, 3)), QColor(0x00, 0x00, 0xff));
    }

    // The popup has a surface of its own — the frame around the list paints it, from the same
    // tokens a combo popup's frame uses.
    void test_theSurfaceIsDrawnFromTheDropdownSurfaceTokens()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup popup(optionsOf(3), /*allowCustom=*/false, /*currentIndex=*/-1, nullptr);
        showPopup(popup);

        const QImage canvas = renderOf(popup);

        // The edge, and the fill just inside the border and padding but above the first row.
        QCOMPARE(canvas.pixelColor(0, 0), surfaceBorderColor);
        QCOMPARE(canvas.pixelColor(containerBorder, containerBorder), surfaceColor);
    }

    // More rows than the cap allows: the popup stops at the cap, scrolls rather than growing
    // past the screen, and ends on a row edge — exactly as a combo popup does.
    void test_aLongPopupIsCappedAndEndsOnARowEdge()  // NOLINT
    {
        const auto capGuard = overrideToken("DropdownListMaxHeight", "80px");

        installFreshPopupStyle();

        // A parent is what FreeCADStyle::correctComboPopupPlacement needs even to begin — it
        // bails out before the row-edge trim runs at all if the container has none. A combo
        // box's popup always has one (the combo box itself); GeometrySelectorWidget passes
        // itself as the popup's parent too, so an anchor here matches how the popup is ever
        // really shown, rather than testing a parentless shape nothing produces.
        QWidget anchor;
        anchor.move(200, 200);
        anchor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&anchor));

        Gui::GeometrySelectorPopup popup(optionsOf(40), /*allowCustom=*/false, /*currentIndex=*/0, &anchor);
        showPopup(popup);

        QListView* view = viewOf(popup);
        QVERIFY(popup.height() <= 80);
        QVERIFY(view->verticalScrollBar()->maximum() > 0);

        // The cap is a pixel count, not a whole number of rows, so what it leaves over is a
        // partial row. The style trims that back off, and a scrolled popup must therefore end
        // exactly on a row edge rather than showing a sliver of the next one.
        QCOMPARE(view->viewport()->height() % view->sizeHintForRow(0), 0);
    }

    // "current" places the chosen row on the control; "below" meets its bottom edge. Both are
    // the style's placement correction, reached because the popup is a tagged container.
    void test_placementFollowsTheDropdownToken()  // NOLINT
    {
        {
            const auto modeGuard = overrideToken("DropdownListPlacement", "below");
            installFreshPopupStyle();

            // Built after the style install, not before: GeometrySelectorWidget hands the
            // FreeCADStyle singleton to its child buttons via setStyle(), which QWidget keeps
            // as a raw pointer, so an anchor built while installFreshPopupStyle() is about to
            // delete the previous singleton would be handed a dangling one.
            Gui::GeometrySelectorWidget anchor(Gui::GeometryQuantity::Single);
            anchor.move(200, 200);
            anchor.show();
            QVERIFY(QTest::qWaitForWindowExposed(&anchor));

            auto* popup = new Gui::GeometrySelectorPopup(optionsOf(3), false, 1, &anchor);
            popup->resize(anchor.width(), popup->sizeHint().height());
            popup->move(anchor.mapToGlobal(QPoint(0, anchor.height())));
            popup->show();
            QCoreApplication::processEvents();  // the correction is deferred by a zero timer

            QCOMPARE(
                popup->mapToGlobal(QPoint {}).y(),
                anchor.mapToGlobal(QPoint {}).y() + anchor.height()
            );
            popup->close();
        }

        {
            const auto modeGuard = overrideToken("DropdownListPlacement", "current");
            installFreshPopupStyle();

            Gui::GeometrySelectorWidget anchor(Gui::GeometryQuantity::Single);
            anchor.move(200, 200);
            anchor.show();
            QVERIFY(QTest::qWaitForWindowExposed(&anchor));

            auto* popup = new Gui::GeometrySelectorPopup(optionsOf(3), false, 1, &anchor);
            popup->resize(anchor.width(), popup->sizeHint().height());
            popup->move(anchor.mapToGlobal(QPoint(0, anchor.height())));
            popup->show();
            QCoreApplication::processEvents();

            QListView* view = viewOf(*popup);
            const QPoint rowTopLeft = view->visualRect(view->model()->index(1, 0)).topLeft();
            QCOMPARE(view->viewport()->mapToGlobal(rowTopLeft).y(), anchor.mapToGlobal(QPoint {}).y());
            popup->close();
        }
    }

private:
    // GeometrySelectorPopup::adoptAsDropdown() constrains itself through
    // Application::Instance->freeCADStyle() directly, not through whatever style
    // installFreshApplicationStyle() puts on qApp — see GeometrySelectorPopup.cpp. That
    // singleton outlives every test in this binary, so its own box-geometry and token caches
    // would otherwise pin a popup to whichever token values were in force the first time any
    // test built one. Deleting it makes Application::freeCADStyle() rebuild a fresh instance
    // lazily on the popup's next access, giving it the same per-test freshness the ambient
    // style gets.
    static void installFreshPopupStyle()
    {
        delete Gui::Application::Instance->freeCADStyle();
        DropdownStyleFixture::installFreshApplicationStyle();
    }

    // Distinct labels so a row picked by index cannot be confused with its neighbour.
    std::vector<Gui::GeometrySelectorOption> optionsOf(int count) const
    {
        std::vector<Gui::GeometrySelectorOption> options;
        options.reserve(count);
        for (int index = 0; index < count; ++index) {
            options.push_back(
                Gui::GeometrySelectorOption::fromReference(
                    {.object = m_object, .subName = "Edge" + std::to_string(index + 1)}
                )
            );
        }
        return options;
    }

    static QListView* viewOf(Gui::GeometrySelectorPopup& popup)
    {
        auto* view = popup.findChild<QListView*>();
        Q_ASSERT(view != nullptr);
        return view;
    }

    static QPoint rowCentre(const QListView& view, int row)
    {
        return view.visualRect(view.model()->index(row, 0)).center();
    }

    static void showPopup(Gui::GeometrySelectorPopup& popup)
    {
        popup.resize(200, popup.sizeHint().height());
        popup.show();
        QVERIFY(QTest::qWaitForWindowExposed(&popup));
        QCoreApplication::processEvents();  // the placement correction is deferred by a zero timer
    }

    // The pointer arriving over a row. Routed through the popup's QWindow rather than the
    // QWidget overload of QTest::mouseMove, which for a widget with no button held warps the
    // real desktop pointer via QCursor::setPos: on a live display that produces no move at all
    // when the cursor already sits at that global position, and the row is then never hovered.
    // The QWindow overload has no such trap — the same reason keyClick above goes through
    // popup.windowHandle() rather than the view. Routing through the window, rather than
    // synthesizing a QMouseEvent straight at the viewport, also means the event passes through
    // Qt's own mouse-tracking gate (QApplicationPrivate::sendMouseEvent) instead of skipping it
    // outright — genuinely closer to what a real pointer move delivers, even though nothing in
    // this popup currently drops mouse tracking in a way only that gate would catch. Do not
    // simplify.
    static void hover(Gui::GeometrySelectorPopup& popup, QListView& view, int row)
    {
        const QPoint spot = view.visualRect(view.model()->index(row, 0)).center();
        const QPoint windowPos = view.viewport()->mapTo(&popup, spot);
        QTest::mouseMove(popup.windowHandle(), windowPos);
        QCoreApplication::processEvents();
    }

    std::string m_docName;
    App::Document* m_doc = nullptr;
    App::DocumentObject* m_object = nullptr;
};

QTEST_MAIN(TestGeometrySelectorPopup)

#include "GeometrySelectorPopup.moc"
