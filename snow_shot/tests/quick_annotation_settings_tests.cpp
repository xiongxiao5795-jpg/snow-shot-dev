#include "../src/presentation/quickannotation/quickannotationsettingswidget.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/configurationstore.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonObject>
#include <QSpinBox>
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
        auto* color = widget.findChild<QComboBox*>(QStringLiteral("quickAnnotationSettingsColor"));
        auto* width = widget.findChild<QSpinBox*>(QStringLiteral("quickAnnotationSettingsWidth"));
        auto* hold =
            widget.findChild<QDoubleSpinBox*>(QStringLiteral("quickAnnotationSettingsHold"));
        auto* fade =
            widget.findChild<QDoubleSpinBox*>(QStringLiteral("quickAnnotationSettingsFade"));
        require(color && width && hold && fade && hold->value() == 1.5 && fade->value() == 0.6,
                "quick annotation settings expose defaults");
        color->setCurrentIndex(3);
        width->setValue(7);
        hold->setValue(3.0);
        fade->setValue(1.2);
        const auto key = QStringLiteral("quick_annotation/options");
        auto& configuration = storage.configuration();
        const auto saved = configuration.value(key).toObject();
        require(saved.value(QStringLiteral("color")) == QStringLiteral("#1890ff") &&
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
