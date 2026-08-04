// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QTest>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>
#include <Gui/StyleParameters/StyleContext.h>

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

        // Saturated, fully opaque colours so a single pixel probe is unambiguous, and a title
        // font far from any system default so a metric can only have come from the token.
        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "GroupBoxBorderColor", .value = "#ff0000"},
                    {.name = "GroupBoxBorderThickness", .value = "1px"},
                    {.name = "GroupBoxBorderRadius", .value = "0px"},
                    {.name = "GroupBoxBackground", .value = "#00ff00"},
                    {.name = "GroupBoxPadding", .value = "padding(12px)"},
                    {.name = "GroupBoxFlatBorderThickness", .value = "border_thickness(0px, top: 1px)"},
                    {.name = "GroupBoxFlatBorderRadius", .value = "0px"},
                    {.name = "GroupBoxTitlePadding", .value = "padding(horizontal: 6px, vertical: 0)"},
                    {.name = "GroupBoxTitleFontSize", .value = "10px"},
                    {.name = "GroupBoxTitleFontWeight", .value = "600"},
                    {.name = "GroupBoxTitleTextColor", .value = "#0000ff"},
                    // On an unrelated component so it cannot interfere with the GroupBox
                    // fixtures above: exercises the pt-unit branch of resolveFont.
                    {.name = "HeaderFontSize", .value = "12pt"},
                },
                {.name = "GroupBox Fixture"}
            )
        );
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

    // The Flat variant is token data, not painting code: it has to reach a token name that
    // carries the variant fragment, or flat group boxes silently keep the full frame.
    void test_flatVariantKeepsOnlyTheTopBorder()  // NOLINT
    {
        Gui::StyleParameters::StyleContext context;
        context.component = Gui::StyleParameters::StyleComponent::GroupBox;
        context.variant.set(
            Gui::StyleParameters::VariantSlot::FrameType,
            Gui::StyleParameters::FrameType::Flat
        );

        Gui::FreeCADStyle style;
        const Gui::FreeCADStyle::BoxStyleDefinition box = style.resolveBoxStyle(context);

        QVERIFY(box.borderThickness.has_value());
        QCOMPARE(box.borderThickness->top(), 1.0);
        QCOMPARE(box.borderThickness->right(), 0.0);
        QCOMPARE(box.borderThickness->bottom(), 0.0);
        QCOMPARE(box.borderThickness->left(), 0.0);
    }

    // GroupBoxTitlePadding only resolves if the Title element contributes "Title" to the token
    // name. Without the element registered it falls back to the root box padding.
    void test_titleElementResolvesItsOwnPadding()  // NOLINT
    {
        Gui::StyleParameters::StyleContext context;
        context.component = Gui::StyleParameters::StyleComponent::GroupBox;
        context.element = Gui::StyleParameters::StyleComponentElement::Title;

        Gui::FreeCADStyle style;
        const Gui::FreeCADStyle::BoxGeometryDefinition geometry = style.resolveBoxGeometry(context);

        QCOMPARE(geometry.padding.left(), 6.0);
        QCOMPARE(geometry.padding.top(), 0.0);
    }

    // A theme sets the title's size and weight; the rest of the font comes from the caller.
    void test_resolveFontAppliesSizeAndWeightTokens()  // NOLINT
    {
        Gui::StyleParameters::StyleContext context;
        context.component = Gui::StyleParameters::StyleComponent::GroupBox;
        context.element = Gui::StyleParameters::StyleComponentElement::Title;

        QFont base;
        base.setPixelSize(30);
        base.setFamily(QStringLiteral("Some Deliberate Family"));

        Gui::FreeCADStyle style;
        const QFont resolved = style.resolveFont(context, base);

        QCOMPARE(resolved.pixelSize(), 10);
        QCOMPARE(resolved.weight(), QFont::Weight(600));
        QCOMPARE(resolved.family(), QStringLiteral("Some Deliberate Family"));
    }

    // Every caller hands over the widget's own font, so a context with no font tokens has to
    // give it straight back rather than substituting a default.
    void test_resolveFontLeavesTheBaseAloneWithoutTokens()  // NOLINT
    {
        Gui::StyleParameters::StyleContext context;
        context.component = Gui::StyleParameters::StyleComponent::GroupBox;

        QFont base;
        base.setPixelSize(30);
        base.setWeight(QFont::Weight(400));

        Gui::FreeCADStyle style;
        const QFont resolved = style.resolveFont(context, base);

        QCOMPARE(resolved, base);
    }

    // A pt-unit token has to reach setPointSizeF, not the px-only setPixelSize path.
    void test_resolveFontAppliesPointSizeToken()  // NOLINT
    {
        Gui::StyleParameters::StyleContext context;
        context.component = Gui::StyleParameters::StyleComponent::Header;

        QFont base;
        base.setPixelSize(30);

        Gui::FreeCADStyle style;
        const QFont resolved = style.resolveFont(context, base);

        QCOMPARE(resolved.pointSizeF(), 12.0);
    }
};

QTEST_MAIN(TestGroupBoxFrame)

#include "GroupBoxFrame.moc"
