// SPDX-License-Identifier: LGPL-2.1-or-later

#include <cstring>
#include <memory>

#include <QAbstractItemModel>
#include <QComboBox>
#include <QGuiApplication>
#include <QImage>
#include <QListView>
#include <QPainter>
#include <QRegion>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollBar>
#include <QStyleFactory>
#include <QStyleOptionComboBox>
#include <QStyleOptionMenuItem>
#include <QTest>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>
#include <Gui/ThemeReloadEvent.h>

// The style handles a theme reload in its event filter, which is protected. Delivering the event
// the way Gui::Application does — sending it to qApp, where the style is installed as a filter —
// is not open to a test either: every filter on qApp sees it, including Application's own, which
// reapplies the stylesheet through a main window a headless test does not have. Widening the
// access reaches the style's handler, which is the wiring under test, and nothing else.
class ReloadableStyle: public Gui::FreeCADStyle
{
public:
    using Gui::FreeCADStyle::eventFilter;
};

class TestComboDropdown: public QObject
{
    Q_OBJECT

public:
    TestComboDropdown()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "DropdownListMaxHeight", .value = "250px"},
                    // Mirrors the production token: a dropdown that must show every row clears
                    // the shared cap for itself alone.
                    {.name = "UncappedDropdownMaxHeight", .value = "reset()"},
                    {.name = "ShortDropdownMaxHeight", .value = "80px"},

                    // The combo box itself, which production also gives a padding. Stated here
                    // because it is what the popup's frame width must NOT be resolved from:
                    // without it nothing would inflate for a Select and a metric that leaked
                    // from the view onto the combo box would go unnoticed.
                    {.name = "SelectPadding", .value = "padding(horizontal: 8px, vertical: 4px)"},

                    // The popup surface. The container around the list paints it, so these are
                    // the tokens the popup's fill and edge come from.
                    {.name = "DropdownListBackground", .value = "#101010"},
                    {.name = "DropdownListBorderColor", .value = "#00ff00"},
                    {.name = "DropdownListBorderThickness", .value = "1px"},
                    {.name = "DropdownListBorderRadius", .value = "0px"},
                    // Deliberately larger than DropdownListItemSpacing below. Every row carries
                    // a leading gap, and the container gives the first one back by shrinking
                    // this padding at the top; a padding smaller than the gap would clamp at
                    // zero and hide whether the deduction happens at all.
                    {.name = "DropdownListPadding", .value = "padding(4px)"},

                    // The rows. The item-view path splits a row between two elements: padding
                    // and label colour on Item, the interaction fill on Row. Values are picked
                    // to be unmistakable, so a token resolved against the wrong element shows up
                    // as a wrong measurement rather than an accidental match.
                    {.name = "DropdownListItemPadding",
                     .value = "padding(horizontal: 7px, vertical: 5px)"},
                    {.name = "DropdownListItemSpacing", .value = "3px"},
                    {.name = "DropdownListRowCheckedBackground", .value = "#ff0000"},
                },
                {.name = "Dropdown Fixture"}
            )
        );

        // Registered last so it outranks the fixture above, and left empty so it costs
        // nothing until a test asks for a different value.
        overrides = new Gui::StyleParameters::InMemoryParameterSource(
            {},
            {.name = "Dropdown Fixture Overrides"}
        );
        Gui::Application::Instance->styleParameterManager()->addSource(overrides);
    }

