#include "../src/presentation/quickannotation/quickannotationsettingswidget.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/configurationstore.h"

#include <QApplication>
#include "widgets/color_picker.h"
#include "widgets/input_number.h"
#include "widgets/select.h"
#include "widgets/switch.h"
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

static void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QTemporaryDir temporary;
    auto& storage = snow_shot::storage::ApplicationStorage::instance();
    require(temporary.isValid() &&
                storage.initialize({temporary.path(), temporary.path(), 60000}).success,
            "initialize isolated storage");
    {
        snow_shot::presentation::QuickAnnotationSettingsWidget widget;
        auto* tool = widget.findChild<adqt::widgets::AdSelect*>(
            QStringLiteral("quickAnnotationSettingsTool"));
        auto* color = widget.findChild<adqt::widgets::AdColorPicker*>(
            QStringLiteral("quickAnnotationSettingsColor"));
        auto* width = widget.findChild<adqt::widgets::AdInputNumber*>(
            QStringLiteral("quickAnnotationSettingsWidth"));
        auto* shadow = widget.findChild<adqt::widgets::AdInputNumber*>(
            QStringLiteral("quickAnnotationSettingsShadow"));
        auto* hold = widget.findChild<adqt::widgets::AdInputNumber*>(
            QStringLiteral("quickAnnotationSettingsHold"));
        auto* fade = widget.findChild<adqt::widgets::AdInputNumber*>(
            QStringLiteral("quickAnnotationSettingsFade"));
        require(tool && color && width && shadow && hold && fade &&
                    tool->currentValue().toString() == QStringLiteral("rounded_rectangle") &&
                    hold->value() == 1.5 && fade->value() == 0.6 && shadow->value() == 0,
                "quick annotation settings expose requested defaults");
#ifdef Q_OS_MACOS
        auto* holdShortcut = widget.findChild<adqt::widgets::AdSelect*>(
            QStringLiteral("quickAnnotationSettingsHoldShortcut"));
        require(holdShortcut &&
                    holdShortcut->currentValue().toString() == QStringLiteral("command_option"),
                "hold shortcut defaults to Command+Option");
        holdShortcut->setCurrentValue(QStringLiteral("control_option"));
#endif
        tool->setCurrentValue(QStringLiteral("ellipse"));
        color->setValue(adqt::widgets::AdColorValue::solid(QColor(QStringLiteral("#1890ff"))));
        width->setValue(7);
        shadow->setValue(12);
        hold->setValue(3.0);
        fade->setValue(1.2);
        const auto key = QStringLiteral("quick_annotation/options");
        auto& configuration = storage.configuration();
        const auto saved = configuration.value(key).toObject();
        require(saved.value(QStringLiteral("default_tool")) == QStringLiteral("ellipse") &&
                    saved.value(QStringLiteral("color")) == QStringLiteral("#1890ff") &&
                    saved.value(QStringLiteral("width")).toInt() == 7 &&
                    saved.value(QStringLiteral("shadow_radius")).toInt() == 12 &&
                    saved.value(QStringLiteral("hold_ms")).toInt() == 3000 &&
                    saved.value(QStringLiteral("fade_ms")).toInt() == 1200,
                "settings persist independently");
        auto changed = saved;
        changed.insert(QStringLiteral("width"), 4);
        changed.insert(QStringLiteral("fade_ms"), 800);
        require(configuration.setValue(key, changed) && width->value() == 4 && fade->value() == 0.8,
                "controls follow changes from other entry points");
    }
    storage.shutdown();
}
