// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QTest>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>

// drawBoxBackground is protected on FreeCADStyle; a using-declaration republishes it so the
// mask can be exercised without going through a widget.
class ProbeStyle: public Gui::FreeCADStyle
{
public:
    using Gui::FreeCADStyle::drawBoxBackground;
};

class TestGroupBoxFrame: public QObject
{
    Q_OBJECT

public:
    TestGroupBoxFrame()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }
    }

private:
    // A red border around a green fill: either one is unmistakable in a single pixel probe.
    static Gui::FreeCADStyle::BoxStyleDefinition borderedBox()
    {
        Gui::FreeCADStyle::BoxStyleDefinition box;
        box.background = QBrush(QColor(0, 255, 0));
        box.borderColor = Gui::FreeCADStyle::BorderColorsPerSide {
            .top = QColor(255, 0, 0),
            .right = QColor(255, 0, 0),
            .bottom = QColor(255, 0, 0),
            .left = QColor(255, 0, 0),
        };
        box.borderThickness = QMarginsF(1, 1, 1, 1);
        return box;
    }

    static QImage paintBoxWithMask(const QPainterPath& mask)
    {
        QImage canvas(40, 20, QImage::Format_ARGB32);
        canvas.fill(Qt::transparent);

        QPainter painter(&canvas);
        ProbeStyle::drawBoxBackground(&painter, QRect(0, 0, 40, 20), borderedBox(), mask);
        painter.end();

        return canvas;
    }

private Q_SLOTS:

    // The whole point of the mask: the stroke is absent inside it, not merely covered up.
    void test_borderMaskCutsTheRingAndSparesTheFill()  // NOLINT
    {
        QPainterPath mask;
        mask.addRect(QRectF(0, 0, 40, 20));

        QPainterPath notch;
        notch.addRect(QRectF(15, 0, 10, 20));

        const QImage canvas = paintBoxWithMask(mask.subtracted(notch));

        QCOMPARE(canvas.pixelColor(5, 0).red(), 255);
        QCOMPARE(canvas.pixelColor(20, 0).red(), 0);
        QCOMPARE(canvas.pixelColor(35, 0).red(), 255);

        // The fill is never confined by the mask, so it runs unbroken under the notch.
        QCOMPARE(canvas.pixelColor(20, 10), QColor(0, 255, 0));
    }

    // Every pre-existing call site relies on the default, so an empty path must mean
    // "unrestricted" rather than "mask everything away".
    void test_anEmptyMaskLeavesTheBorderWhole()  // NOLINT
    {
        const QImage canvas = paintBoxWithMask({});

        QCOMPARE(canvas.pixelColor(5, 0).red(), 255);
        QCOMPARE(canvas.pixelColor(20, 0).red(), 255);
        QCOMPARE(canvas.pixelColor(35, 0).red(), 255);
    }
};

QTEST_MAIN(TestGroupBoxFrame)

#include "GroupBoxFrame.moc"
