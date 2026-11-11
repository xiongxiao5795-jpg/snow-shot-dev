#include "../src/presentation/quickannotation/quickannotationsettingswidget.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/configurationstore.h"

#include <QApplication>
#include "widgets/color_picker.h"
#include "widgets/slider.h"
#include "widgets/select.h"
#include "widgets/switch.h"
#include <QJsonObject>
#include <QJsonArray>
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
        auto* preset = widget.findChild<adqt::widgets::AdColorPicker*>(
            QStringLiteral("quickAnnotationColorPreset2"));
        auto* width = widget.findChild<adqt::widgets::AdSlider*>(
            QStringLiteral("quickAnnotationSettingsWidth"));
        auto* hold = widget.findChild<adqt::widgets::AdSlider*>(
            QStringLiteral("quickAnnotationSettingsHold"));
        auto* fade = widget.findChild<adqt::widgets::AdSlider*>(
            QStringLiteral("quickAnnotationSettingsFade"));
        require(tool && color && preset && width && hold && fade &&
                    tool->currentValue().toString() == QStringLiteral("rounded_rectangle") &&
                    hold->value() == 0.3 && fade->value() == 0.5 && color->presets().size() == 1 &&
                    color->presets().front().colors.size() == 5,
                "quick annotation settings expose requested defaults");
        tool->setCurrentValue(QStringLiteral("ellipse"));
        color->setValue(adqt::widgets::AdColorValue::solid(QColor(QStringLiteral("#1890ff"))));
        preset->setValue(adqt::widgets::AdColorValue::solid(QColor(QStringLiteral("#abcdef"))));
        width->setValue(7);
        hold->setValue(3.0);
        fade->setValue(1.2);
        const auto key = QStringLiteral("quick_annotation/options");
        auto& configuration = storage.configuration();
        const auto saved = configuration.value(key).toObject();
        require(saved.value(QStringLiteral("default_tool")) == QStringLiteral("ellipse") &&
                    saved.value(QStringLiteral("color")) == QStringLiteral("#1890ff") &&
                    saved.value(QStringLiteral("color_presets")).toArray().at(1).toString() ==
                        QStringLiteral("#abcdef") &&
                    saved.value(QStringLiteral("width")).toInt() == 7 &&
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
