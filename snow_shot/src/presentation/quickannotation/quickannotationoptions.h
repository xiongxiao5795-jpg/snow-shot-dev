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
    int fadeMs = 600;
    int shadowRadius = 0;
    QString defaultTool = QStringLiteral("rounded_rectangle");
    bool persistent = false;
    bool showColor = true;
    bool showWidth = true;
    bool showHold = true;

    static QuickAnnotationOptions fromJson(const QJsonObject& object) {
        QuickAnnotationOptions result;
        const QColor parsedColor(object.value(QStringLiteral("color")).toString());
        if (parsedColor.isValid())
            result.color = parsedColor;
        result.width = std::clamp(object.value(QStringLiteral("width")).toInt(3), 1, 20);
        result.holdMs = std::clamp(object.value(QStringLiteral("hold_ms")).toInt(1500), 500, 30000);
        result.fadeMs = std::clamp(object.value(QStringLiteral("fade_ms")).toInt(600), 100, 5000);
        result.shadowRadius =
            std::clamp(object.value(QStringLiteral("shadow_radius")).toInt(0), 0, 25);
        const QString tool = object.value(QStringLiteral("default_tool")).toString();
        if (tool == QStringLiteral("brush") || tool == QStringLiteral("arrow") ||
            tool == QStringLiteral("rectangle") || tool == QStringLiteral("rounded_rectangle") ||
            tool == QStringLiteral("ellipse"))
            result.defaultTool = tool;
        result.persistent = object.value(QStringLiteral("persistent")).toBool(false);
        result.showColor = object.value(QStringLiteral("toolbar_color")).toBool(true);
        result.showWidth = object.value(QStringLiteral("toolbar_width")).toBool(true);
        result.showHold = object.value(QStringLiteral("toolbar_hold")).toBool(true);
        return result;
    }

    [[nodiscard]] QJsonObject toJson() const {
        return {{QStringLiteral("color"), color.name()},
                {QStringLiteral("width"), width},
                {QStringLiteral("hold_ms"), holdMs},
                {QStringLiteral("fade_ms"), fadeMs},
                {QStringLiteral("shadow_radius"), shadowRadius},
                {QStringLiteral("default_tool"), defaultTool},
                {QStringLiteral("persistent"), persistent},
                {QStringLiteral("toolbar_color"), showColor},
                {QStringLiteral("toolbar_width"), showWidth},
                {QStringLiteral("toolbar_hold"), showHold}};
    }
};

} // namespace snow_shot::presentation
