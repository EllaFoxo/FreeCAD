// SPDX-License-Identifier: LGPL-2.1-or-later

#include <functional>

#include <QHeaderView>
#include <QImage>
#include <QPainter>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QTest>
#include <QTreeView>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>

class TestTreeBranch: public QObject
{
    Q_OBJECT

public:
    TestTreeBranch()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        // Saturated red at full opacity so a painted pixel is unmistakable.
        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "TreeBranchBorderColor", .value = "#ff0000"},
                    {.name = "TreeBranchBorderThickness", .value = "1px"},
                    // Fully opaque, so anything painted under it would be lost rather than
                    // merely tinted — which is what makes the layering testable.
                    {.name = "ListRowAlternateBackground", .value = "#00ff00"},
                },
                {.name = "Branch Fixture"}
            )
        );
    }

private:
    // Paints one depth-1 branch cell of an otherwise ordinary tree and returns the result.
    static QImage paintCell(int cellLeft, const std::function<void(QTreeView&)>& configure = {})
    {
        QTreeView tree;
        tree.setIndentation(20);
        if (configure) {
            configure(tree);
        }

        QImage canvas(40, 24, QImage::Format_ARGB32);
        canvas.fill(Qt::transparent);

        QStyleOptionViewItem option;
        option.rect = QRect(cellLeft, 0, 20, 24);
        option.state = QStyle::State_Enabled | QStyle::State_Item | QStyle::State_Sibling;

        Gui::FreeCADStyle style;
        QPainter painter(&canvas);
        static_cast<QStyle*>(&style)->drawPrimitive(QStyle::PE_IndicatorBranch, &option, &painter, &tree);
        painter.end();

        return canvas;
    }

    static bool hasColour(const QImage& image, const QColor& colour)
    {
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixelColor(x, y) == colour) {
                    return true;
                }
            }
        }
        return false;
    }

private Q_SLOTS:

    // Qt paints the row, then the branch column, then the cells. An opaque row background
    // therefore has to go down first, or it buries the connectors drawn after it — which is
    // what happened to every alternate row of the property editor.
    void test_opaqueAlternateRowDoesNotEraseConnectors()  // NOLINT
    {
        QTreeView tree;
        tree.setIndentation(20);
        tree.resize(200, 100);

        QImage canvas(40, 24, QImage::Format_ARGB32);
        canvas.fill(Qt::transparent);

        QStyleOptionViewItem option;
        option.rect = QRect(20, 0, 20, 24);
        option.state = QStyle::State_Enabled | QStyle::State_Item | QStyle::State_Sibling;
        option.features |= QStyleOptionViewItem::Alternate;

        Gui::FreeCADStyle style;
        auto* asStyle = static_cast<QStyle*>(&style);
        QPainter painter(&canvas);
        asStyle->drawPrimitive(QStyle::PE_PanelItemViewRow, &option, &painter, &tree);
        asStyle->drawPrimitive(QStyle::PE_IndicatorBranch, &option, &painter, &tree);
        asStyle->drawPrimitive(QStyle::PE_PanelItemViewItem, &option, &painter, &tree);
        painter.end();

        QCOMPARE(canvas.pixelColor(30, 0), QColor(Qt::red));
        QVERIFY(hasColour(canvas, QColor(Qt::green)));
    }

    // The token colour reaches the pen, on the pixel column the geometry names.
    void test_tokenColourReachesTheStroke()  // NOLINT
    {
        const QImage painted = paintCell(20);

        QCOMPARE(painted.pixelColor(30, 0), QColor(Qt::red));
    }

    // A tree may decline connectors without the theme knowing about it.
    void test_widgetPropertySuppressesConnectors()  // NOLINT
    {
        const QImage painted = paintCell(20, [](QTreeView& tree) {
            tree.setProperty("branches", false);
        });

        QVERIFY(!hasColour(painted, QColor(Qt::red)));
    }

    // A root item has no parent to reach toward.
    void test_topLevelCellDrawsNothing()  // NOLINT
    {
        const QImage painted = paintCell(0);

        QVERIFY(!hasColour(painted, QColor(Qt::red)));
    }

    // Root decorations hidden means the leading cell is depth 1, which must still draw.
    void test_leadingCellDrawsWhenRootIsNotDecorated()  // NOLINT
    {
        const QImage painted = paintCell(0, [](QTreeView& tree) { tree.setRootIsDecorated(false); });

        QCOMPARE(painted.pixelColor(10, 0), QColor(Qt::red));
    }

    // Scrolling the tree column right by one indentation step moves a depth-1 ancestor's
    // cell to x == 0 -- the same pixel column an unscrolled root cell would occupy. That
    // ancestor still has a parent to reach toward and must keep drawing its guide; only the
    // tree column's own (now off-screen) position identifies the true root cell.
    void test_leadingVisibleGuideDrawsWhenViewIsScrolledHorizontally()  // NOLINT
    {
        QStandardItemModel model(3, 1);
        QTreeView tree;
        tree.setIndentation(20);
        tree.setModel(&model);
        tree.header()->setOffset(20);

        QImage canvas(40, 24, QImage::Format_ARGB32);
        canvas.fill(Qt::transparent);

        QStyleOptionViewItem option;
        option.rect = QRect(0, 0, 20, 24);
        option.state = QStyle::State_Enabled | QStyle::State_Sibling;

        Gui::FreeCADStyle style;
        QPainter painter(&canvas);
        static_cast<QStyle*>(&style)->drawPrimitive(QStyle::PE_IndicatorBranch, &option, &painter, &tree);
        painter.end();

        QCOMPARE(canvas.pixelColor(10, 0), QColor(Qt::red));
    }
};

QTEST_MAIN(TestTreeBranch)

#include "TreeBranch.moc"
