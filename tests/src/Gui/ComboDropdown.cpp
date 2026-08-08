// SPDX-License-Identifier: LGPL-2.1-or-later

#include <memory>

#include <QAbstractItemModel>
#include <QComboBox>
#include <QImage>
#include <QListView>
#include <QPainter>
#include <QScopeGuard>
#include <QStyleFactory>
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
                },
                {.name = "Dropdown Fixture"}
            )
        );
    }

private:
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
};

QTEST_MAIN(TestComboDropdown)

#include "ComboDropdown.moc"
