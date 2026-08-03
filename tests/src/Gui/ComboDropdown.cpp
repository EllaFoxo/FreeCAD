// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QComboBox>
#include <QListView>
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
};

QTEST_MAIN(TestComboDropdown)

#include "ComboDropdown.moc"
