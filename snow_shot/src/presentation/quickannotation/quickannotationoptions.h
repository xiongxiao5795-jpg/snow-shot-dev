#pragma once

#include <QColor>
#include <QJsonObject>
#include <QString>

#include <algorithm>

namespace snow_shot::presentation {

// This configuration belongs to the fork's quick-annotation module only.
struct QuickAnnotationOptions {
    QColor color{0xf5, 0x22, 0x2d};
    int width = 3;
    int holdMs = 1500;

    static QuickAnnotationOptions fromJson(const QJsonObject& object) {
        QuickAnnotationOptions result;
        const QColor parsedColor(object.value(QStringLiteral("color")).toString());
        if (parsedColor.isValid())
            result.color = parsedColor;
        result.width = std::clamp(object.value(QStringLiteral("width")).toInt(3), 1, 20);
        result.holdMs = std::clamp(object.value(QStringLiteral("hold_ms")).toInt(1500), 500, 30000);
        return result;
    }

    [[nodiscard]] QJsonObject toJson() const {
        return {{QStringLiteral("color"), color.name()},
                {QStringLiteral("width"), width},
                {QStringLiteral("hold_ms"), holdMs}};
    }
};

} // namespace snow_shot::presentation
