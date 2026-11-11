#pragma once

#include "quickannotationoptions.h"
#include "snow_shot/presentation/components/settingscustomwidget.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/configurationstore.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>

namespace snow_shot::presentation {

class QuickAnnotationSettingsWidget final : public SettingsCustomWidget {
  public:
    explicit QuickAnnotationSettingsWidget(QWidget* parent = nullptr)
        : SettingsCustomWidget(parent) {
        auto* form = new QFormLayout(this);
        m_color = new QComboBox(this);
        m_color->setObjectName(QStringLiteral("quickAnnotationSettingsColor"));
        for (const auto* color : {"#f5222d", "#faad14", "#52c41a", "#1890ff", "#000000", "#ffffff"})
            m_color->addItem(QString(), QString::fromLatin1(color));
        m_width = new QSpinBox(this);
        m_width->setObjectName(QStringLiteral("quickAnnotationSettingsWidth"));
        m_width->setRange(1, 20);
        m_hold = new QDoubleSpinBox(this);
        m_hold->setObjectName(QStringLiteral("quickAnnotationSettingsHold"));
        m_hold->setRange(0.5, 30.0);
        m_hold->setSingleStep(0.5);
        m_hold->setDecimals(1);
        m_fade = new QDoubleSpinBox(this);
        m_fade->setObjectName(QStringLiteral("quickAnnotationSettingsFade"));
        m_fade->setRange(0.1, 5.0);
        m_fade->setSingleStep(0.1);
        m_fade->setDecimals(1);
        for (auto** label : {&m_colorLabel, &m_widthLabel, &m_holdLabel, &m_fadeLabel})
            *label = new QLabel(this);
        form->addRow(m_colorLabel, m_color);
        form->addRow(m_widthLabel, m_width);
        form->addRow(m_holdLabel, m_hold);
        form->addRow(m_fadeLabel, m_fade);
        connect(m_color, &QComboBox::currentIndexChanged, this, [this](int) {
            save([this](auto& options) {
                options.color = QColor(m_color->currentData().toString());
            });
        });
        connect(m_width, &QSpinBox::valueChanged, this,
                [this](int width) { save([width](auto& options) { options.width = width; }); });
        connect(m_hold, &QDoubleSpinBox::valueChanged, this, [this](double seconds) {
            save([seconds](auto& options) { options.holdMs = qRound(seconds * 1000.0); });
        });
        connect(m_fade, &QDoubleSpinBox::valueChanged, this, [this](double seconds) {
            save([seconds](auto& options) { options.fadeMs = qRound(seconds * 1000.0); });
        });
        auto& storage = storage::ApplicationStorage::instance();
        setEnabled(storage.isInitialized());
        if (storage.isInitialized()) {
            connect(&storage.configuration(), &storage::ConfigurationStore::valueChanged, this,
                    [this](const QString& key, const QJsonValue&) {
                        if (key == QStringLiteral("quick_annotation/options"))
                            refresh();
                    });
        }
        retranslateUi();
        refresh();
    }

    void applyTheme(const styles::ThemeColorScheme&) override {}
    void retranslateUi() override {
        const auto text = [](const char* source) {
            return QCoreApplication::translate("snow_shot::presentation::QuickAnnotationController",
                                               source);
        };
        m_colorLabel->setText(QCoreApplication::translate("SettingsCatalog", "Default color"));
        m_widthLabel->setText(text("Stroke width"));
        m_holdLabel->setText(text("Hold time in seconds"));
        m_fadeLabel->setText(
            QCoreApplication::translate("SettingsCatalog", "Fade duration in seconds"));
        m_color->setAccessibleName(m_colorLabel->text());
        m_width->setAccessibleName(m_widthLabel->text());
        m_hold->setAccessibleName(m_holdLabel->text());
        m_fade->setAccessibleName(m_fadeLabel->text());
        m_color->setItemText(0, text("Red"));
        m_color->setItemText(1, text("Orange"));
        m_color->setItemText(2, text("Green"));
        m_color->setItemText(3, text("Blue"));
        m_color->setItemText(4, text("Black"));
        m_color->setItemText(5, text("White"));
        m_hold->setSuffix(text(" s"));
        m_fade->setSuffix(text(" s"));
    }

  private:
    void refresh() {
        auto& storage = storage::ApplicationStorage::instance();
        const auto options = storage.isInitialized()
                                 ? QuickAnnotationOptions::fromJson(
                                       storage.configuration()
                                           .value(QStringLiteral("quick_annotation/options"))
                                           .toObject())
                                 : QuickAnnotationOptions{};
        const QSignalBlocker a(m_color), b(m_width), c(m_hold), d(m_fade);
        m_color->setCurrentIndex(qMax(0, m_color->findData(options.color.name())));
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
    QComboBox* m_color = nullptr;
    QSpinBox* m_width = nullptr;
    QDoubleSpinBox* m_hold = nullptr;
    QDoubleSpinBox* m_fade = nullptr;
    QLabel* m_colorLabel = nullptr;
    QLabel* m_widthLabel = nullptr;
    QLabel* m_holdLabel = nullptr;
    QLabel* m_fadeLabel = nullptr;
};
} // namespace snow_shot::presentation