private:
    Gui::StyleParameters::InMemoryParameterSource* overrides = nullptr;

    // Vertical half of DropdownListItemPadding, and the inter-row gap, as the fixture states
    // them. Named so an assertion reads as the token it is pinning.
    static constexpr int itemPaddingVertical = 5;
    static constexpr int itemSpacing = 3;

    // DropdownListBorderThickness and DropdownListPadding, as the fixture states them. Together
    // they are the whole inset between the popup's edge and the first row.
    static constexpr int containerBorder = 1;
    static constexpr int containerPadding = 4;

    // Where the first row's box started before every row was given a leading gap: the container
    // inset and nothing else, because row 0 alone reserved no gap above itself. The change adds
    // that gap to row 0 and takes the same amount off the top inset, so this must not move.
    static constexpr int baselineFirstRowTop = containerBorder + containerPadding;

    // The popup's total height as the pre-change model computed it, on 2026-08-09: one gap
    // between each adjacent pair of rows and none above the first, inside the container's
    // border and padding.
    //
    // A formula rather than the raw pixel count it was measured at, because the label's height
    // comes from the ambient font and the platform theme picks that — the number is 100 under
    // qt5ct and 97 with no theme plugin, while the invariant holds in both.
    static int baselineContainerHeight(const QListView& view, int rowCount)
    {
        const int rowHeight = view.fontMetrics().height() + (2 * itemPaddingVertical);

        return (2 * containerBorder) + (2 * containerPadding) + (rowCount * rowHeight)
            + ((rowCount - 1) * itemSpacing);
    }

    // Swaps one token in for the body of a test and puts the fixture's value back on the way
    // out, so an assertion that returns early cannot leak it into the next test.
    [[nodiscard]] auto overrideToken(const std::string& name, const std::string& value) const
    {
        auto* manager = Gui::Application::Instance->styleParameterManager();

        overrides->define({.name = name, .value = value});
        manager->reload();

        return qScopeGuard([this, manager, name] {
            overrides->remove(name);
            manager->reload();
        });
    }

    // The popup list is created lazily by QComboBox, so ask for it only after polishing.
    static QListView* popupOf(QComboBox& box)
    {
        return qobject_cast<QListView*>(box.view());
    }

    // Short labels, so a correctly sized popup sits well under the cap and a failure can only
    // be about the row height, never about the content being genuinely too tall.
    static void populate(QComboBox& box)
    {
        box.addItems({QStringLiteral("Alpha"), QStringLiteral("Beta"), QStringLiteral("Gamma")});
    }

    // Far more rows than DropdownListMaxHeight can hold, so the popup is capped and has to
    // scroll — the only shape in which a partial row's worth of surface can be left over.
    static void populateBeyondTheCap(QComboBox& box)
    {
        for (int row = 0; row < 40; ++row) {
            box.addItem(QStringLiteral("Item %1").arg(row));
        }
    }

    // A menu-item option shaped the way QComboMenuDelegate builds one: the widget is the combo
    // box, never a QMenu, and the rect is the whole list-view viewport rather than a row.
    static QStyleOptionMenuItem comboRowItem(const QComboBox& box, const QListView& view)
    {
        QStyleOptionMenuItem option;
        option.initFrom(&box);
        option.menuItemType = QStyleOptionMenuItem::Normal;
        option.checkType = QStyleOptionMenuItem::NotCheckable;
        option.checked = false;
        option.menuHasCheckableItems = false;
        option.maxIconWidth = 0;
        option.reservedShortcutWidth = 0;
        option.text = QStringLiteral("Alpha");
        option.rect = view.rect();
        return option;
    }

    // The widget as it paints itself, over a ground no token in the fixture can produce — so a
    // pixel nothing painted is impossible to mistake for one something did.
    static QImage renderOf(QWidget& widget)
    {
        QImage canvas(widget.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);
        widget.render(&canvas);
        return canvas;
    }

    // A FreeCADStyle installed the way Gui::Application installs it: as the application's.
    //
    // A combo box builds its popup as a window of its own, and a window inherits nothing from a
    // style set on one widget — set on the combo box alone, the popup would keep whatever the
    // platform theme provides and every measurement of it would be of the wrong style. It is
    // also always a fresh instance, because box geometry is cached for a style's lifetime and
    // only clearTokenCache() drops it, which an installed style is told to do on a theme reload
    // but not on the token overrides a test makes. QApplication takes ownership of the style it
    // is given and deletes the one it replaces, so successive calls clean up after each other.
    static ReloadableStyle& installFreshApplicationStyle()
    {
        auto* style = new ReloadableStyle();
        QApplication::setStyle(style);
        return *style;
    }

    // The y at which the popup's first row starts painting, measured on the container so the
    // frame's own inset counts. Row 0 is the combo's current item, so its box carries
    // DropdownListRowCheckedBackground and is the topmost red pixel down the popup's centre.
    // The box, not the cell: the cell above it also holds the leading gap, which is exactly
    // what moved, so a cell-based measurement would report the move rather than survive it.
    static int firstRowBoxTop(QWidget& container)
    {
        const QImage canvas = renderOf(container);
        const int middle = canvas.width() / 2;

        for (int y = 0; y < canvas.height(); ++y) {
            if (canvas.pixelColor(middle, y) == QColor(0xff, 0x00, 0x00)) {
                return y;
            }
        }

        return -1;
    }

    // The height of row 0 of a freshly shown popup, under a style built after the caller's
    // token overrides.
    static int firstRowHeightWithAFreshStyle()
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        return popupOf(box)->visualRect(box.model()->index(0, 0)).height();
    }

