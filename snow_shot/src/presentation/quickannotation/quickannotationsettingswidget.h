#pragma once

#include "quickannotationoptions.h"
#include "snow_shot/presentation/components/settingscustomwidget.h"
#include "snow_shot/presentation/components/settingspageutils.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/configurationstore.h"
#include "widgets/color_picker.h"
#include "widgets/slider.h"
#include "widgets/select.h"
#include "widgets/switch.h"

#include <QCoreApplication>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace snow_shot::presentation {

class QuickAnnotationSettingsWidget final : public SettingsCustomWidget {
  public:
    explicit QuickAnnotationSettingsWidget(QWidget* parent = nullptr)
        : SettingsCustomWidget(parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(16);
        m_tool = new adqt::widgets::AdSelect(this);
        m_tool->setObjectName(QStringLiteral("quickAnnotationSettingsTool"));
        m_tool->setSearchEnabled(false);
        m_color = new adqt::widgets::AdColorPicker(this);
        m_color->setObjectName(QStringLiteral("quickAnnotationSettingsColor"));
        m_color->setAlphaChannelEnabled(false);
        auto* presetsHost = new QWidget(this);
        auto* presetsLayout = new QHBoxLayout(presetsHost);
        presetsLayout->setContentsMargins(0, 0, 0, 0);
        presetsLayout->setSpacing(8);
        for (int i = 0; i < 5; ++i) {
            auto* picker = new adqt::widgets::AdColorPicker(presetsHost);
            picker->setObjectName(QStringLiteral("quickAnnotationColorPreset%1").arg(i + 1));
            picker->setAlphaChannelEnabled(false);
            picker->setTriggerTextVisible(false);
            picker->setAllowClear(false);
            presetsLayout->addWidget(picker);
            m_presets.push_back(picker);
            connect(picker, &adqt::widgets::AdColorPicker::valueChanged, this,
                    [this, i](const adqt::widgets::AdColorValue& value) {
                        if (value.isSolid())
                            save(
                                [&](auto& options) { options.colorPresets[i] = value.solidColor; });
                    });
        }
        m_width = slider("quickAnnotationSettingsWidth", 1, 20, 1);
        m_hold = slider("quickAnnotationSettingsHold", 0, 3, 0.1);
        m_fade = slider("quickAnnotationSettingsFade", 0, 3, 0.1);
        const styles::ThemeAliasMetricToken metric;
        QVector<QWidget*> controls{m_tool};
        controls += {m_color, presetsHost, m_width, m_hold, m_fade};
        for (QWidget* control : controls) {
            QLabel* title = nullptr;
            QLabel* description = nullptr;
            auto* row = components::createSettingItemRow(this, metric, &title, &description,
                                                         control, QString());
            m_titles.push_back(title);
            m_descriptions.push_back(description);
            layout->addWidget(row);
        }
        connect(m_tool, &adqt::widgets::AdSelect::currentValueChanged, this,
                [this](const QVariant& value) {
                    save([&](auto& options) { options.defaultTool = value.toString(); });
                });
        connect(m_color, &adqt::widgets::AdColorPicker::valueChanged, this,
                [this](const adqt::widgets::AdColorValue& value) {
                    if (value.isSolid())
                        save([&](auto& options) { options.color = value.solidColor; });
                });
        connect(m_width, &adqt::widgets::AdSlider::valueChanged, this, [this](double value) {
            save([&](auto& options) { options.width = qRound(value); });
        });
        connect(m_hold, &adqt::widgets::AdSlider::valueChanged, this, [this](double value) {
            save([&](auto& options) { options.holdMs = qRound(value * 1000); });
        });
        connect(m_fade, &adqt::widgets::AdSlider::valueChanged, this, [this](double value) {
            save([&](auto& options) { options.fadeMs = qRound(value * 1000); });
        });
        auto& storage = storage::ApplicationStorage::instance();
        setEnabled(storage.isInitialized());
        if (storage.isInitialized())
            connect(&storage.configuration(), &storage::ConfigurationStore::valueChanged, this,
                    [this](const QString& key, const QJsonValue&) {
                        if (key == QStringLiteral("quick_annotation/options"))
                            refresh();
                    });
        retranslateUi();
        refresh();
    }

