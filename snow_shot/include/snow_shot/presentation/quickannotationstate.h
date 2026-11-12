#pragma once

#include <algorithm>
#include <cstdint>

namespace snow_shot::presentation {

enum class QuickAnnotationTool { Brush, Arrow, Rectangle, RoundedRectangle, Ellipse };

[[nodiscard]] inline bool quickAnnotationControlClickIsLeft(bool holding, bool rightButton,
                                                            bool controlDown, bool physicalLeft) {
    return holding && rightButton && controlDown && physicalLeft;
}

// macOS sets the function modifier on navigation keys too; only key code 63 is Fn.
[[nodiscard]] inline bool quickAnnotationFunctionKeyState(bool previous, unsigned keyCode,
                                                          bool functionFlag) {
    return keyCode == 63 ? functionFlag : previous;
}

// A short hold avoids stealing ordinary shortcuts that start with the same modifiers.
class QuickAnnotationHoldState final {
  public:
    enum class Change { None, Schedule, End };
    Change modifiers(bool matched) {
        if (!matched) {
            const bool wasActive = m_active;
            m_pending = false;
            m_active = false;
            m_cancelled = false;
            return wasActive ? Change::End : Change::None;
        }
        if (!m_active && !m_pending && !m_cancelled) {
            m_pending = true;
            return Change::Schedule;
        }
        return Change::None;
    }
    bool activate() {
        if (!m_pending)
            return false;
        m_pending = false;
        m_active = true;
        return true;
    }
    bool otherKey() {
        const bool wasActive = m_active;
        const bool hadChord = m_active || m_pending;
        m_pending = false;
        m_active = false;
        m_cancelled = m_cancelled || hadChord;
        return wasActive;
    }

  private:
    bool m_pending = false;
    bool m_active = false;
    bool m_cancelled = false;
};

// The host owns windows and marks; this state machine only describes one gesture.
class QuickAnnotationState final {
  public:
    enum class Phase { Idle, Armed, Drawing };

    [[nodiscard]] Phase phase() const {
        return m_phase;
    }
    [[nodiscard]] QuickAnnotationTool tool() const {
        return m_tool;
    }

    bool arm(QuickAnnotationTool tool) {
        if (m_phase == Phase::Drawing)
            return false;
        m_tool = tool;
        m_phase = Phase::Armed;
        return true;
    }

    bool begin() {
        if (m_phase != Phase::Armed)
            return false;
        m_phase = Phase::Drawing;
        return true;
    }

    bool finish() {
        if (m_phase != Phase::Drawing)
            return false;
        m_phase = Phase::Idle;
        return true;
    }

    void cancel() {
        m_phase = Phase::Idle;
    }

    // Input is elapsed monotonic milliseconds since the mark was committed.
    [[nodiscard]] static double opacity(std::int64_t elapsedMs, std::int64_t holdMs = 300,
                                        std::int64_t fadeMs = 500) {
        holdMs = std::clamp<std::int64_t>(holdMs, 0, 3000);
        fadeMs = std::clamp<std::int64_t>(fadeMs, 0, 3000);
        if (fadeMs == 0)
            return elapsedMs < holdMs ? 1.0 : 0.0;
        const auto fadeElapsed = std::clamp(elapsedMs, holdMs, holdMs + fadeMs) - holdMs;
        return 1.0 - static_cast<double>(fadeElapsed) / static_cast<double>(fadeMs);
    }

  private:
    Phase m_phase = Phase::Idle;
    QuickAnnotationTool m_tool = QuickAnnotationTool::Brush;
};
} // namespace snow_shot::presentation
