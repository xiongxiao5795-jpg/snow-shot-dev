#include "quickannotationholdmonitor.h"
#include "snow_shot/presentation/quickannotationstate.h"
#include "snow_shot/shortcuts/shortcutbinding.h"
#include <QKeySequence>
#include <QVector>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <QTimer>
#include <utility>

namespace snow_shot::presentation {
class QuickAnnotationHoldMonitor::Impl final {
  public:
    explicit Impl(std::function<void(bool, const QString&)> callback)
        : changed(std::move(callback)) {
        delay.setSingleShot(true);
        delay.setInterval(180);
        QObject::connect(&delay, &QTimer::timeout, &delay, [this] {
            if (!AXIsProcessTrusted()) {
                state.modifiers(false);
                finish();
                return;
            }
            if (keyPending || state.activate()) {
                keyPending = false;
                active = true;
                watchdog.start();
                changed(true, activeTool);
            }
        });
        watchdog.setInterval(250);
        QObject::connect(&watchdog, &QTimer::timeout, &watchdog, [this] {
            if (activeKey >= 0) {
                if (!CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState,
                                           static_cast<CGKeyCode>(activeKey)) ||
                    !AXIsProcessTrusted())
                    finish();
            } else {
                const auto flags = [NSEvent modifierFlags];
                if (functionKeyDown && (flags & NSEventModifierFlagFunction) == 0)
                    functionKeyDown = false;
                modifiers(flags);
            }
        });
        const NSEventMask mask = NSEventMaskFlagsChanged | NSEventMaskKeyDown | NSEventMaskKeyUp;
        global = [[NSEvent addGlobalMonitorForEventsMatchingMask:mask
                                                         handler:^(NSEvent* event) {
                                                           handle(event);
                                                         }] retain];
        local = [[NSEvent addLocalMonitorForEventsMatchingMask:mask
                                                       handler:^NSEvent*(NSEvent* event) {
                                                         handle(event);
                                                         return event;
                                                       }] retain];
    }
    ~Impl() {
        delay.stop();
        watchdog.stop();
        if (global) {
            [NSEvent removeMonitor:global];
            [global release];
        }
        if (local) {
            [NSEvent removeMonitor:local];
            [local release];
        }
    }
    void setChord(const QString& chord) {
        if (chord == currentChord)
            return;
        currentChord = chord;
        finish();
        state = {};
        if (chord == QStringLiteral("function"))
            required = NSEventModifierFlagFunction;
        else if (chord == QStringLiteral("command_option"))
            required = NSEventModifierFlagCommand | NSEventModifierFlagOption;
        else if (chord == QStringLiteral("command_shift"))
            required = NSEventModifierFlagCommand | NSEventModifierFlagShift;
        else if (chord == QStringLiteral("control_option"))
            required = NSEventModifierFlagControl | NSEventModifierFlagOption;
        else if (chord == QStringLiteral("control_shift"))
            required = NSEventModifierFlagControl | NSEventModifierFlagShift;
        else
            required = 0;
    }

    void setBindings(const QJsonObject& object) {
        delay.stop();
        finish();
        bindings.clear();
        const QStringList tools{QStringLiteral("default"), QStringLiteral("brush"),
                                QStringLiteral("arrow"), QStringLiteral("rectangle"),
                                QStringLiteral("ellipse")};
        for (const auto& tool : tools) {
            const auto binding = shortcuts::bindingFromPortableText(object.value(tool).toString());
            const auto native = shortcuts::macVirtualKeyForBinding(binding);
            if (!native)
                continue;
            const QKeySequence sequence =
                QKeySequence::fromString(binding.portableText, QKeySequence::PortableText);
            if (sequence.count() != 1)
                continue;
            const auto modifiers = sequence[0].keyboardModifiers();
            NSEventModifierFlags flags = 0;
            if (modifiers.testFlag(Qt::ControlModifier))
                flags |= NSEventModifierFlagCommand;
            if (modifiers.testFlag(Qt::MetaModifier))
                flags |= NSEventModifierFlagControl;
            if (modifiers.testFlag(Qt::AltModifier))
                flags |= NSEventModifierFlagOption;
            if (modifiers.testFlag(Qt::ShiftModifier))
                flags |= NSEventModifierFlagShift;
            bindings.push_back({static_cast<int>(*native), flags, tool});
        }
    }

