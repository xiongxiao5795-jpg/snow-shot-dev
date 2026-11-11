#pragma once

#include <QColor>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QString>

#include <algorithm>

namespace snow_shot::presentation {

// This configuration belongs to the fork's quick-annotation module only.
struct QuickAnnotationOptions {
    QColor color{0xf5, 0x22, 0x2d};
    QVector<QColor> colorPresets{QColor(0xf5, 0x22, 0x2d), QColor(0xfa, 0x8c, 0x16),
                                 QColor(0xfa, 0xdb, 0x14), QColor(0x52, 0xc4, 0x1a),
                                 QColor(0x18, 0x90, 0xff)};
    int width = 3;
    int holdMs = 300;
    int fadeMs = 500;
    QString defaultTool = QStringLiteral("rounded_rectangle");
    QString holdShortcut = QStringLiteral("command_option");
    bool persistent = false;
    bool showColor = true;
    bool showWidth = true;
    bool showHold = true;

    static QuickAnnotationOptions fromJson(const QJsonObject& object) {
        QuickAnnotationOptions result;
        const QColor parsedColor(object.value(QStringLiteral("color")).toString());
        if (parsedColor.isValid())
            result.color = parsedColor;
        const auto colors = object.value(QStringLiteral("color_presets")).toArray();
        for (qsizetype i = 0; i < result.colorPresets.size() && i < colors.size(); ++i) {
            const QColor preset(colors.at(i).toString());
            if (preset.isValid())
                result.colorPresets[i] = preset;
        }
        result.width = std::clamp(object.value(QStringLiteral("width")).toInt(3), 1, 20);
        result.holdMs = std::clamp(object.value(QStringLiteral("hold_ms")).toInt(300), 0, 3000);
        result.fadeMs = std::clamp(object.value(QStringLiteral("fade_ms")).toInt(500), 0, 3000);
        const QString tool = object.value(QStringLiteral("default_tool")).toString();
        if (tool == QStringLiteral("brush") || tool == QStringLiteral("arrow") ||
            tool == QStringLiteral("rectangle") || tool == QStringLiteral("rounded_rectangle") ||
            tool == QStringLiteral("ellipse"))
            result.defaultTool = tool;
        const QString hold = object.value(QStringLiteral("hold_shortcut")).toString();
        if (hold == QStringLiteral("disabled") || hold == QStringLiteral("command_option") ||
            hold == QStringLiteral("command_shift") || hold == QStringLiteral("control_option") ||
            hold == QStringLiteral("control_shift"))
            result.holdShortcut = hold;
        result.persistent = object.value(QStringLiteral("persistent")).toBool(false);
        result.showColor = object.value(QStringLiteral("toolbar_color")).toBool(true);
        result.showWidth = object.value(QStringLiteral("toolbar_width")).toBool(true);
        result.showHold = object.value(QStringLiteral("toolbar_hold")).toBool(true);
        return result;
    }

    [[nodiscard]] QJsonObject toJson() const {
        QJsonArray colors;
        for (const auto& preset : colorPresets)
            colors.append(preset.name());
        return {{QStringLiteral("color"), color.name()},
                {QStringLiteral("color_presets"), colors},
                {QStringLiteral("width"), width},
                {QStringLiteral("hold_ms"), holdMs},
                {QStringLiteral("fade_ms"), fadeMs},
                {QStringLiteral("default_tool"), defaultTool},
                {QStringLiteral("hold_shortcut"), holdShortcut},
                {QStringLiteral("persistent"), persistent},
                {QStringLiteral("toolbar_color"), showColor},
                {QStringLiteral("toolbar_width"), showWidth},
                {QStringLiteral("toolbar_hold"), showHold}};
    }
};

} // namespace snow_shot::presentation
