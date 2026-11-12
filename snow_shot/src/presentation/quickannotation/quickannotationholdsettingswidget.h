#pragma once

#include "quickannotationoptions.h"
#include "snow_shot/presentation/components/settingscustomwidget.h"
#include "snow_shot/presentation/components/settingspageutils.h"
#include "snow_shot/presentation/styles/actionrowstyle.h"
#include "snow_shot/presentation/styles/mainwindowcomponenttoken.h"
#include "snow_shot/presentation/settings/settingsruntimesession.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/configurationstore.h"
#include "snow_shot/shortcuts/shortcutbinding.h"
#include "widgets/select.h"
#include <QCoreApplication>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>
#include <QToolTip>
#include <QHBoxLayout>

namespace snow_shot::presentation {
class QuickAnnotationHoldSettingsWidget final : public SettingsCustomWidget {
  public:
    explicit QuickAnnotationHoldSettingsWidget(settings::SettingsRuntimeSession& session,
                                               QWidget* parent = nullptr)
        : SettingsCustomWidget(parent), m_session(session) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(12, 8, 12, 8);
        m_title = new QLabel(this);
        layout->addWidget(m_title, 1);
        m_select = new adqt::widgets::AdSelect(this);
        m_select->setObjectName(QStringLiteral("quickAnnotationHoldShortcut"));
        m_select->setSearchEnabled(false);
        m_select->setFixedWidth(90);
        layout->addWidget(m_select);
        m_key = new QKeySequenceEdit(this);
        m_key->setObjectName(QStringLiteral("quickAnnotationHoldCustomKey"));
        m_key->setMaximumSequenceLength(1);
        m_key->setClearButtonEnabled(true);
        m_key->setFixedWidth(140);
        layout->addWidget(m_key);
        connect(m_select, &adqt::widgets::AdSelect::currentValueChanged, this,
                [this](const QVariant& value) {
                    auto& config = storage::ApplicationStorage::instance().configuration();
                    auto options = QuickAnnotationOptions::fromJson(
                        config.value(QStringLiteral("quick_annotation/options")).toObject());
                    options.holdShortcut = value.toString();
                    auto bindings = config.value(QStringLiteral("quick_annotation/hold_bindings")).toObject();
                    if (value.toString() != QStringLiteral("custom"))
                        bindings.remove(QStringLiteral("default"));
                    config.setValue(QStringLiteral("quick_annotation/hold_bindings"), bindings);
                    config.setValue(QStringLiteral("quick_annotation/options"), options.toJson());
                    m_key->setVisible(value.toString() == QStringLiteral("custom"));
                });
        connect(m_key, &QKeySequenceEdit::editingFinished, this, [this] {
            auto& config = storage::ApplicationStorage::instance().configuration();
            auto bindings = config.value(QStringLiteral("quick_annotation/hold_bindings")).toObject();
            const QString text = m_key->keySequence().toString(QKeySequence::PortableText);
            const auto binding = shortcuts::bindingFromPortableText(text);
            bool valid = text.isEmpty() || (!binding.portableText.isEmpty() &&
                m_session.validateShortcut(GlobalShortcutAction::QuickAnnotation, binding).supported);
            const auto defaults = shortcuts::shortcutBindingsFromJson(
                config.value(QStringLiteral("global_shortcuts/quick_annotation")), true);
            for (const auto& existing : defaults)
                if (!text.isEmpty() && shortcuts::bindingsConflict(binding, existing))
                    valid = false;
            if (!valid) {
                QToolTip::showText(m_key->mapToGlobal(QPoint(0, m_key->height())),
                    QCoreApplication::translate("SettingsCatalog", "Shortcut is invalid or already in use"), m_key);
                refresh();
                return;
            }
            if (text.isEmpty())
                bindings.remove(QStringLiteral("default"));
            else
                bindings.insert(QStringLiteral("default"), binding.portableText);
            config.setValue(QStringLiteral("quick_annotation/hold_bindings"), bindings);
        });
        auto& storage = storage::ApplicationStorage::instance();
        setEnabled(storage.isInitialized());
        if (storage.isInitialized())
            connect(&storage.configuration(), &storage::ConfigurationStore::valueChanged, this,
                    [this](const QString& key, const QJsonValue&) {
                        if (key == QStringLiteral("quick_annotation/hold_bindings") ||
                            key == QStringLiteral("quick_annotation/options"))
                            refresh();
                    });
        retranslateUi();
        refresh();
    }
    void applyTheme(const styles::ThemeColorScheme& scheme) override {
        m_scheme = scheme;
        setFixedHeight(scheme.metricAlias.controlHeightLG + scheme.metricAlias.paddingXXS);
        update();
        QPalette p = m_title->palette();
        p.setColor(QPalette::WindowText, scheme.map.colorText);
        m_title->setPalette(p);
    }
    void retranslateUi() override {
        m_title->setText(QCoreApplication::translate("SettingsCatalog", "Hold-to-annotate shortcut"));
        const QSignalBlocker blocker(m_select);
        const auto value = m_select->currentValue();
        m_select->setOptions({{QStringLiteral("function"), QStringLiteral("fn")},
                              {QStringLiteral("command_option"), QStringLiteral("⌘ + ⌥")},
                              {QStringLiteral("command_shift"), QStringLiteral("⌘ + ⇧")},
                              {QStringLiteral("control_option"), QStringLiteral("⌃ + ⌥")},
                              {QStringLiteral("control_shift"), QStringLiteral("⌃ + ⇧")},
                              {QStringLiteral("custom"), QCoreApplication::translate("SettingsCatalog", "Custom")},
                              {QStringLiteral("disabled"), QCoreApplication::translate("SettingsCatalog", "Disabled")}});
        m_select->setCurrentValue(value);
    }
  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const auto metric = styles::buildMainWindowComponentMetricToken(m_scheme);
        styles::paintActionRow(painter, size(), m_scheme.map, QStringLiteral("normal"), false,
                               false, metric.cardRadius, m_scheme.metricAlias.lineWidth, true,
                               styles::mainWindowBackgroundOpacity(this));
    }
  private:
    void refresh() {
        auto& storage = storage::ApplicationStorage::instance();
        if (!storage.isInitialized())
            return;
        auto& config = storage.configuration();
        const auto options = QuickAnnotationOptions::fromJson(
            config.value(QStringLiteral("quick_annotation/options")).toObject());
        const auto bindings = config.value(QStringLiteral("quick_annotation/hold_bindings")).toObject();
        const QString custom = bindings.value(QStringLiteral("default")).toString();
        const bool hasCustom = options.holdShortcut == QStringLiteral("custom");
        const QSignalBlocker blocker(m_select);
        const QSignalBlocker keyBlocker(m_key);
        m_select->setCurrentValue(hasCustom ? QStringLiteral("custom") : options.holdShortcut);
        m_key->setKeySequence(QKeySequence::fromString(custom, QKeySequence::PortableText));
        m_key->setVisible(hasCustom);
    }
    styles::ThemeColorScheme m_scheme = styles::generateThemeColorScheme();
    settings::SettingsRuntimeSession& m_session;
    QLabel* m_title = nullptr;
    adqt::widgets::AdSelect* m_select = nullptr;
    QKeySequenceEdit* m_key = nullptr;
};
} // namespace snow_shot::presentation
