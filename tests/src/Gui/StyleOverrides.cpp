// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QApplication>
#include <QTest>
#include <QWidget>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>
#include <Gui/StyleParameters/StyleContext.h>
#include <Gui/StyleParameters/StyleOverrides.h>

using Gui::StyleParameters::OverrideRegistry;
using Gui::StyleParameters::OverrideSet;

class TestStyleOverrides: public QObject
{
    Q_OBJECT

public:
    TestStyleOverrides()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        // Attached to plain QWidgets through the "component" property, so no real QTabBar or
        // QTreeView is needed. TestPanelBackground goes through TestPaneBackground so the
        // nested-reference path is what the assertions actually exercise.
        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "TestPaneBackground", .value = "#112233"},
                    {.name = "TestPanelBackground", .value = "@TestPaneBackground"},
                },
                {.name = "Style Overrides Fixture"}
            )
        );

        // The production entry points read the widget's own style, so the application style
        // has to be the one under test.
        QApplication::setStyle(new Gui::FreeCADStyle);
    }

private:
    static Gui::FreeCADStyle* style()
    {
        return qobject_cast<Gui::FreeCADStyle*>(QApplication::style());
    }

    static Gui::StyleParameters::ParameterManager* manager()
    {
        return Gui::Application::Instance->styleParameterManager();
    }

    /// The colour a widget's Background token resolves to, through the full production path.
    static QColor backgroundOf(const QWidget* widget)
    {
        return style()->resolveBoxStyle(Gui::FreeCADStyle::contextOf(widget)).background.color();
    }

    /// A panel whose Background comes from TestPanelBackground → TestPaneBackground.
    static QWidget* makePanel(QWidget* parent)
    {
        auto* panel = new QWidget(parent);
        panel->setProperty("component", "TestPanel");
        return panel;
    }

private Q_SLOTS:

    void test_aWidgetWithNoOverridesResolvesTheThemeValue()  // NOLINT
    {
        QWidget root;
        QWidget* panel = makePanel(&root);

        QCOMPARE(backgroundOf(panel), QColor(0x11, 0x22, 0x33));
    }

    void test_aStoredSetChangesWhatTheWidgetResolves()  // NOLINT
    {
        QWidget root;
        QWidget* panel = makePanel(&root);

        const uint32_t identifier = manager()->overrideRegistry().intern(
            {{"TestPaneBackground", "#445566"}}
        );
        panel->setProperty(Gui::FreeCADStyle::overrideSetProperty, identifier);

        QCOMPARE(backgroundOf(panel), QColor(0x44, 0x55, 0x66));
    }

    // If the token cache were still one flat map, whichever widget painted first would decide
    // the colour for the other. Resolving both in one test is what catches that.
    void test_twoSetsDoNotShareCacheEntries()  // NOLINT
    {
        QWidget root;
        QWidget* first = makePanel(&root);
        QWidget* second = makePanel(&root);

        first->setProperty(
            Gui::FreeCADStyle::overrideSetProperty,
            manager()->overrideRegistry().intern({{"TestPaneBackground", "#445566"}})
        );
        second->setProperty(
            Gui::FreeCADStyle::overrideSetProperty,
            manager()->overrideRegistry().intern({{"TestPaneBackground", "#778899"}})
        );

        QCOMPARE(backgroundOf(first), QColor(0x44, 0x55, 0x66));
        QCOMPARE(backgroundOf(second), QColor(0x77, 0x88, 0x99));

        // ...and again in the opposite order, now that both are cached.
        QCOMPARE(backgroundOf(second), QColor(0x77, 0x88, 0x99));
        QCOMPARE(backgroundOf(first), QColor(0x44, 0x55, 0x66));
    }

    // An overridden widget must not leave its colour in the bin every other widget reads.
    void test_anOverriddenWidgetDoesNotAffectAPlainOne()  // NOLINT
    {
        QWidget root;
        QWidget* overridden = makePanel(&root);
        QWidget* plain = makePanel(&root);

        overridden->setProperty(
            Gui::FreeCADStyle::overrideSetProperty,
            manager()->overrideRegistry().intern({{"TestPaneBackground", "#445566"}})
        );

        QCOMPARE(backgroundOf(overridden), QColor(0x44, 0x55, 0x66));
        QCOMPARE(backgroundOf(plain), QColor(0x11, 0x22, 0x33));
    }
};

QTEST_MAIN(TestStyleOverrides)

#include "StyleOverrides.moc"