private Q_SLOTS:

    // The property this change exists to create: every row the same height, so the pitch
    // between any adjacent pair is identical. Row 0 was previously shorter by exactly the
    // gap, which is what made a capped popup's trim leave a residue.
    void test_everyRowHasTheSamePitch()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QListView* view = popupOf(box);
        QAbstractItemModel* model = box.model();

        const int firstPitch = view->visualRect(model->index(1, 0)).top()
            - view->visualRect(model->index(0, 0)).top();

        for (int row = 2; row < model->rowCount(); ++row) {
            const int pitch = view->visualRect(model->index(row, 0)).top()
                - view->visualRect(model->index(row - 1, 0)).top();
            QCOMPARE(pitch, firstPitch);
        }

        // And every row is the same height, not merely evenly spaced.
        const int firstHeight = view->visualRect(model->index(0, 0)).height();
        for (int row = 1; row < model->rowCount(); ++row) {
            QCOMPARE(view->visualRect(model->index(row, 0)).height(), firstHeight);
        }
    }

    // The cancellation is the design's core claim: the gap added to row 0's height is exactly
    // the gap removed from the container's top inset, so nothing moves and nothing grows.
    // These constants are the PRE-CHANGE baseline, measured against the build before this
    // work. This test must pass BOTH before and after. If it goes red, the cancellation is
    // wrong and the design is broken — not the test.
    void test_totalHeightAndFirstRowPositionAreUnchanged()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QListView* view = popupOf(box);
        QWidget* container = view->parentWidget();

        QCOMPARE(container->height(), baselineContainerHeight(*view, box.count()));
        QCOMPARE(firstRowBoxTop(*container), baselineFirstRowTop);
    }

    // The model only works while the gap fits inside the padding. Past that the top inset
    // would have to go negative, which SE_ShapedFrameContents cannot express, so it clamps
    // and the first row sits `spacing - padding` lower. Documented, not silent.
    void test_aGapLargerThanThePaddingClampsRatherThanInverting()  // NOLINT
    {
        const auto paddingGuard = overrideToken("DropdownListPadding", "padding(2px)");
        const auto spacingGuard = overrideToken("DropdownListItemSpacing", "8px");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QWidget* container = popupOf(box)->parentWidget();

        // Border 1 + clamped top padding 0 + the row's own 8px leading gap.
        QCOMPARE(firstRowBoxTop(*container), 1 + 0 + 8);
    }

    // Without an override the shared DropdownList cap applies, as it does to every combo box.
    void test_sharedCapApplies()  // NOLINT
    {
        QComboBox box;
        Gui::FreeCADStyle style;
        style.polish(&box);

        QCOMPARE(popupOf(box)->maximumHeight(), 250);
        QCOMPARE(popupOf(box)->parentWidget()->maximumHeight(), 250);
    }

    // The named component is consulted ahead of DropdownList, so one dropdown can differ.
    void test_overrideReplacesSharedCap()  // NOLINT
    {
        QComboBox box;
        box.setProperty("dropdownComponent", "ShortDropdown");

        Gui::FreeCADStyle style;
        style.polish(&box);

        QCOMPARE(popupOf(box)->maximumHeight(), 80);
        QCOMPARE(popupOf(box)->parentWidget()->maximumHeight(), 80);
    }

    // reset() cancels the cap rather than falling through to the shared one, leaving the
    // dropdown to Qt — which is what lets a combo box show a row per item.
    void test_overrideCanCancelTheCap()  // NOLINT
    {
        QComboBox box;
        box.setProperty("dropdownComponent", "UncappedDropdown");

        Gui::FreeCADStyle style;
        style.polish(&box);

        QCOMPARE(popupOf(box)->maximumHeight(), QWIDGETSIZE_MAX);
        QCOMPARE(popupOf(box)->parentWidget()->maximumHeight(), QWIDGETSIZE_MAX);
    }

    // The override names the popup, not the combo box, so the combo box keeps resolving as one.
    void test_overrideDoesNotRenameTheComboBox()  // NOLINT
    {
        QComboBox box;
        box.setProperty("dropdownComponent", "ShortDropdown");

        Gui::FreeCADStyle style;
        style.polish(&box);

        QVERIFY(!box.property("component").isValid());
        QCOMPARE(popupOf(box)->property("component").toString(), QStringLiteral("ShortDropdown"));
    }

    // The cap only bounds the popup; it says nothing about what is inside it. With
    // SH_ComboBox_Popup on, QComboBox sizes its rows through a QComboMenuDelegate, which asks
    // the style for CT_MenuItem passing the combo box as the widget and the whole viewport as
    // the contents size. A menu handler that answers that question instead of declining to the
    // base style makes one row as tall as the entire popup, and every other row falls out of it.
    void test_everyRowOfAPopulatedPopupIsVisible()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QComboBox box;
        box.setStyle(&style);
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QListView* view = popupOf(box);
        const int rowHeight = view->visualRect(box.model()->index(0, 0)).height();
        QVERIFY2(rowHeight > 0, qPrintable(QStringLiteral("row height %1px").arg(rowHeight)));
        QVERIFY2(
            rowHeight < view->maximumHeight(),
            qPrintable(QStringLiteral("row height %1px does not fit the %2px popup")
                           .arg(rowHeight)
                           .arg(view->maximumHeight()))
        );

        // Three short rows stack well inside the cap, so the popup shows every one of them
        // without scrolling. Only the vertical extent is asserted: a menu-style row is as wide
        // as the widest label plus the shortcut column and is routinely wider than the popup,
        // which clips it — but a row that starts or ends outside the viewport is not on screen.
        const QRect viewport = view->viewport()->rect();
        for (int row = 0; row < box.count(); ++row) {
            const QRect itemRect = view->visualRect(box.model()->index(row, 0));
            QVERIFY2(
                itemRect.top() >= viewport.top() && itemRect.bottom() <= viewport.bottom(),
                qPrintable(QStringLiteral("row %1 spans y %2..%3, outside the viewport's %4..%5")
                               .arg(row)
                               .arg(itemRect.top())
                               .arg(itemRect.bottom())
                               .arg(viewport.top())
                               .arg(viewport.bottom()))
            );
        }
    }

    // The general guard for the whole class of bug: a handler that decides it does not own a
    // widget must let the base style answer, never invent a value of its own. CT_MenuItem is
    // reachable with a QComboBox because of QComboMenuDelegate, so the combo box is the honest
    // subject here — but the claim is about declining, not about combo boxes.
    void test_menuItemSizingDeclinesToTheBaseStyle()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QComboBox box;
        box.setStyle(&freecadStyle);
        populate(box);
        freecadStyle.polish(&box);

        // FreeCADStyle proxies Fusion, so Fusion is the baseline — not QApplication::style(),
        // which a platform theme plugin (qt5ct, qt6ct) replaces, making the comparison depend
        // on the developer's environment.
        const std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("Fusion")));
        QVERIFY(fusion != nullptr);

        QListView* view = popupOf(box);
        const QStyleOptionMenuItem option = comboRowItem(box, *view);
        const QSize contentsSize = view->rect().size();

        QCOMPARE(
            style.sizeFromContents(QStyle::CT_MenuItem, &option, contentsSize, &box),
            fusion->sizeFromContents(QStyle::CT_MenuItem, &option, contentsSize, &box)
        );
    }

    // QComboBoxPrivateContainer paints its popup surface with PE_PanelMenu, but it is a plain
    // QFrame and not a QMenu, so the menu surface handler does not own it. Declining has to mean
    // handing the surface back to the base style, which fills it and draws its 1px outer frame;
    // painting nothing leaves the popup with no edge at all.
    void test_popupSurfaceKeepsTheBaseStyleFrame()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QComboBox box;
        box.setStyle(&freecadStyle);
        populate(box);
        freecadStyle.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        // The real popup container, at the size and with the palette it was just shown with.
        QWidget* container = popupOf(box)->parentWidget();
        QStyleOption option;
        option.initFrom(container);

        const auto surfaceOf = [container, &option](QStyle& painting) {
            QImage canvas(container->size(), QImage::Format_ARGB32);
            canvas.fill(Qt::magenta);

            QPainter painter(&canvas);
            painting.drawPrimitive(QStyle::PE_PanelMenu, &option, &painter, container);
            painter.end();

            return canvas;
        };

        // FreeCADStyle proxies Fusion, so Fusion is the baseline a widget it declines must
        // still see — not QApplication::style(), which a platform theme plugin (qt5ct, qt6ct)
        // replaces, making the comparison depend on the developer's environment.
        const std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("Fusion")));
        QVERIFY(fusion != nullptr);

        const QImage baseline = surfaceOf(*fusion);
        const QPoint edge(0, container->height() / 2);
        const QPoint interior(container->width() / 2, container->height() / 2);

        // The baseline really does draw an edge distinct from the surface behind it, so the
        // comparison below cannot pass by both styles painting nothing.
        QVERIFY(baseline.pixelColor(edge) != QColor(Qt::magenta));
        QVERIFY(baseline.pixelColor(edge) != baseline.pixelColor(interior));

        QCOMPARE(surfaceOf(style), baseline);
    }

    // Which of the two popup routes Qt takes is a decision of ours, not an accident of the base
    // style, and everything below depends on it: declining SH_ComboBox_Popup is what stops Qt
    // installing a QComboMenuDelegate and leaves the rows on the item-view path the DropdownList
    // tokens describe.
    void test_comboPopupIsNotRenderedAsAMenu()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QComboBox box;
        box.setStyle(&freecadStyle);
        populate(box);
        freecadStyle.polish(&box);

        QStyleOptionComboBox option;
        option.initFrom(&box);

        QCOMPARE(style.styleHint(QStyle::SH_ComboBox_Popup, &option, &box, nullptr), 0);

        // The consequence, not just the number: Qt picks the delegate from that hint, and the
        // menu one is what sized a row to the whole viewport.
        QVERIFY2(
            std::strcmp(box.view()->itemDelegate()->metaObject()->className(), "QComboMenuDelegate")
                != 0,
            box.view()->itemDelegate()->metaObject()->className()
        );
    }

    // A populated popup's rows are ordinary item-view rows, so their height is assembled from the
    // DropdownList item tokens: the label, the item padding around it, and the inter-row gap every
    // row reserves above itself. The menu route answered this question with the size
    // of the entire viewport, which is both wrong and impossible to tell from a plausible number
    // unless the tokens are pinned.
    void test_popupRowsAreItemViewRowsOfATokenDerivedHeight()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QListView* view = popupOf(box);
        const int firstRow = view->visualRect(box.model()->index(0, 0)).height();
        const int secondRow = view->visualRect(box.model()->index(1, 0)).height();

        // The label, plus DropdownListItemPadding on both edges and the DropdownListItemSpacing
        // gap the row reserves above itself.
        QCOMPARE(firstRow, view->fontMetrics().height() + (2 * itemPaddingVertical) + itemSpacing);

        // Every row reserves that gap, the first included, so the pitch never varies.
        QCOMPARE(secondRow, firstRow);

        // Three such rows sit well inside the cap, so the popup is nowhere near being one row
        // tall — the shape the regression took.
        QVERIFY2(
            (box.count() * secondRow) < view->maximumHeight(),
            qPrintable(QStringLiteral("%1 rows of %2px do not fit comfortably in the %3px cap")
                           .arg(box.count())
                           .arg(secondRow)
                           .arg(view->maximumHeight()))
        );
    }

    // A separator row is a 1px rule, and the item-view popup route has to keep it one. Qt sizes
    // one as QSize(pm, pm) from PM_DefaultFrameWidth asked with the *combo box* as the widget,
    // never the popup view, so this pins the one thing that could make it grow: item-view frame
    // padding reaching a QComboBox. The fixture gives Select a padding precisely so there is
    // something to leak — widen resolveItemViewFrameWidth()'s guard to cover combo boxes and this
    // row becomes 5px.
    void test_separatorRowsAreOnePixel()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        box.insertSeparator(1);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QCOMPARE(popupOf(box)->visualRect(box.model()->index(1, 0)).height(), 1);
    }

    // A rounded scroll area is clipped to its border radius so the compositor does not show the
    // widget's square corners. A combo popup's radius belongs to the container that paints its
    // edge, not to the list sitting inset inside it — masking the list would round a widget whose
    // corners are nowhere near the popup's, and leave the visible edge square.
    void test_aRoundedPopupDoesNotMaskItsList()  // NOLINT
    {
        const auto radiusGuard = overrideToken("DropdownListBorderRadius", "8px");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QVERIFY2(
            popupOf(box)->mask().isEmpty(),
            qPrintable(QStringLiteral("the popup list was clipped to %1 rect(s)")
                           .arg(popupOf(box)->mask().rectCount()))
        );
    }

    // The dropdown tokens are live rather than merely present: changing the one the row padding
    // comes from moves the row. A token written against a component or element the item-view path
    // never asks for resolves to nothing in silence, and the row keeps whatever it had.
    void test_rowPaddingFollowsTheDropdownItemToken()  // NOLINT
    {
        const int before = firstRowHeightWithAFreshStyle();

        constexpr int taller = itemPaddingVertical + 6;
        const auto restore = overrideToken(
            "DropdownListItemPadding",
            "padding(horizontal: 7px, vertical: " + std::to_string(taller) + "px)"
        );

        QCOMPARE(firstRowHeightWithAFreshStyle() - before, 2 * (taller - itemPaddingVertical));
    }

    // The selected row's fill comes from DropdownListRowCheckedBackground — the Row element. A
    // menu states the same fill on its item, and an alias that copied that name across would
    // never resolve here: the item-view path paints the interaction layer from Row alone. Only a
    // rendered popup can tell "the token is defined" from "the token reached the paint".
    void test_selectedRowIsFilledFromTheDropdownRowToken()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QListView* view = popupOf(box);
        view->setCurrentIndex(box.model()->index(1, 0));

        const QImage canvas = renderOf(*view);
        const QPoint selected = view->visualRect(box.model()->index(1, 0)).center();
        const QPoint resting = view->visualRect(box.model()->index(0, 0)).center();

        QCOMPARE(canvas.pixelColor(selected), QColor(0xff, 0x00, 0x00));
        QVERIFY(canvas.pixelColor(resting) != QColor(0xff, 0x00, 0x00));
    }

    // With the popup off the menu route Qt no longer asks for PE_PanelMenu, so the popup's edge
    // is the container QFrame's own frame, drawn through PE_Frame from the DropdownList surface
    // tokens. That is the only thing standing between the popup and having no edge at all.
    void test_popupSurfaceIsDrawnFromTheDropdownSurfaceTokens()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QWidget* container = popupOf(box)->parentWidget();
        const QImage canvas = renderOf(*container);

        // DropdownListBorderThickness is 1px, so the outermost ring is the border and the pixel
        // behind it the surface.
        const int middle = container->height() / 2;
        QCOMPARE(canvas.pixelColor(0, middle), QColor(0x00, 0xff, 0x00));
        QCOMPARE(canvas.pixelColor(1, middle), QColor(0x10, 0x10, 0x10));
    }

    // The property that actually broke: rows overlapped by 6px because the view's cached item
    // layout used a row height resolved before the view became a DropdownList, while every
    // fresh size-hint query returned the new one. No single function was wrong, so no test
    // that asks a function for a number could see it. This asserts the layout is
    // self-consistent instead.
    void test_popupRowsAbutWithoutOverlapping()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);

        // The ordering the running application was measured in: constrainComboDropdown() calls
        // comboBox->view(), which creates the view, and only afterwards tags it as a dropdown.
        // Asking the untagged view for a row rectangle lays its items out at the plain List
        // pitch, which is the cache the tagging has to invalidate. Without this the view's first
        // layout would happen after the tag, and the defect would be out of reach of the test.
        box.view()->visualRect(box.model()->index(0, 0));

        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QListView* view = popupOf(box);
        QAbstractItemModel* model = box.model();

        for (int row = 1; row < model->rowCount(); ++row) {
            const QRect previous = view->visualRect(model->index(row - 1, 0));
            const QRect current = view->visualRect(model->index(row, 0));
            QCOMPARE(current.top(), previous.bottom() + 1);
        }

        const QRect last = view->visualRect(model->index(model->rowCount() - 1, 0));
        QVERIFY2(
            last.bottom() <= view->viewport()->rect().bottom(),
            qPrintable(QStringLiteral("last row ends at %1, viewport at %2")
                           .arg(last.bottom())
                           .arg(view->viewport()->rect().bottom()))
        );
    }

    // A theme reload drops the style's caches but nothing tells a view its rows changed size,
    // so its layout would keep the pre-reload pitch. Same defect as the tagging one, reached
    // by the route the owner actually uses while retuning tokens.
    void test_rowPitchFollowsAThemeReload()  // NOLINT
    {
        ReloadableStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QListView* view = popupOf(box);
        QAbstractItemModel* model = box.model();
        const int pitchBefore = view->visualRect(model->index(1, 0)).top()
            - view->visualRect(model->index(0, 0)).top();

        const auto tokenGuard
            = overrideToken("DropdownListItemPadding", "padding(horizontal: 7px, vertical: 11px)");

        // qApp is the object Gui::Application sends the reload to, so it is the object the
        // filter is handed here.
        Gui::ThemeReloadEvent reloadEvent;
        style.eventFilter(qApp, &reloadEvent);
        QCoreApplication::processEvents();

        const int pitchAfter = view->visualRect(model->index(1, 0)).top()
            - view->visualRect(model->index(0, 0)).top();

        // The fixture states 5px vertical padding; the override states 11px, so each row grows
        // by twice the 6px difference.
        QCOMPARE(pitchAfter - pitchBefore, 12);
    }

    // The selected row lands on the combo box, which is how a menu-style popup behaves and what
    // Qt did before this branch declined SH_ComboBox_Popup.
    void test_placementOverCurrentPutsTheSelectedRowOnTheComboBox()  // NOLINT
    {
        const auto tokenGuard = overrideToken("DropdownListPlacement", "current");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        box.setCurrentIndex(1);
        style.polish(&box);
        box.move(200, 200);
        box.show();

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });
        QCoreApplication::processEvents();  // the correction is deferred by a zero timer

        // Walk the real widget hierarchy to the row rather than adding up the container's margins:
        // the claim is about where the row ends up on screen, not about the arithmetic that put
        // it there.
        QListView* view = popupOf(box);
        const QPoint rowTopLeft = view->visualRect(box.model()->index(1, 0)).topLeft();
        const int rowTopGlobal = view->viewport()->mapToGlobal(rowTopLeft).y();

        QCOMPARE(rowTopGlobal, box.mapToGlobal(QPoint {}).y());
    }

    // The popup's top edge meets the combo box's bottom edge exactly — no 1px bite out of the
    // combo's own border, which is what Qt's uncorrected list placement produces.
    void test_placementBelowMeetsTheComboBoxEdge()  // NOLINT
    {
        const auto tokenGuard = overrideToken("DropdownListPlacement", "below");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);
        box.move(200, 200);
        box.show();

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });
        QCoreApplication::processEvents();

        QWidget* container = popupOf(box)->parentWidget();
        const QPoint comboTopLeft = box.mapToGlobal(QPoint {});

        QCOMPARE(container->mapToGlobal(QPoint {}).y(), comboTopLeft.y() + box.height());
    }

    // The offset applies on top of whichever mode is in force, so a gap or an overlap can be
    // dialled in without a rebuild.
    void test_placementOffsetShiftsThePopup()  // NOLINT
    {
        const auto modeGuard = overrideToken("DropdownListPlacement", "below");
        const auto offsetGuard = overrideToken("DropdownListPlacementOffset", "6px");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);
        box.move(200, 200);
        box.show();

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });
        QCoreApplication::processEvents();

        QWidget* container = popupOf(box)->parentWidget();
        QCOMPARE(
            container->mapToGlobal(QPoint {}).y(),
            box.mapToGlobal(QPoint {}).y() + box.height() + 6
        );
    }

    // No mode may push the popup off the screen: the clamp outranks the placement.
    void test_placementNeverLeavesTheScreen()  // NOLINT
    {
        const auto tokenGuard = overrideToken("DropdownListPlacement", "current");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        box.setCurrentIndex(2);
        style.polish(&box);

        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
        box.move(available.left() + 40, available.bottom() - box.sizeHint().height());
        box.show();

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });
        QCoreApplication::processEvents();

        QWidget* container = popupOf(box)->parentWidget();
        const int top = container->mapToGlobal(QPoint {}).y();
        QVERIFY(top >= available.top());
        QVERIFY(top + container->height() <= available.bottom() + 1);
    }

    // The other edge, and the one an uncapped dropdown reaches. OverCurrent subtracts the current
    // row's offset from the combo box's position, so a long list scrolled to a late row asks for a
    // top well above the desktop — and, the list being taller than the space below the combo box,
    // the lower clamp has nothing to give back. Only the upper clamp stops the popup there, and
    // without it the current row itself is what ends up off screen.
    void test_placementClampsALongPopupToTheTopOfTheScreen()  // NOLINT
    {
        const auto modeGuard = overrideToken("DropdownListPlacement", "current");
        // The shared cap would keep the popup short enough to place honestly; the workbench
        // selector clears it the same way, which is what makes this shape a shipped one.
        const auto capGuard = overrideToken("DropdownListMaxHeight", "reset()");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();

        QComboBox box;
        // More rows than the desktop can show, with maxVisibleItems raised past them so nothing
        // but the (cleared) cap and Qt's own screen fit bound the popup.
        const int rowCount = available.height();
        box.setMaxVisibleItems(rowCount);
        for (int row = 0; row < rowCount; ++row) {
            box.addItem(QStringLiteral("Item %1").arg(row));
        }
        box.setCurrentIndex(rowCount - 1);
        style.polish(&box);

        box.move(available.left() + 40, available.top());
        box.show();

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });
        QCoreApplication::processEvents();

        QListView* view = popupOf(box);
        QWidget* container = view->parentWidget();
        const int rowOffsetInPopup
            = view->viewport()->mapTo(container, view->visualRect(view->currentIndex()).topLeft()).y();

        // The precondition the clamp exists for, stated rather than assumed: aligning the current
        // row with the combo box would have put the popup's top above the desktop.
        QVERIFY2(
            box.mapToGlobal(QPoint {}).y() - rowOffsetInPopup < available.top(),
            qPrintable(QStringLiteral(
                           "the current row sits %1px into the popup and the combo box %2px "
                           "below the top of the desktop, so the placement stays on screen "
                           "unaided and the clamp under test is never reached"
            )
                           .arg(rowOffsetInPopup)
                           .arg(box.mapToGlobal(QPoint {}).y() - available.top()))
        );

        const int top = container->mapToGlobal(QPoint {}).y();
        QVERIFY2(
            top >= available.top(),
            qPrintable(QStringLiteral(
                           "the popup starts %1px above the desktop, so its first rows "
                           "— the current one among them — are off screen"
            )
                           .arg(available.top() - top))
        );
    }

    // With a uniform pitch the trim has nothing left to do on an unscrolled capped popup — the
    // 3px residue that survived the old model is gone. This is the payoff the uniform gap was
    // for.
    void test_anUnscrolledCappedPopupHasNoResidue()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populateBeyondTheCap(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });
        QCoreApplication::processEvents();  // the correction is deferred by a zero timer

        QListView* view = popupOf(box);
        const int rowHeight = view->sizeHintForRow(0);
        QVERIFY(rowHeight > 0);
        QCOMPARE(view->viewport()->height() % rowHeight, 0);
    }

    // A capped popup scrolls per item, so it only ever shows whole rows: whatever the viewport
    // has left over after the last of them is empty surface, and the owner sees it as a band of
    // background under the bottom row. Measured scrolled, where every visible row carries the
    // leading DropdownListItemSpacing and the pitch is uniform.
    void test_aScrolledCappedPopupEndsOnARowEdge()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populateBeyondTheCap(box);
        style.polish(&box);
        box.move(200, 200);
        box.show();

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });
        QCoreApplication::processEvents();  // the correction is deferred by a zero timer

        QListView* view = popupOf(box);
        QScrollBar* verticalBar = view->verticalScrollBar();

        // The mechanism the band comes from, pinned rather than assumed: per-pixel scrolling
        // shows a partial row instead of leaving the remainder blank, and nothing below applies.
        QCOMPARE(view->verticalScrollMode(), QAbstractItemView::ScrollPerItem);

        // The precondition: a popup that shows every row it has cannot leave a remainder, so it
        // would pass the assertions below without the correction under test ever running.
        QVERIFY2(
            verticalBar->maximum() > 0,
            "the popup shows every row, so it never scrolls and has no remainder"
        );

        verticalBar->setValue(verticalBar->maximum());

        const int rowHeight = view->sizeHintForRow(1);
        QVERIFY(rowHeight > 0);
        QCOMPARE(view->viewport()->height() % rowHeight, 0);

        // The visible claim, not just the arithmetic: the bottom row meets the bottom of the
        // viewport, so there is no surface between the two.
        const QRect lastRowRect = view->visualRect(box.model()->index(box.count() - 1, 0));
        QCOMPARE(lastRowRect.bottom(), view->viewport()->rect().bottom());
    }

    // The trim changes the container's height, and the screen clamp is computed from that height,
    // so the trim has to run first. A capped popup at the bottom of the screen is the shape that
    // tells the two orders apart: the clamp seats the popup's bottom edge on the screen edge, and
    // a trim applied after it lifts the popup clear of that edge by the remainder it removed.
    void test_theTrimPrecedesTheScreenClamp()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();

        QComboBox box;
        populateBeyondTheCap(box);
        style.polish(&box);
        box.move(available.left() + 40, available.bottom() - box.sizeHint().height());
        box.show();

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });
        QCoreApplication::processEvents();  // the correction is deferred by a zero timer

        QWidget* container = popupOf(box)->parentWidget();

        // Two preconditions, because either one missing would let both orders pass.
        //
        // A popup that was not trimmed has the same height before and after the clamp, so the
        // order cannot matter to it.
        QVERIFY2(
            container->height() < container->maximumHeight(),
            qPrintable(QStringLiteral("the popup was not trimmed: %1px against a %2px cap")
                           .arg(container->height())
                           .arg(container->maximumHeight()))
        );

        // And a popup that fits below its combo box unaided never reaches the clamp at all.
        QVERIFY2(
            box.mapToGlobal(QPoint {}).y() + container->height() - 1 > available.bottom(),
            "the popup fits below the combo box, so the clamp under test is never reached"
        );

        const int containerBottom = container->mapToGlobal(QPoint {}).y() + container->height() - 1;
        QVERIFY2(
            containerBottom == available.bottom(),
            qPrintable(QStringLiteral(
                           "the popup ends %1px short of the bottom of the screen: it "
                           "was clamped against a height it no longer has"
            )
                           .arg(available.bottom() - containerBottom))
        );
    }

    // Qt sizes the container afresh on every showPopup(), so the trim starts from the cap each
    // time. A trim that compounded instead would cost the popup a row per open.
    void test_reopeningACappedPopupDoesNotTrimItFurther()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populateBeyondTheCap(box);
        style.polish(&box);
        box.move(200, 200);
        box.show();

        const auto heightAfterOpening = [&box] {
            box.showPopup();
            QCoreApplication::processEvents();
            const int height = popupOf(box)->parentWidget()->height();
            box.hidePopup();
            return height;
        };

        const int firstHeight = heightAfterOpening();
        QCOMPARE(heightAfterOpening(), firstHeight);
        QCOMPARE(heightAfterOpening(), firstHeight);
    }

    // The snap applies only where there is a remainder to give back. An uncapped popup is
    // already exactly as tall as its content, and its first row is shorter than the rest — it
    // carries no leading DropdownListItemSpacing — so a remainder taken against a later row's
    // pitch would shave pixels off a popup that fits perfectly, and push its last row out.
    void test_anUncappedPopupIsNotShrunk()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);
        box.move(200, 200);
        box.show();

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        // Qt has already sized the popup by the time showPopup() returns; the correction is what
        // has not run yet.
        QListView* view = popupOf(box);
        const int viewportHeightBeforeCorrection = view->viewport()->height();

        QCoreApplication::processEvents();

        QCOMPARE(view->viewport()->height(), viewportHeightBeforeCorrection);

        const QRect lastRowRect = view->visualRect(box.model()->index(box.count() - 1, 0));
        QVERIFY2(
            lastRowRect.bottom() <= view->viewport()->rect().bottom(),
            qPrintable(QStringLiteral("the last row ends %1px past the viewport")
                           .arg(lastRowRect.bottom() - view->viewport()->rect().bottom()))
        );
    }
};

QTEST_MAIN(TestComboDropdown)

#include "ComboDropdown.moc"
