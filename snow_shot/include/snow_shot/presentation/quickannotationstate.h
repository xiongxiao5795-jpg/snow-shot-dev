#pragma once

#include <algorithm>
#include <cstdint>

namespace snow_shot::presentation {

enum class QuickAnnotationTool { Brush, Arrow, Rectangle, RoundedRectangle, Ellipse };

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
    [[nodiscard]] static double opacity(std::int64_t elapsedMs, std::int64_t holdMs = 1500,
                                        std::int64_t fadeMs = 600) {
        holdMs = std::clamp<std::int64_t>(holdMs, 0, 30000);
        fadeMs = std::clamp<std::int64_t>(fadeMs, 100, 5000);
        const auto fadeElapsed = std::clamp(elapsedMs, holdMs, holdMs + fadeMs) - holdMs;
        return 1.0 - static_cast<double>(fadeElapsed) / static_cast<double>(fadeMs);
    }

  private:
    Phase m_phase = Phase::Idle;
    QuickAnnotationTool m_tool = QuickAnnotationTool::Brush;
};
} // namespace snow_shot::presentation
