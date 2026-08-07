// SPDX-License-Identifier: LGPL-2.1-or-later

#include <memory>
#include <string>

#include <QApplication>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QScopeGuard>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionMenuItem>
#include <QTest>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>

// The fixture's own numbers, chosen so every column width is distinct and no two sums
// collide — a wrong column therefore fails loudly instead of coincidentally matching.
constexpr int menuBorder = 1;
constexpr int menuPadding = 4;
constexpr int itemPaddingH = 6;
constexpr int itemPaddingV = 3;
constexpr int iconSpacing = 8;
constexpr int iconSize = 16;
constexpr int indicatorSize = 14;
constexpr int arrowWidth = 10;
constexpr int shortcutSpacing = 20;
constexpr int separatorHeight = 9;

// menuItemLayout is protected on FreeCADStyle; a using-declaration republishes it so the
// column walk can be exercised without going through a live menu.
class ProbeStyle: public Gui::FreeCADStyle
{
public:
    using Gui::FreeCADStyle::menuItemLayout;
};

class TestMenuGeometry: public QObject
{
    Q_OBJECT

public:
    TestMenuGeometry()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "MenuBackground", .value = "#202020"},
                    {.name = "MenuBorderColor", .value = "#000000"},
                    {.name = "MenuBorderThickness", .value = "1px"},
                    {.name = "MenuBorderRadius", .value = "0px"},
                    {.name = "MenuPadding", .value = "padding(4px)"},
                    {.name = "MenuIconSize", .value = "16px"},
                    {.name = "MenuOverlap", .value = "0px"},

                    {.name = "MenuItemPadding", .value = "padding(horizontal: 6px, vertical: 3px)"},
                    {.name = "MenuItemIconSpacing", .value = "8px"},
                    {.name = "MenuItemSpacing", .value = "0px"},
                    {.name = "MenuItemMargin", .value = "padding(0px)"},
                    {.name = "MenuItemTextColor", .value = "#ffffff"},
                    {.name = "MenuItemHoveredBackground", .value = "#ff0000"},

                    {.name = "MenuShortcutSpacing", .value = "20px"},
                    {.name = "MenuShortcutTextColor", .value = "#808080"},

                    {.name = "MenuSeparatorHeight", .value = "9px"},
                    {.name = "MenuSeparatorMargin", .value = "padding(horizontal: 4px)"},
                    {.name = "MenuSeparatorBorderColor", .value = "#00ff00"},
                    {.name = "MenuSeparatorBorderThickness", .value = "1px"},
                    {.name = "MenuSeparatorPadding",
                     .value = "padding(horizontal: 4px, vertical: 2px)"},
                    {.name = "MenuSeparatorTextColor", .value = "#c0c0c0"},

                    {.name = "MenuArrowWidth", .value = "10px"},
                    {.name = "MenuArrowIconColor", .value = "#ffffff"},

                    // PM_IndicatorWidth/Height resolve through the CheckBox component at the
                    // Root element — contextOf() routes the Indicator element there and does
                    // not carry the element across, so the tokens are CheckBoxWidth/Height.
                    {.name = "CheckBoxWidth", .value = "14px"},
                    {.name = "CheckBoxHeight", .value = "14px"},
                },
                {.name = "Menu Geometry"}
            )
        );

        // Registered last so it outranks the fixture above, and left empty so it costs
        // nothing until a test asks for a different value.
        overrides = new Gui::StyleParameters::InMemoryParameterSource(
            {},
            {.name = "Menu Geometry Overrides"}
        );
        Gui::Application::Instance->styleParameterManager()->addSource(overrides);
    }

private:
    Gui::StyleParameters::InMemoryParameterSource* overrides = nullptr;

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

    // A menu option seeded the way QMenu::initStyleOption does for a plain text action.
    static QStyleOptionMenuItem plainItem(const QMenu& menu)
    {
        QStyleOptionMenuItem option;
        option.initFrom(&menu);
        option.menuItemType = QStyleOptionMenuItem::Normal;
        option.checkType = QStyleOptionMenuItem::NotCheckable;
        option.checked = false;
        option.menuHasCheckableItems = false;
        option.maxIconWidth = 0;
        option.reservedShortcutWidth = 0;
        option.text = QStringLiteral("Open");
        option.rect = QRect();
        return option;
    }

