// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QApplication>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTest>
#include <QWidget>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/SearchHighlightOverlay.h>
#include <Gui/StyleParameters/ParameterManager.h>
#include <Gui/StyleParameters/StyleContext.h>

namespace
{
constexpr QMargins testMargins {4, 4, 4, 4};

/// A scroll area whose content is deliberately larger than the viewport, so the content widget
/// sits at the viewport origin while the vertical scrollbar still has room to move.
struct ScrollFixture
{
    QScrollArea area;
    QWidget* content = new QWidget;

    ScrollFixture()
    {
        area.resize(200, 200);
        content->resize(400, 800);
        area.setWidget(content);
    }
};
}  // namespace

class TestSearchHighlightOverlay: public QObject
{
    Q_OBJECT

public:
    TestSearchHighlightOverlay()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        // Saturated and unmistakable, so a resolved value can only have come from these.
        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "SearchHighlightBackground", .value = "#112233"},
                    {.name = "SearchHighlightBorderColor", .value = "#445566"},
                    {.name = "SearchHighlightBorderThickness", .value = "2px"},
                    {.name = "SearchHighlightBorderRadius", .value = "3px"},
                    {.name = "SearchHighlightMargin", .value = "6px"},
                },
                {.name = "Search Highlight Fixture"}
            )
        );

        // The production paths read the widget's own style, so the application style has to be
        // the one under test.
        QApplication::setStyle(new Gui::FreeCADStyle);
    }

private:
    static Gui::FreeCADStyle* style()
    {
        return qobject_cast<Gui::FreeCADStyle*>(QApplication::style());
    }

private Q_SLOTS:
    void theHaloSurroundsTheTarget()
    {
        ScrollFixture fixture;

        auto* nesting = new QWidget(fixture.content);
        nesting->setGeometry(10, 20, 100, 100);

        auto* target = new QWidget(nesting);
        target->setGeometry(5, 7, 40, 12);

        const QRect halo
            = Gui::SearchHighlightOverlay::highlightRect(target, fixture.area.viewport(), testMargins);

        // 10 + 5 across and 20 + 7 down from the viewport origin, then grown by 4 a side.
        QCOMPARE(halo, QRect(11, 23, 48, 20));
    }

    void aTargetOnAnInactivePageGetsNoHalo()
    {
        ScrollFixture fixture;

        auto* stack = new QStackedWidget(fixture.content);
        stack->setGeometry(0, 0, 400, 400);

        auto* firstPage = new QWidget;
        auto* secondPage = new QWidget;
        stack->addWidget(firstPage);
        stack->addWidget(secondPage);
        stack->setCurrentWidget(firstPage);

        auto* onFirstPage = new QWidget(firstPage);
        onFirstPage->setGeometry(5, 5, 30, 10);

        auto* onSecondPage = new QWidget(secondPage);
        onSecondPage->setGeometry(5, 5, 30, 10);

        const QRect visible = Gui::SearchHighlightOverlay::highlightRect(
            onFirstPage,
            fixture.area.viewport(),
            testMargins
        );
        const QRect hidden = Gui::SearchHighlightOverlay::highlightRect(
            onSecondPage,
            fixture.area.viewport(),
            testMargins
        );

        // Asserted together: without the first line the second would also pass on a fixture
        // that simply never produces a halo.
        QVERIFY(!visible.isNull());
        QVERIFY(hidden.isNull());
    }

    void aWidgetOutsideTheViewportGetsNoHalo()
    {
        ScrollFixture fixture;

        QWidget elsewhere;
        auto* stranger = new QWidget(&elsewhere);
        stranger->setGeometry(5, 5, 30, 10);

        const QRect outsider = Gui::SearchHighlightOverlay::highlightRect(
            stranger,
            fixture.area.viewport(),
            testMargins
        );
        const QRect absent = Gui::SearchHighlightOverlay::highlightRect(
            nullptr,
            fixture.area.viewport(),
            testMargins
        );

        QVERIFY(outsider.isNull());
        QVERIFY(absent.isNull());
    }

    void scrollingMovesTheHalo()
    {
        ScrollFixture fixture;

        auto* target = new QWidget(fixture.content);
        target->setGeometry(10, 300, 40, 12);

        fixture.area.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.area));

        const QRect before
            = Gui::SearchHighlightOverlay::highlightRect(target, fixture.area.viewport(), testMargins);

        fixture.area.verticalScrollBar()->setValue(50);

        const QRect after
            = Gui::SearchHighlightOverlay::highlightRect(target, fixture.area.viewport(), testMargins);

        QCOMPARE(after, before.translated(0, -50));
    }

    void settingATargetShowsTheOverlay()
    {
        ScrollFixture fixture;

        auto* target = new QWidget(fixture.content);
        target->setGeometry(10, 20, 40, 12);

        auto* overlay = new Gui::SearchHighlightOverlay(&fixture.area);

        fixture.area.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.area));

        overlay->setTarget(target);
        QCOMPARE(overlay->target(), target);
        QVERIFY(overlay->isVisible());

        overlay->setTarget(nullptr);
        QVERIFY(overlay->target() == nullptr);
        QVERIFY(!overlay->isVisible());
    }

    void aDestroyedTargetClearsItself()
    {
        ScrollFixture fixture;

        auto* target = new QWidget(fixture.content);
        target->setGeometry(10, 20, 40, 12);

        auto* overlay = new Gui::SearchHighlightOverlay(&fixture.area);

        fixture.area.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.area));

        overlay->setTarget(target);
        delete target;

        QVERIFY(overlay->target() == nullptr);
        QVERIFY(!overlay->isVisible());
    }

    void theOverlayResolvesTheSearchHighlightTokens()
    {
        ScrollFixture fixture;

        auto* overlay = new Gui::SearchHighlightOverlay(&fixture.area);

        const Gui::StyleParameters::StyleContext context = Gui::FreeCADStyle::contextOf(overlay);

        QCOMPARE(style()->resolveBoxStyle(context).background.color(), QColor(0x11, 0x22, 0x33));
    }

    void theHaloMarginComesFromTheToken()
    {
        ScrollFixture fixture;

        auto* overlay = new Gui::SearchHighlightOverlay(&fixture.area);

        const Gui::StyleParameters::StyleContext context = Gui::FreeCADStyle::contextOf(overlay);

        QCOMPARE(style()->resolveBoxGeometry(context).margin, QMarginsF(6, 6, 6, 6));
    }
};

QTEST_MAIN(TestSearchHighlightOverlay)

#include "SearchHighlightOverlay.moc"
