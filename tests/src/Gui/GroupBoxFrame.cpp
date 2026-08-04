// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QGroupBox>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionGroupBox>
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

// QGroupBox::initStyleOption is protected; a using-declaration republishes it so a test can
// build the very option Qt would hand the style.
class ProbeGroupBox: public QGroupBox
{
public:
    using QGroupBox::initStyleOption;
    using QGroupBox::QGroupBox;
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

    // Renders a group box over an unmistakable parent colour, exactly as Qt would.
    static QImage paintGroupBox(ProbeGroupBox& box, QStyleOptionGroupBox& option)
    {
        box.resize(200, 80);
        box.initStyleOption(&option);

        QImage canvas(200, 80, QImage::Format_ARGB32);
        canvas.fill(QColor(0, 0, 255));

        Gui::FreeCADStyle style;
        QPainter painter(&canvas);
        static_cast<QStyle*>(&style)->drawComplexControl(QStyle::CC_GroupBox, &option, &painter, &box);
        painter.end();

        return canvas;
    }

    // The sub-control rects the style itself reports, so probe positions are never guessed.
    static QRect groupBoxRect(
        const QStyleOptionGroupBox& option,
        const QWidget* widget,
        QStyle::SubControl subControl
    )
    {
        Gui::FreeCADStyle style;
        return static_cast<QStyle*>(&style)
            ->subControlRect(QStyle::CC_GroupBox, &option, subControl, widget);
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

    // The label rect has to be measured with the font the title is painted in. Qt measures it
    // from option->fontMetrics, which is the widget's font unless the style substitutes.
    void test_labelRectFollowsTheTokenFontNotTheWidgetFont()  // NOLINT
    {
        Gui::FreeCADStyle style;

        ProbeGroupBox box(QStringLiteral("Title"));
        box.resize(200, 80);
        QFont oversized = box.font();
        oversized.setPixelSize(30);
        box.setFont(oversized);

        QStyleOptionGroupBox option;
        box.initStyleOption(&option);

        const QRect label = static_cast<QStyle*>(&style)->subControlRect(
            QStyle::CC_GroupBox,
            &option,
            QStyle::SC_GroupBoxLabel,
            &box
        );

        QFont titleFont = box.font();
        titleFont.setPixelSize(10);
        titleFont.setWeight(QFont::Weight(600));

        // Two bounds rather than an exact height: QFusionStyle sizes the label from the text's
        // bounding box plus a couple of pixels of its own, and pinning that arithmetic here would
        // test Fusion's padding instead of the substitution this task exists for. Below the widget
        // font's height proves the widget font was not used; at or above the token font's bounding
        // box proves the label is sized for the font the title is painted in.
        QVERIFY(label.height() < QFontMetrics(oversized).height());
        QVERIFY(label.height() >= QFontMetrics(titleFont).boundingRect(box.title()).height());
    }

    // QGroupBox turns SC_GroupBoxContents into its own contents margins, so this is what every
    // layout inside a group box is spaced by.
    void test_untitledContentsMarginsAreExactlyTheTokenPadding()  // NOLINT
    {
        Gui::FreeCADStyle style;

        ProbeGroupBox box;
        box.setStyle(&style);
        box.resize(200, 80);

        QCOMPARE(box.contentsMargins(), QMargins(12, 12, 12, 12));
    }

    // A title hangs into the frame, so contents have to start below it — but only at the top.
    void test_aTitleAddsTopClearanceAndNothingElse()  // NOLINT
    {
        Gui::FreeCADStyle style;

        ProbeGroupBox box(QStringLiteral("Title"));
        box.setStyle(&style);
        box.resize(200, 80);

        const QMargins margins = box.contentsMargins();

        QCOMPARE(margins.left(), 12);
        QCOMPARE(margins.right(), 12);
        QCOMPARE(margins.bottom(), 12);
        QVERIFY(margins.top() > 12);
    }

    // The masking tests below probe at rects the style itself reports, so they hold whether the
    // notch lands on the title or on empty space. This is the geometry that makes them mean
    // something: the frame's top edge has to run through the title band.
    void test_theTitleStraddlesTheFrameTopEdge()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        box.resize(200, 80);
        QStyleOptionGroupBox option;
        box.initStyleOption(&option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);
        const QRect label = groupBoxRect(option, &box, QStyle::SC_GroupBoxLabel);

        // Strictly inside the band on both counts. Merely touching is what an AlignTop vertical
        // alignment produces: the title ends on the frame's first row, so the notch takes the
        // stroke out from under empty space and leaves the top-left corner a detached stub.
        QVERIFY(label.top() < frame.top());
        QVERIFY(frame.top() < label.bottom());

        // Half the title hangs below the edge when it is centred on it; a third allows for
        // rounding without admitting the single-row overlap AlignTop leaves behind.
        QVERIFY(label.bottom() + 1 - frame.top() >= label.height() / 3);
    }