  private:
    struct Binding {
        int key;
        NSEventModifierFlags flags;
        QString tool;
    };
    static constexpr NSEventModifierFlags relevant =
        NSEventModifierFlagCommand | NSEventModifierFlagOption | NSEventModifierFlagControl |
        NSEventModifierFlagShift;
    void handle(NSEvent* event) {
        if (event.type == NSEventTypeKeyUp) {
            if (activeKey == static_cast<int>(event.keyCode))
                finish();
            return;
        }
        if (event.type == NSEventTypeFlagsChanged) {
            functionKeyDown = quickAnnotationFunctionKeyState(
                functionKeyDown, event.keyCode,
                (event.modifierFlags & NSEventModifierFlagFunction) != 0);
            if (activeKey >= 0) {
                if (functionKeyDown || (event.modifierFlags & relevant) != activeFlags)
                    finish();
            } else {
                modifiers(event.modifierFlags);
            }
            return;
        }
        if (event.type != NSEventTypeKeyDown || event.isARepeat)
            return;
        delay.stop();
        if (state.otherKey())
            finish();
        if (!AXIsProcessTrusted() || active || activeKey >= 0 || functionKeyDown)
            return;
        for (const auto& binding : bindings) {
            if (binding.key == static_cast<int>(event.keyCode) &&
                binding.flags == (event.modifierFlags & relevant)) {
                activeKey = binding.key;
                activeFlags = binding.flags;
                activeTool = binding.tool;
                keyPending = true;
                delay.start();
                return;
            }
        }
    }
    void modifiers(NSEventModifierFlags flags) {
        const bool matched =
            required == NSEventModifierFlagFunction
                ? functionKeyDown && (flags & relevant) == 0
                : !functionKeyDown && required != 0 && (flags & relevant) == required;
        const auto transition = state.modifiers(AXIsProcessTrusted() && matched);
        if (transition == QuickAnnotationHoldState::Change::Schedule) {
            activeTool = QStringLiteral("default");
            delay.start();
        } else if (transition == QuickAnnotationHoldState::Change::End)
            finish();
        else if (!matched)
            delay.stop();
    }
    void finish() {
        delay.stop();
        watchdog.stop();
        keyPending = false;
        activeKey = -1;
        activeFlags = 0;
        if (active) {
            active = false;
            changed(false, activeTool);
        }
    }
    std::function<void(bool, const QString&)> changed;
    QVector<Binding> bindings;
    QString activeTool = QStringLiteral("default");
    int activeKey = -1;
    NSEventModifierFlags activeFlags = 0;
    bool keyPending = false;
    bool functionKeyDown = false;
    QuickAnnotationHoldState state;
    QTimer delay;
    QTimer watchdog;
    QString currentChord;
    NSEventModifierFlags required = 0;
    id global = nil;
    id local = nil;
    bool active = false;
};
QuickAnnotationHoldMonitor::QuickAnnotationHoldMonitor(
    std::function<void(bool, const QString&)> changed)
    : m_impl(std::make_unique<Impl>(std::move(changed))) {}
QuickAnnotationHoldMonitor::~QuickAnnotationHoldMonitor() = default;
void QuickAnnotationHoldMonitor::setChord(const QString& chord) {
    m_impl->setChord(chord);
}
void QuickAnnotationHoldMonitor::setBindings(const QJsonObject& bindings) {
    m_impl->setBindings(bindings);
}
} // namespace snow_shot::presentation
