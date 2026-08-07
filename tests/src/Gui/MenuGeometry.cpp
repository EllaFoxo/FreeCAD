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
};

QTEST_MAIN(TestMenuGeometry)
#include "MenuGeometry.moc"
