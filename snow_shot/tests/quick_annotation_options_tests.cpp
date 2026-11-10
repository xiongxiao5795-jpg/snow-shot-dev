#include "../src/presentation/quickannotation/quickannotationoptions.h"

#include <cstdlib>
#include <iostream>

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
                defaults.holdMs == 1500,
            "independent default appearance and 1.5 second hold");
    const auto configured =
        QuickAnnotationOptions::fromJson({{QStringLiteral("color"), QStringLiteral("#1890ff")},
                                          {QStringLiteral("width"), 7},
                                          {QStringLiteral("hold_ms"), 3000}});
    const auto restored = QuickAnnotationOptions::fromJson(configured.toJson());
    require(restored.color == QColor(QStringLiteral("#1890ff")) && restored.width == 7 &&
                restored.holdMs == 3000,
            "settings round trip");
    const auto invalid =
        QuickAnnotationOptions::fromJson({{QStringLiteral("color"), QStringLiteral("invalid")},
                                          {QStringLiteral("width"), 100},
                                          {QStringLiteral("hold_ms"), -1}});
    require(invalid.color == defaults.color && invalid.width == 20 && invalid.holdMs == 500,
            "invalid options fall back or remain within supported limits");
}
