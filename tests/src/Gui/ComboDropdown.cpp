// SPDX-License-Identifier: LGPL-2.1-or-later

#include <cstring>
#include <memory>

#include <QAbstractItemModel>
#include <QComboBox>
#include <QImage>
#include <QListView>
#include <QPainter>
#include <QScopeGuard>
#include <QStyleFactory>
#include <QStyleOptionComboBox>
#include <QStyleOptionMenuItem>
#include <QTest>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>

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

                    // The popup surface. The container around the list paints it, so these are
                    // the tokens the popup's fill and edge come from.
                    {.name = "DropdownListBackground", .value = "#101010"},
                    {.name = "DropdownListBorderColor", .value = "#00ff00"},
                    {.name = "DropdownListBorderThickness", .value = "1px"},
                    {.name = "DropdownListBorderRadius", .value = "0px"},
                    {.name = "DropdownListPadding", .value = "padding(2px)"},

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
    static Gui::FreeCADStyle& installFreshApplicationStyle()
    {
        auto* style = new Gui::FreeCADStyle();
        QApplication::setStyle(style);
        return *style;
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
    // row but the first reserves above itself. The menu route answered this question with the size
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

        // The label, plus DropdownListItemPadding on both edges.
        QCOMPARE(firstRow, view->fontMetrics().height() + (2 * itemPaddingVertical));

        // Every row after the first also carries DropdownListItemSpacing.
        QCOMPARE(secondRow - firstRow, itemSpacing);

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
};

QTEST_MAIN(TestComboDropdown)

#include "ComboDropdown.moc"
