#include "snow_shot/presentation/quickannotationstate.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using snow_shot::presentation::QuickAnnotationHoldState;
using snow_shot::presentation::QuickAnnotationState;
using snow_shot::presentation::QuickAnnotationTool;

static void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main() {
    using snow_shot::presentation::quickAnnotationFunctionKeyState;
    using snow_shot::presentation::quickAnnotationControlClickIsLeft;
    require(quickAnnotationControlClickIsLeft(true, true, true, true) &&
                !quickAnnotationControlClickIsLeft(false, true, true, true) &&
                !quickAnnotationControlClickIsLeft(true, true, true, false) &&
                !quickAnnotationControlClickIsLeft(true, true, false, true),
            "only a held control-left click is normalized; real right clicks remain untouched");
    require(!quickAnnotationFunctionKeyState(false, 126, true) &&
                quickAnnotationFunctionKeyState(false, 63, true) &&
                quickAnnotationFunctionKeyState(true, 126, false) &&
                !quickAnnotationFunctionKeyState(true, 63, false),
            "Fn tracking distinguishes the physical Fn key from navigation keys");
    QuickAnnotationHoldState hold;
    using HoldChange = QuickAnnotationHoldState::Change;
    require(!hold.otherKey() && hold.modifiers(true) == HoldChange::Schedule && hold.activate() &&
                hold.modifiers(false) == HoldChange::End,
            "ordinary typing does not cancel the next hold gesture");
    require(hold.modifiers(true) == HoldChange::Schedule && !hold.otherKey() && !hold.activate(),
            "another key cancels pending modifier-only activation");
    require(!hold.otherKey(), "additional typing keeps the held chord cancelled");
    require(hold.modifiers(true) == HoldChange::None && hold.modifiers(false) == HoldChange::None,
            "cancelled modifier chord cannot reactivate until released");
    require(hold.modifiers(true) == HoldChange::Schedule && hold.activate() &&
                hold.modifiers(true) == HoldChange::None &&
                hold.modifiers(false) == HoldChange::End,
            "a held modifier chord activates once and releases once");
    require(hold.modifiers(false) == HoldChange::None, "duplicate release is ignored");
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
                QuickAnnotationState::opacity(300) == 1.0 &&
                QuickAnnotationState::opacity(550) == 0.5 &&
                QuickAnnotationState::opacity(800) == 0.0 &&
                QuickAnnotationState::opacity(4000) == 0.0,
            "hold and fade independently by elapsed time");
    require(QuickAnnotationState::opacity(3000, 3000) == 1.0 &&
                QuickAnnotationState::opacity(3250, 3000) == 0.5 &&
                QuickAnnotationState::opacity(3500, 3000) == 0.0,
            "custom hold duration changes only the selected mark");
    require(QuickAnnotationState::opacity(0, 0, 0) == 0.0 &&
                QuickAnnotationState::opacity(99, 100, 0) == 1.0 &&
                QuickAnnotationState::opacity(100, 100, 0) == 0.0,
            "zero fade duration expires immediately without division by zero");
    require(QuickAnnotationState::opacity(3000, 3000, 1200) == 1.0 &&
                QuickAnnotationState::opacity(3600, 3000, 1200) == 0.5 &&
                QuickAnnotationState::opacity(4200, 3000, 1200) == 0.0,
            "custom fade duration applies independently");
}