private Q_SLOTS:

    // QMenu asks the style for these before it lays anything out, and QMenu::paintEvent then
    // clips each CE_MenuItem call to its own action rect — so this is the only channel a
    // style has for insetting menu items from the popup edge. Panel width stays 0 so QMenu
    // skips its PE_FrameMenu pass and the border PE_PanelMenu paints is the only one.
    void test_popupMarginsComeFromTheMenuTokens()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QCOMPARE(style.pixelMetric(QStyle::PM_MenuPanelWidth, nullptr, &menu), 0);
        QCOMPARE(style.pixelMetric(QStyle::PM_MenuHMargin, nullptr, &menu), menuBorder + menuPadding);
        QCOMPARE(style.pixelMetric(QStyle::PM_MenuVMargin, nullptr, &menu), menuBorder + menuPadding);
        QCOMPARE(style.pixelMetric(QStyle::PM_SubMenuOverlap, nullptr, &menu), 0);
    }

    // The metrics are menu-scoped: a widget that is not a QMenu must keep whatever the base
    // style says, or every other popup in the application shifts.
    void test_nonMenuWidgetsKeepTheBaseStyleMargins()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QWidget plain;

        // FreeCADStyle proxies Fusion, so Fusion is the baseline a non-menu widget must
        // still see — not QApplication::style(), which a platform theme plugin (qt5ct,
        // qt6ct) replaces, making the comparison depend on the developer's environment.
        const std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("Fusion")));
        QVERIFY(fusion != nullptr);

        QCOMPARE(
            style.pixelMetric(QStyle::PM_MenuHMargin, nullptr, &plain),
            fusion->pixelMetric(QStyle::PM_MenuHMargin, nullptr, &plain)
        );
        QCOMPARE(
            style.pixelMetric(QStyle::PM_MenuVMargin, nullptr, &plain),
            fusion->pixelMetric(QStyle::PM_MenuVMargin, nullptr, &plain)
        );
    }

    // PE_PanelMenu is drawn first, over the whole widget rect and unclipped, so it is where
    // the surface belongs. CE_MenuEmptyArea then runs last over whatever region the items
    // left and must not repaint anything.
    void test_panelMenuPaintsTheWholePopupSurface()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;
        menu.resize(120, 60);

        QImage canvas(menu.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);

        QStyleOption option;
        option.initFrom(&menu);
        option.rect = QRect(QPoint(), menu.size());

        QPainter painter(&canvas);
        style.drawPrimitive(QStyle::PE_PanelMenu, &option, &painter, &menu);
        painter.end();

        // Centre is the background token; the outermost pixel is the border token.
        QCOMPARE(canvas.pixelColor(60, 30), QColor(QStringLiteral("#202020")));
        QCOMPARE(canvas.pixelColor(0, 0), QColor(QStringLiteral("#000000")));
    }

    // REGRESSION GUARD, not a red-first test: Fusion's CE_MenuEmptyArea is already a no-op,
    // so this passes before the handler exists and cannot be made to fail first. It is here
    // to lock in that the empty area never paints over the surface PE_PanelMenu now owns.
    void test_emptyAreaLeavesThePanelUntouched()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;
        menu.resize(120, 60);

        QImage canvas(menu.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuItemType = QStyleOptionMenuItem::EmptyArea;
        option.rect = QRect(QPoint(), menu.size());

        QPainter painter(&canvas);
        style.drawControl(QStyle::CE_MenuEmptyArea, &option, &painter, &menu);
        painter.end();

        QCOMPARE(canvas.pixelColor(60, 30), QColor(Qt::magenta));
    }

    // Each column is reserved only under its own condition, and costs exactly its width plus
    // one MenuItemIconSpacing gap. Deltas keep this independent of the test font.
    void test_eachColumnCostsItsWidthPlusOneGap()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        const QStyleOptionMenuItem plain = plainItem(menu);
        const auto widthOf = [&style, &menu](const QStyleOptionMenuItem& option) {
            return style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu).width();
        };
        const int base = widthOf(plain);

        QStyleOptionMenuItem checkable = plain;
        checkable.menuHasCheckableItems = true;
        QCOMPARE(widthOf(checkable) - base, indicatorSize + iconSpacing);

        // maxIconWidth is Qt's hardcoded PM_SmallIconSize + 4; only its non-zero answer is
        // used, and the column itself comes from MenuIconSize.
        QStyleOptionMenuItem withIcon = plain;
        withIcon.maxIconWidth = 20;
        QCOMPARE(widthOf(withIcon) - base, iconSize + iconSpacing);

        QStyleOptionMenuItem submenu = plain;
        submenu.menuItemType = QStyleOptionMenuItem::SubMenu;
        QCOMPARE(widthOf(submenu) - base, arrowWidth + iconSpacing);
    }

    // Qt adds reservedShortcutWidth to max_column_width itself, after sizing every item.
    // Adding it here too is what makes stock Fusion menus so wide; only the gap is ours.
    void test_shortcutContributesItsGapButNotItsWidth()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem plain = plainItem(menu);
        const int base = style.sizeFromContents(QStyle::CT_MenuItem, &plain, QSize(), &menu).width();

        QStyleOptionMenuItem withShortcut = plain;
        withShortcut.text = QStringLiteral("Open\tCtrl+O");
        withShortcut.reservedShortcutWidth = 77;
        const int shortcutWidth
            = style.sizeFromContents(QStyle::CT_MenuItem, &withShortcut, QSize(), &menu).width();

        // The label is the same in both, so the whole delta is the gap — never the 77px.
        QCOMPARE(shortcutWidth - base, shortcutSpacing);
    }

    // Qt stacks action rects with a bare y += height() and has no spacing metric, so the gap
    // has to be built into the row height. It is split half above and half below because
    // CT_MenuItem runs before Qt positions anything and cannot tell a first row from any other.
    void test_itemSpacingAddsToTheRowHeight()  // NOLINT
    {
        QMenu menu;
        QStyleOptionMenuItem option = plainItem(menu);

        const int tight = [&] {
            Gui::FreeCADStyle freecadStyle;
            QStyle& style = freecadStyle;
            return style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu).height();
        }();

        const auto guard = overrideToken("MenuItemSpacing", "6px");

        // A fresh style: FreeCADStyle caches resolved box geometry per instance, and only
        // clearTokenCache() drops it — which fires from eventFilter() on ThemeReloadEvent,
        // an event a bare style instance like this one never receives. Reusing the instance
        // above would measure the pre-override geometry straight out of its cache.
        Gui::FreeCADStyle spacedFreecadStyle;
        QStyle& spacedStyle = spacedFreecadStyle;
        const int spaced
            = spacedStyle.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu).height();

        QCOMPARE(spaced - tight, 6);
    }

    void test_plainSeparatorUsesItsOwnHeight()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuItemType = QStyleOptionMenuItem::Separator;
        option.text = QString();

        // Qt seeds separators at {2,2}; the style replaces that outright.
        QCOMPARE(
            style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(2, 2), &menu).height(),
            separatorHeight
        );
    }

    // The guard against the whole class of bug where the size hint and the paint code drift
    // apart: give the item exactly the width the hint asked for, then check every column
    // still fits, in order, without overlapping its neighbour.
    void test_layoutFitsInsideTheWidthTheSizeHintAsked()  // NOLINT
    {
        ProbeStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuItemType = QStyleOptionMenuItem::SubMenu;
        option.menuHasCheckableItems = true;
        option.checkType = QStyleOptionMenuItem::NonExclusive;
        option.maxIconWidth = 20;
        option.text = QStringLiteral("Export as\tCtrl+Shift+E");
        option.reservedShortcutWidth = 77;

        const QSize hint = style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu);

        // QMenuPrivate::updateActionRects() widens every row to max_column_width and then
        // adds tabWidth once, globally. Reproduce that here or the shortcut has no room.
        option.rect = QRect(0, 0, hint.width() + option.reservedShortcutWidth, hint.height());

        const auto layout = freecadStyle.menuItemLayout(&option, &menu);
        QVERIFY(layout.has_value());

        QVERIFY(!layout->indicator.isNull());
        QVERIFY(!layout->icon.isNull());
        QVERIFY(!layout->shortcut.isNull());
        QVERIFY(!layout->arrow.isNull());

        QVERIFY(layout->indicator.right() < layout->icon.left());
        QVERIFY(layout->icon.right() < layout->text.left());
        QVERIFY(layout->text.right() < layout->shortcut.left());
        QVERIFY(layout->shortcut.right() < layout->arrow.left());

        QVERIFY(option.rect.contains(layout->indicator));
        QVERIFY(option.rect.contains(layout->arrow));

        // The label keeps at least the width it was measured at, so nothing elides that the
        // size hint claimed would fit.
        const QFontMetrics metrics(option.font);
        const int measured
            = metrics.boundingRect(QRect(), Qt::TextShowMnemonic, QStringLiteral("Export as")).width();
        QVERIFY2(
            layout->text.width() >= measured,
            qPrintable(
                QStringLiteral("text rect %1px, label needs %2px").arg(layout->text.width()).arg(measured)
            )
        );
    }

    // Columns a plain item does not have leave null rects, so the caller can test for them
    // rather than reasoning about zero-width geometry.
    void test_plainItemReservesNothingButItsLabel()  // NOLINT
    {
        ProbeStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.rect
            = QRect(QPoint(), style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu));

        const auto layout = freecadStyle.menuItemLayout(&option, &menu);
        QVERIFY(layout.has_value());
        QVERIFY(layout->indicator.isNull());
        QVERIFY(layout->icon.isNull());
        QVERIFY(layout->shortcut.isNull());
        QVERIFY(layout->arrow.isNull());
        QVERIFY(!layout->text.isNull());
    }

    // The walk is written left-to-right and mirrored as a block, so right-to-left has to put
    // the leading column on the right without any per-part special casing.
    void test_rightToLeftMirrorsTheWholeWalk()  // NOLINT
    {
        ProbeStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuHasCheckableItems = true;
        option.checkType = QStyleOptionMenuItem::NonExclusive;
        option.maxIconWidth = 20;
        option.direction = Qt::RightToLeft;
        option.rect
            = QRect(QPoint(), style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu));

        const auto layout = freecadStyle.menuItemLayout(&option, &menu);
        QVERIFY(layout.has_value());

        // Leading column is now on the right, and the order reverses.
        QVERIFY(layout->indicator.left() > layout->icon.left());
        QVERIFY(layout->icon.left() > layout->text.left());
    }
};

QTEST_MAIN(TestMenuGeometry)
#include "MenuGeometry.moc"
