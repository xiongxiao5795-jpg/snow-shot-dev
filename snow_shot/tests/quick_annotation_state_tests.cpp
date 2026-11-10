#include "snow_shot/presentation/quickannotationstate.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using snow_shot::presentation::QuickAnnotationState;
using snow_shot::presentation::QuickAnnotationTool;

static void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main() {
    QuickAnnotationState state;
    using Phase = QuickAnnotationState::Phase;
    require(state.phase() == Phase::Idle && state.tool() == QuickAnnotationTool::Brush,
            "initial state and tool");
    require(!state.begin() && !state.finish(), "idle rejects gesture events");
    require(state.arm(QuickAnnotationTool::Rectangle), "arm rectangle");
    require(state.arm(QuickAnnotationTool::Ellipse) && state.tool() == QuickAnnotationTool::Ellipse,
            "last selection wins while armed");
    require(state.begin() && state.phase() == Phase::Drawing, "begin gesture");
    require(!state.arm(QuickAnnotationTool::Arrow) && state.tool() == QuickAnnotationTool::Ellipse,
            "do not interrupt drawing");
    require(state.finish() && state.phase() == Phase::Idle, "one gesture returns to idle");
    require(!state.finish(), "duplicate release is ignored");
    require(state.arm(QuickAnnotationTool::Arrow), "arm arrow");
    state.cancel();
    require(state.phase() == Phase::Idle && state.tool() == QuickAnnotationTool::Arrow,
            "cancel preserves last tool");
    require(QuickAnnotationState::opacity(0) == 1.0 &&
                QuickAnnotationState::opacity(std::numeric_limits<std::int64_t>::min()) == 1.0 &&
                QuickAnnotationState::opacity(3000) == 1.0 &&
                QuickAnnotationState::opacity(3300) == 0.5 &&
                QuickAnnotationState::opacity(3600) == 0.0 &&
                QuickAnnotationState::opacity(4000) == 0.0,
            "hold and fade independently by elapsed time");
}
