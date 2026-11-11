#pragma once

#include "quickannotationoptions.h"
#include "snow_shot/presentation/components/settingscustomwidget.h"
#include "snow_shot/presentation/components/settingspageutils.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/configurationstore.h"
#include "widgets/color_picker.h"
#include "widgets/input_number.h"
#include "widgets/select.h"
#include "widgets/switch.h"

#include <QCoreApplication>
#include <QSignalBlocker>
#include <QVBoxLayout>

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
        m_width = number("quickAnnotationSettingsWidth", 1, 20, 1, 0);
        m_shadow = number("quickAnnotationSettingsShadow", 0, 25, 1, 0);
        m_hold = number("quickAnnotationSettingsHold", 0.5, 30, 0.5, 1);
        m_fade = number("quickAnnotationSettingsFade", 0.1, 5, 0.1, 1);
        m_showColor = toggle("quickAnnotationSettingsShowColor");
        m_showWidth = toggle("quickAnnotationSettingsShowWidth");
        m_showHold = toggle("quickAnnotationSettingsShowHold");
        const styles::ThemeAliasMetricToken metric;
        int rowIndex = 0;
        for (QWidget* control : QVector<QWidget*>{m_tool, m_color, m_width, m_shadow, m_hold,
                                                  m_fade, m_showColor, m_showWidth, m_showHold}) {
            if (rowIndex++ == 6) {
                m_toolbarHeading = new QLabel(this);
                QFont font = m_toolbarHeading->font();
                font.setBold(true);
                m_toolbarHeading->setFont(font);
                layout->addWidget(m_toolbarHeading);
            }
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
        connect(m_width, &adqt::widgets::AdInputNumber::valueChanged, this, [this](double value) {
            save([&](auto& options) { options.width = qRound(value); });
        });
        connect(m_shadow, &adqt::widgets::AdInputNumber::valueChanged, this, [this](double value) {
            save([&](auto& options) { options.shadowRadius = qRound(value); });
        });
        connect(m_hold, &adqt::widgets::AdInputNumber::valueChanged, this, [this](double value) {
            save([&](auto& options) { options.holdMs = qRound(value * 1000); });
        });
        connect(m_fade, &adqt::widgets::AdInputNumber::valueChanged, this, [this](double value) {
            save([&](auto& options) { options.fadeMs = qRound(value * 1000); });
        });
        connect(m_showColor, &QAbstractButton::toggled, this,
                [this](bool value) { save([&](auto& options) { options.showColor = value; }); });
        connect(m_showWidth, &QAbstractButton::toggled, this,
                [this](bool value) { save([&](auto& options) { options.showWidth = value; }); });
        connect(m_showHold, &QAbstractButton::toggled, this,
                [this](bool value) { save([&](auto& options) { options.showHold = value; }); });
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
                                QT_TRANSLATE_NOOP("SettingsCatalog", "Stroke width"),
                                QT_TRANSLATE_NOOP("SettingsCatalog", "Shadow radius"),
                                QT_TRANSLATE_NOOP("SettingsCatalog", "Hold time"),
                                QT_TRANSLATE_NOOP("SettingsCatalog", "Fade duration"),
                                QT_TRANSLATE_NOOP("SettingsCatalog", "Toolbar color control"),
                                QT_TRANSLATE_NOOP("SettingsCatalog", "Toolbar width control"),
                                QT_TRANSLATE_NOOP("SettingsCatalog", "Toolbar hold-time control")};
        const char* descriptions[] = {
            QT_TRANSLATE_NOOP("SettingsCatalog", "Tool selected when quick annotation starts"),
            QT_TRANSLATE_NOOP("SettingsCatalog", "Color used for new marks"),
            QT_TRANSLATE_NOOP("SettingsCatalog", "Width of new marks"),
            QT_TRANSLATE_NOOP("SettingsCatalog", "Shadow range around marks, from 0 to 25 pt"),
            QT_TRANSLATE_NOOP("SettingsCatalog", "How long temporary marks remain visible"),
            QT_TRANSLATE_NOOP("SettingsCatalog", "How long marks take to fade out"),
            QT_TRANSLATE_NOOP("SettingsCatalog", "Show the color control in the toolbar"),
            QT_TRANSLATE_NOOP("SettingsCatalog", "Show the width control in the toolbar"),
            QT_TRANSLATE_NOOP("SettingsCatalog", "Show the hold-time control in the toolbar")};
        for (qsizetype i = 0; i < m_titles.size(); ++i) {
            m_titles.at(i)->setText(QCoreApplication::translate("SettingsCatalog", titles[i]));
            m_descriptions.at(i)->setText(
                QCoreApplication::translate("SettingsCatalog", descriptions[i]));
        }
        m_toolbarHeading->setText(
            QCoreApplication::translate("SettingsCatalog", "Toolbar controls"));
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
        m_shadow->setSuffixText(QStringLiteral(" pt"));
        m_hold->setSuffixText(QCoreApplication::translate(
            "snow_shot::presentation::QuickAnnotationController", " s"));
        m_fade->setSuffixText(QCoreApplication::translate(
            "snow_shot::presentation::QuickAnnotationController", " s"));
    }

  private:
    adqt::widgets::AdInputNumber* number(const char* name, double minimum, double maximum,
                                         double step, int decimals) {
        auto* value = new adqt::widgets::AdInputNumber(this);
        value->setObjectName(QString::fromLatin1(name));
        value->setRange(minimum, maximum);
        value->setSingleStep(step);
        value->setDecimals(decimals);
        value->setFixedWidth(160);
        return value;
    }
    adqt::widgets::AdSwitch* toggle(const char* name) {
        auto* value = new adqt::widgets::AdSwitch(this);
        value->setObjectName(QString::fromLatin1(name));
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
        const QSignalBlocker a(m_tool), b(m_color), c(m_width), d(m_shadow), e(m_hold), f(m_fade),
            g(m_showColor), h(m_showWidth), i(m_showHold);
        m_tool->setCurrentValue(options.defaultTool);
        m_color->setValue(adqt::widgets::AdColorValue::solid(options.color));
        m_width->setValue(options.width);
        m_shadow->setValue(options.shadowRadius);
        m_hold->setValue(options.holdMs / 1000.0);
        m_fade->setValue(options.fadeMs / 1000.0);
        m_showColor->setChecked(options.showColor);
        m_showWidth->setChecked(options.showWidth);
        m_showHold->setChecked(options.showHold);
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
    adqt::widgets::AdInputNumber* m_width = nullptr;
    adqt::widgets::AdInputNumber* m_shadow = nullptr;
    adqt::widgets::AdInputNumber* m_hold = nullptr;
    adqt::widgets::AdInputNumber* m_fade = nullptr;
    adqt::widgets::AdSwitch* m_showColor = nullptr;
    adqt::widgets::AdSwitch* m_showWidth = nullptr;
    adqt::widgets::AdSwitch* m_showHold = nullptr;
    QLabel* m_toolbarHeading = nullptr;
    QVector<QLabel*> m_titles;
    QVector<QLabel*> m_descriptions;
};
} // namespace snow_shot::presentation