    // The defect this whole change exists for: the stroke has to be absent under the title,
    // not covered by an opaque patch that only matches one background.
    void test_theBorderIsAbsentUnderTheTitle()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        QStyleOptionGroupBox option;
        const QImage canvas = paintGroupBox(box, option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);
        const QRect label = groupBoxRect(option, &box, QStyle::SC_GroupBoxLabel);

        // Three pixels left of the glyphs: inside the notch the 6px title padding opens, and
        // clear of the text itself.
        const int inNotch = label.left() - 3;
        QVERIFY(inNotch > frame.left() + 1);

        QCOMPARE(canvas.pixelColor(frame.left() + 1, frame.top()), QColor(255, 0, 0));

        // No red: the stroke is genuinely gone here, not covered over. Not an exact parent colour —
        // the fill sits half a pixel under where the border was and feathers into this row, which
        // is the fill staying whole under the title exactly as intended.
        QCOMPARE(canvas.pixelColor(inNotch, frame.top()).red(), 0);

        QCOMPARE(canvas.pixelColor(frame.right() - 2, frame.top()), QColor(255, 0, 0));
    }

    // The mask governs the border ring alone, so the box keeps its own surface under the title.
    void test_theFillRunsUnbrokenUnderTheTitle()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        QStyleOptionGroupBox option;
        const QImage canvas = paintGroupBox(box, option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);
        const QRect label = groupBoxRect(option, &box, QStyle::SC_GroupBoxLabel);

        QCOMPARE(canvas.pixelColor(label.left() - 3, frame.top() + 3), QColor(0, 255, 0));
    }

    // A checkable box's indicator sits on the frame edge too, so the notch has to cover it.
    void test_theNotchCoversTheCheckIndicator()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        box.setCheckable(true);
        QStyleOptionGroupBox option;
        const QImage canvas = paintGroupBox(box, option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);
        const QRect indicator = groupBoxRect(option, &box, QStyle::SC_GroupBoxCheckBox);

        QVERIFY(indicator.left() - 3 > frame.left() + 1);

        // No red: the stroke is genuinely gone here, not covered over. See
        // test_theBorderIsAbsentUnderTheTitle for why this isn't an exact parent-colour check.
        QCOMPARE(canvas.pixelColor(indicator.left() - 3, frame.top()).red(), 0);
    }

    // Nothing to mask, so nothing may be cut.
    void test_anUntitledBoxKeepsAnUnbrokenBorder()  // NOLINT
    {
        ProbeGroupBox box;
        QStyleOptionGroupBox option;
        const QImage canvas = paintGroupBox(box, option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);

        for (int x = frame.left() + 1; x < frame.right() - 1; ++x) {
            QCOMPARE(canvas.pixelColor(x, frame.top()), QColor(255, 0, 0));
        }
    }

    // Flat is token data: the top line survives and the other three sides do not.
    void test_aFlatBoxDrawsOnlyItsTopEdge()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        box.setFlat(true);
        QStyleOptionGroupBox option;
        const QImage canvas = paintGroupBox(box, option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);

        QCOMPARE(canvas.pixelColor(frame.right() - 2, frame.top()), QColor(255, 0, 0));
        QVERIFY(canvas.pixelColor(frame.left(), frame.center().y()) != QColor(255, 0, 0));
        QVERIFY(canvas.pixelColor(frame.center().x(), frame.bottom()) != QColor(255, 0, 0));
    }

    // The leading-edge inset the notch relies on only applies to a leading-aligned title;
    // a centred one has to stay centred on the frame, not drift by the padding as well.
    void test_aCentredTitleStaysCentredOnTheFrame()  // NOLINT
    {
        ProbeGroupBox box(QStringLiteral("Title"));
        box.setAlignment(Qt::AlignHCenter);
        box.resize(200, 80);
        QStyleOptionGroupBox option;
        box.initStyleOption(&option);

        const QRect frame = groupBoxRect(option, &box, QStyle::SC_GroupBoxFrame);
        const QRect label = groupBoxRect(option, &box, QStyle::SC_GroupBoxLabel);

        QVERIFY(qAbs(label.center().x() - frame.center().x()) <= 1);
    }
};

QTEST_MAIN(TestGroupBoxFrame)

#include "GroupBoxFrame.moc"
