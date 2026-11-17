#include "../src/presentation/quickannotation/quickannotationoptions.h"
#include "../src/presentation/quickannotation/quickannotationeffects.h"

#include <cstdlib>
#include <iostream>

using snow_shot::presentation::QuickAnnotationColorMode;
using snow_shot::presentation::quickAnnotationEffects;
using snow_shot::presentation::QuickAnnotationOptions;

static void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main() {
    const auto defaults = QuickAnnotationOptions::fromJson({});
    require(defaults.color == QColor(0xf5, 0x22, 0x2d) && defaults.width == 3 &&
                defaults.holdMs == 0 && defaults.fadeMs == 500 &&
                defaults.colorMode == QuickAnnotationColorMode::Rainbow &&
                defaults.glowWidth == 1 &&
                defaults.defaultTool == QStringLiteral("rounded_rectangle") &&
                defaults.holdShortcut == QStringLiteral("function") && !defaults.persistent,
            "independent default appearance, hold and fade");
    const auto configured = QuickAnnotationOptions::fromJson(
        {{QStringLiteral("color"), QStringLiteral("#1890ff")},
         {QStringLiteral("width"), 7},
         {QStringLiteral("glow_width"), 4},
         {QStringLiteral("color_mode"), QStringLiteral("random_stroke")},
         {QStringLiteral("hold_ms"), 3000},
         {QStringLiteral("fade_ms"), 1200},
         {QStringLiteral("shadow_radius"), 25},
         {QStringLiteral("default_tool"), QStringLiteral("ellipse")},
         {QStringLiteral("persistent"), true}});
    const auto restored = QuickAnnotationOptions::fromJson(configured.toJson());
    require(restored.color == QColor(QStringLiteral("#1890ff")) && restored.width == 7 &&
                restored.holdMs == 3000 && restored.fadeMs == 1200 && restored.glowWidth == 4 &&
                restored.colorMode == QuickAnnotationColorMode::RandomStroke &&
                restored.defaultTool == QStringLiteral("ellipse") && restored.persistent,
            "settings round trip");
    const auto invalid = QuickAnnotationOptions::fromJson(
        {{QStringLiteral("color"), QStringLiteral("invalid")},
         {QStringLiteral("width"), 100},
         {QStringLiteral("glow_width"), 100},
         {QStringLiteral("color_mode"), QStringLiteral("invalid")},
         {QStringLiteral("hold_ms"), -1},
         {QStringLiteral("fade_ms"), 9000},
         {QStringLiteral("shadow_radius"), 100},
         {QStringLiteral("default_tool"), QStringLiteral("unknown")}});
    require(invalid.color == defaults.color && invalid.width == 20 && invalid.holdMs == 0 &&
                invalid.fadeMs == 3000 && invalid.glowWidth == 5 &&
                invalid.colorMode == QuickAnnotationColorMode::Rainbow &&
                invalid.defaultTool == QStringLiteral("rounded_rectangle"),
            "invalid options fall back or remain within supported limits");

    QImage source(120, 30, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::transparent);
    {
        QPainter painter(&source);
        painter.setPen(QPen(Qt::red, 4));
        painter.drawLine(QPoint(10, 15), QPoint(110, 15));
    }
    const QVector<QPointF> points{QPointF(10, 15), QPointF(110, 15)};
    const auto rainbow =
        quickAnnotationEffects(source, QuickAnnotationColorMode::Rainbow, 0, 0, points, {}, 1, 4);
    const auto random = quickAnnotationEffects(source, QuickAnnotationColorMode::RandomStroke, 80,
                                               0, points, {}, 1, 4);
    const auto glow = quickAnnotationEffects(source, QuickAnnotationColorMode::RandomStroke, 80, 3,
                                             points, {}, 1, 4);
    require(rainbow.pixelColor(15, 15).hue() != rainbow.pixelColor(105, 15).hue(),
            "rainbow changes hue along a stroke");
    require(random.pixelColor(15, 15) == random.pixelColor(105, 15),
            "random stroke uses one color for the whole gesture");
    require(qAlpha(random.pixel(50, 11)) == 0 && qAlpha(glow.pixel(50, 11)) > 0 &&
                qAbs(glow.pixelColor(50, 11).hue() - random.pixelColor(50, 15).hue()) < 10,
            "glow extends the same color beyond the stroke edge");
}
