// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QTest>
#include <QWidget>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>

class TestStyleTransparency: public QObject
{
    Q_OBJECT

public:
    TestStyleTransparency()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        // Attached to a plain QWidget through the "component" property, so these tokens
        // apply without needing a real QListView or QTreeView.
        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "ListIsTransparent", .value = "false"},
                    {.name = "TreeIsTransparent", .value = "true"},
                },
                {.name = "Transparency Fixture"}
            )
        );
    }

private Q_SLOTS:

    // A widget that declares IsTransparent=false still renders transparent itself;
    // only what it presents downward changes.
    void test_tokenBreaksChainBelowButNotForItself()  // NOLINT
    {
        QWidget root;
        auto* passthrough = new QWidget(&root);
        auto* breaker = new QWidget(passthrough);
        breaker->setProperty("component", "List");
        auto* child = new QWidget(breaker);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, true);

        QVERIFY(Gui::FreeCADStyle::isTransparent(&root));
        QVERIFY(Gui::FreeCADStyle::isTransparent(passthrough));
        QVERIFY(Gui::FreeCADStyle::isTransparent(breaker));
        QVERIFY(!Gui::FreeCADStyle::isTransparent(child));
    }

    // IsTransparent=true opens a root for the subtree, not for the declaring widget.
    void test_tokenOpensRootForSubtreeOnly()  // NOLINT
    {
        QWidget root;
        auto* opener = new QWidget(&root);
        opener->setProperty("component", "Tree");
        auto* child = new QWidget(opener);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, false);

        QVERIFY(!Gui::FreeCADStyle::isTransparent(&root));
        QVERIFY(!Gui::FreeCADStyle::isTransparent(opener));
        QVERIFY(Gui::FreeCADStyle::isTransparent(child));
    }

    // The property, unlike the token, seeds the carrying widget itself.
    void test_propertySeedsWidgetItself()  // NOLINT
    {
        QWidget root;
        auto* panel = new QWidget(&root);
        panel->setProperty("transparent", true);
        auto* child = new QWidget(panel);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, false);

        QVERIFY(!Gui::FreeCADStyle::isTransparent(&root));
        QVERIFY(Gui::FreeCADStyle::isTransparent(panel));
        QVERIFY(Gui::FreeCADStyle::isTransparent(child));
    }

    void test_flippingPropertyRepropagates()  // NOLINT
    {
        QWidget root;
        auto* panel = new QWidget(&root);
        panel->setProperty("transparent", true);
        auto* child = new QWidget(panel);

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, false);
        QVERIFY(Gui::FreeCADStyle::isTransparent(child));

        panel->setProperty("transparent", false);
        style.updateTransparency(&root, false);

        QVERIFY(!Gui::FreeCADStyle::isTransparent(panel));
        QVERIFY(!Gui::FreeCADStyle::isTransparent(child));
    }

    // polish() must seed a widget from what its parent presents downward
    // (transparencyBelow()), not from what the parent renders with (isTransparent()) —
    // the two disagree here precisely because breaker is a chain-breaker.
    void test_polishSeedsFromTransparencyBelowNotIsTransparent()  // NOLINT
    {
        QWidget root;
        auto* breaker = new QWidget(&root);
        breaker->setProperty("component", "List");

        Gui::FreeCADStyle style;
        style.updateTransparency(&root, true);
        QVERIFY(Gui::FreeCADStyle::isTransparent(breaker));

        // Simulate a widget built after the subtree was propagated — e.g. a lazily created
        // editor — whose only transparency signal comes from polish().
        auto* child = new QWidget(breaker);
        style.polish(child);

        QVERIFY(!Gui::FreeCADStyle::isTransparent(child));
    }
};

QTEST_MAIN(TestStyleTransparency)

#include "StyleTransparency.moc"
