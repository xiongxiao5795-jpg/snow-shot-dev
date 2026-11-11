#pragma once

#include "quickannotationoptions.h"
#include "snow_shot/presentation/components/settingscustomwidget.h"
#include "snow_shot/presentation/components/settingspageutils.h"
#include "snow_shot/presentation/settings/settingsruntimesession.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/configurationstore.h"
#include "snow_shot/shortcuts/shortcutbinding.h"
#include "widgets/select.h"
#include <QCoreApplication>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace snow_shot::presentation {
class QuickAnnotationHoldSettingsWidget final : public SettingsCustomWidget {
  public:
    explicit QuickAnnotationHoldSettingsWidget(settings::SettingsRuntimeSession& session,
                                               QWidget* parent = nullptr)
        : SettingsCustomWidget(parent), m_session(session) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        const styles::ThemeAliasMetricToken metric;
        m_legacy = new adqt::widgets::AdSelect(this);
        m_legacy->setSearchEnabled(false);
        addRow(layout, metric, m_legacy);
        connect(
            m_legacy, &adqt::widgets::AdSelect::currentValueChanged, this,
            [this](const QVariant& value) {
                auto& config = storage::ApplicationStorage::instance().configuration();
                auto options = QuickAnnotationOptions::fromJson(
                    config.value(QStringLiteral("quick_annotation/options")).toObject());
                options.holdShortcut = value.toString();
                if (!config.setValue(QStringLiteral("quick_annotation/options"), options.toJson()))
                    refresh();
            });
        for (int i = 0; i < 5; ++i) {
            auto* editor = new QKeySequenceEdit(this);
            editor->setMaximumSequenceLength(1);
            editor->setClearButtonEnabled(true);
            editor->setObjectName(QStringLiteral("quickAnnotationHoldKey%1").arg(i));
            m_editors.push_back(editor);
            addRow(layout, metric, editor);
            connect(editor, &QKeySequenceEdit::editingFinished, this, [this, i] { save(i); });
        }
        m_error = new QLabel(this);
        m_error->setWordWrap(true);
        layout->addWidget(m_error);
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
        for (qsizetype i = 0; i < m_titles.size(); ++i)
            components::applySettingItemTheme(m_titles.at(i), m_descriptions.at(i), scheme);
    }
    void retranslateUi() override {
        const char* labels[] = {QT_TRANSLATE_NOOP("SettingsCatalog", "Hold-to-annotate shortcut"),
                                QT_TRANSLATE_NOOP("SettingsCatalog", "Quick annotation"),
                                QT_TRANSLATE_NOOP("SettingsCatalog", "Quick annotation: brush"),
                                QT_TRANSLATE_NOOP("SettingsCatalog", "Quick annotation: arrow"),
                                QT_TRANSLATE_NOOP("SettingsCatalog", "Quick annotation: rectangle"),
                                QT_TRANSLATE_NOOP("SettingsCatalog", "Quick annotation: ellipse")};
        for (qsizetype i = 0; i < m_titles.size(); ++i)
            m_titles.at(i)->setText(QCoreApplication::translate("SettingsCatalog", labels[i]));
        const QSignalBlocker blocker(m_legacy);
        const auto value = m_legacy->currentValue();
        m_legacy->setOptions({{QStringLiteral("command_option"), QStringLiteral("⌘ + ⌥")},
                              {QStringLiteral("command_shift"), QStringLiteral("⌘ + ⇧")},
                              {QStringLiteral("control_option"), QStringLiteral("⌃ + ⌥")},
                              {QStringLiteral("control_shift"), QStringLiteral("⌃ + ⇧")},
                              {QStringLiteral("disabled"),
                               QCoreApplication::translate("SettingsCatalog", "Disabled")}});
        m_legacy->setCurrentValue(value);
    }

  private:
    static QString key(int i) {
        static const char* keys[] = {"default", "brush", "arrow", "rectangle", "ellipse"};
        return QString::fromLatin1(keys[i]);
    }
    void addRow(QVBoxLayout* layout, const styles::ThemeAliasMetricToken& metric,
                QWidget* control) {
        QLabel* title = nullptr;
        QLabel* description = nullptr;
        auto* row = components::createSettingItemRow(this, metric, &title, &description, control,
                                                     QString());
        m_titles.push_back(title);
        m_descriptions.push_back(description);
        layout->addWidget(row);
    }
    void refresh() {
        auto& storage = storage::ApplicationStorage::instance();
        if (!storage.isInitialized())
            return;
        auto& config = storage.configuration();
        const auto options = QuickAnnotationOptions::fromJson(
            config.value(QStringLiteral("quick_annotation/options")).toObject());
        const auto bindings =
            config.value(QStringLiteral("quick_annotation/hold_bindings")).toObject();
        const QSignalBlocker blocker(m_legacy);
        m_legacy->setCurrentValue(options.holdShortcut);
        for (int i = 0; i < m_editors.size(); ++i) {
            const QSignalBlocker editBlocker(m_editors[i]);
            m_editors[i]->setKeySequence(QKeySequence::fromString(bindings.value(key(i)).toString(),
                                                                  QKeySequence::PortableText));
        }
    }
    void save(int i) {
        auto& config = storage::ApplicationStorage::instance().configuration();
        auto bindings = config.value(QStringLiteral("quick_annotation/hold_bindings")).toObject();
        const auto sequence = m_editors[i]->keySequence();
        const QString text = sequence.toString(QKeySequence::PortableText);
        const auto binding = shortcuts::bindingFromPortableText(text);
        bool valid =
            text.isEmpty() ||
            (!binding.portableText.isEmpty() &&
             m_session.validateShortcut(GlobalShortcutAction::QuickAnnotation, binding).supported);
        for (auto it = bindings.begin(); valid && it != bindings.end(); ++it)
            if (it.key() != key(i) && !it.value().toString().isEmpty() &&
                shortcuts::bindingsConflict(
                    binding, shortcuts::bindingFromPortableText(it.value().toString())))
                valid = false;
        if (!valid) {
            m_error->setText(QCoreApplication::translate("SettingsCatalog",
                                                         "Shortcut is invalid or already in use"));
            refresh();
            return;
        }
        m_error->clear();
        if (text.isEmpty())
            bindings.remove(key(i));
        else
            bindings.insert(key(i), binding.portableText);
        if (!config.setValue(QStringLiteral("quick_annotation/hold_bindings"), bindings))
            refresh();
    }
    settings::SettingsRuntimeSession& m_session;
    adqt::widgets::AdSelect* m_legacy = nullptr;
    QVector<QKeySequenceEdit*> m_editors;
    QVector<QLabel*> m_titles;
    QVector<QLabel*> m_descriptions;
    QLabel* m_error = nullptr;
};
} // namespace snow_shot::presentation