    void applyTheme(const styles::ThemeColorScheme& scheme) override {
        for (qsizetype i = 0; i < m_titles.size(); ++i)
            components::applySettingItemTheme(m_titles.at(i), m_descriptions.at(i), scheme);
    }
    void retranslateUi() override {
        const char* titles[] = {QT_TRANSLATE_NOOP("SettingsCatalog", "Default tool"),
                                QT_TRANSLATE_NOOP("SettingsCatalog", "Default color"),
                                QT_TRANSLATE_NOOP("SettingsCatalog", "Stroke color presets"),
                                QT_TRANSLATE_NOOP("SettingsCatalog", "Stroke width"),
                                QT_TRANSLATE_NOOP("SettingsCatalog", "Hold time"),
                                QT_TRANSLATE_NOOP("SettingsCatalog", "Fade duration")};
        const char* descriptions[] = {
            QT_TRANSLATE_NOOP("SettingsCatalog", "Tool selected when quick annotation starts"),
            QT_TRANSLATE_NOOP("SettingsCatalog", "Color used for new marks"),
            QT_TRANSLATE_NOOP("SettingsCatalog",
                              "Customize the quick-set buttons for annotation stroke colors"),
            QT_TRANSLATE_NOOP("SettingsCatalog", "Width of new marks"),
            QT_TRANSLATE_NOOP("SettingsCatalog", "How long temporary marks remain visible"),
            QT_TRANSLATE_NOOP("SettingsCatalog", "How long marks take to fade out")};
        for (qsizetype i = 0; i < m_titles.size(); ++i) {
            m_titles.at(i)->setText(QCoreApplication::translate("SettingsCatalog", titles[i]));
            m_descriptions.at(i)->setText(
                QCoreApplication::translate("SettingsCatalog", descriptions[i]));
        }
        for (qsizetype i = 0; i < m_presets.size(); ++i) {
            const auto title =
                QCoreApplication::translate("SettingsCatalog", "Stroke color preset %1").arg(i + 1);
            m_presets[i]->setAccessibleName(title);
            m_presets[i]->setToolTip(title);
        }
        const QSignalBlocker blocker(m_tool);
        const auto selected = m_tool->currentValue();
        m_tool->setOptions(
            {{QStringLiteral("rounded_rectangle"),
              QCoreApplication::translate("SettingsCatalog", "Rounded rectangle")},
             {QStringLiteral("rectangle"),
              QCoreApplication::translate("SettingsCatalog", "Rectangle")},
             {QStringLiteral("ellipse"), QCoreApplication::translate("SettingsCatalog", "Ellipse")},
             {QStringLiteral("arrow"), QCoreApplication::translate("SettingsCatalog", "Arrow")},
             {QStringLiteral("brush"), QCoreApplication::translate("SettingsCatalog", "Brush")}});
        m_tool->setCurrentValue(selected);
    }

  private:
    adqt::widgets::AdSlider* slider(const char* name, double minimum, double maximum, double step) {
        auto* value = new adqt::widgets::AdSlider(this);
        value->setObjectName(QString::fromLatin1(name));
        value->setRange(minimum, maximum);
        value->setSingleStep(step);
        value->setFixedWidth(160);
        return value;
    }
    void refresh() {
        auto& storage = storage::ApplicationStorage::instance();
        const auto options = storage.isInitialized()
                                 ? QuickAnnotationOptions::fromJson(
                                       storage.configuration()
                                           .value(QStringLiteral("quick_annotation/options"))
                                           .toObject())
                                 : QuickAnnotationOptions{};
        const QSignalBlocker a(m_tool), b(m_color), c(m_width), e(m_hold), f(m_fade);
        m_tool->setCurrentValue(options.defaultTool);
        QVector<adqt::widgets::AdColorValue> presetValues;
        for (qsizetype i = 0; i < m_presets.size(); ++i) {
            const QSignalBlocker presetBlocker(m_presets[i]);
            m_presets[i]->setValue(adqt::widgets::AdColorValue::solid(options.colorPresets[i]));
            presetValues.push_back(adqt::widgets::AdColorValue::solid(options.colorPresets[i]));
        }
        m_color->setPresets({{QString(), presetValues, true, QStringLiteral("quick-annotation")}});
        m_color->setValue(adqt::widgets::AdColorValue::solid(options.color));
        m_width->setValue(options.width);
        m_hold->setValue(options.holdMs / 1000.0);
        m_fade->setValue(options.fadeMs / 1000.0);
    }
    template <typename Change> void save(Change change) {
        auto& storage = storage::ApplicationStorage::instance();
        if (!storage.isInitialized())
            return;
        auto& config = storage.configuration();
        auto options = QuickAnnotationOptions::fromJson(
            config.value(QStringLiteral("quick_annotation/options")).toObject());
        change(options);
        if (!config.setValue(QStringLiteral("quick_annotation/options"), options.toJson()))
            refresh();
    }
    adqt::widgets::AdSelect* m_tool = nullptr;
    adqt::widgets::AdColorPicker* m_color = nullptr;
    QVector<adqt::widgets::AdColorPicker*> m_presets;
    adqt::widgets::AdSlider* m_width = nullptr;
    adqt::widgets::AdSlider* m_hold = nullptr;
    adqt::widgets::AdSlider* m_fade = nullptr;
    QVector<QLabel*> m_titles;
    QVector<QLabel*> m_descriptions;
};
} // namespace snow_shot::presentation
