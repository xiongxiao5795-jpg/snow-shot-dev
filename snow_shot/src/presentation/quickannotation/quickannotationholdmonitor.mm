#include "quickannotationholdmonitor.h"
#include "snow_shot/presentation/quickannotationstate.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <QTimer>
#include <utility>

namespace snow_shot::presentation {
class QuickAnnotationHoldMonitor::Impl final {
  public:
    explicit Impl(std::function<void(bool)> callback) : changed(std::move(callback)) {
        delay.setSingleShot(true);
        delay.setInterval(180);
        QObject::connect(&delay, &QTimer::timeout, &delay, [this] {
            if (!AXIsProcessTrusted()) {
                state.modifiers(false);
                return;
            }
            if (state.activate()) {
                active = true;
                watchdog.start();
                changed(true);
            }
        });
        watchdog.setInterval(250);
        QObject::connect(&watchdog, &QTimer::timeout, &watchdog,
                         [this] { modifiers([NSEvent modifierFlags]); });
        const NSEventMask mask = NSEventMaskFlagsChanged | NSEventMaskKeyDown;
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
        delay.stop();
        watchdog.stop();
        if (active) {
            active = false;
            changed(false);
        }
        state = {};
        if (chord == QStringLiteral("command_option"))
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

  private:
    void handle(NSEvent* event) {
        if (event.type == NSEventTypeFlagsChanged)
            modifiers(event.modifierFlags);
        else if (event.type == NSEventTypeKeyDown) {
            delay.stop();
            if (state.otherKey())
                finish();
        }
    }
    void modifiers(NSEventModifierFlags flags) {
        constexpr NSEventModifierFlags relevant =
            NSEventModifierFlagCommand | NSEventModifierFlagOption | NSEventModifierFlagControl |
            NSEventModifierFlagShift;
        const auto transition = state.modifiers(AXIsProcessTrusted() && required != 0 &&
                                                (flags & relevant) == required);
        if (transition == QuickAnnotationHoldState::Change::Schedule)
            delay.start();
        else if (transition == QuickAnnotationHoldState::Change::End)
            finish();
        else if (required == 0 || (flags & relevant) != required)
            delay.stop();
    }
    void finish() {
        delay.stop();
        watchdog.stop();
        if (active) {
            active = false;
            changed(false);
        }
    }
    std::function<void(bool)> changed;
    QuickAnnotationHoldState state;
    QTimer delay;
    QTimer watchdog;
    QString currentChord;
    NSEventModifierFlags required = 0;
    id global = nil;
    id local = nil;
    bool active = false;
};
QuickAnnotationHoldMonitor::QuickAnnotationHoldMonitor(std::function<void(bool)> changed)
    : m_impl(std::make_unique<Impl>(std::move(changed))) {}
QuickAnnotationHoldMonitor::~QuickAnnotationHoldMonitor() = default;
void QuickAnnotationHoldMonitor::setChord(const QString& chord) {
    m_impl->setChord(chord);
}
} // namespace snow_shot::presentation
