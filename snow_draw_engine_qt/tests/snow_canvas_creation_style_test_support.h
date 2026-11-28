#pragma once

#include "snow_canvas_type_conversions.h"

#include <cstdlib>

inline SnowCanvasStyleDefaults textAndArrowTestDefaults() {
    SnowStyleDefaults engine{};
    if (snow_runtime_style_defaults_default(&engine) != SNOW_OK)
        std::abort();
    SnowCanvasStyleDefaults defaults;
    defaults.text = snow_canvas_types::toCanvasTextStyle(engine.text);
    defaults.arrow = snow_canvas_types::toCanvasShapeStyle(engine.arrow);
    return defaults;
}
