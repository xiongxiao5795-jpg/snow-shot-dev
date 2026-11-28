use super::*;
use snow_draw_engine_core::ColorRgba8;

/// Colors owned by an in-progress creation gesture, independently of future preferences.
#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) struct CreationColors {
    pub stroke: ColorRgba8,
    pub fill: ColorRgba8,
    pub foreground: ColorRgba8,
}

impl CreationColors {
    fn new(stroke: ColorRgba8, fill: ColorRgba8) -> Self {
        Self {
            stroke,
            fill,
            foreground: stroke,
        }
    }
}

impl Editor {
    pub(crate) fn preserve_creation_colors(&mut self) {
        if self.state.creation_colors.is_some() {
            return;
        }
        let state = &self.state;
        let colors = match (&state.interaction, state.active_tool) {
            (InteractionState::CreatingRectangle(_), ActiveTool::Shape) => CreationColors::new(
                state.default_rectangle_shape_style.stroke,
                state.default_rectangle_shape_style.fill,
            ),
            (InteractionState::CreatingRectangle(_), ActiveTool::SpeechBubble) => {
                CreationColors::new(
                    state.default_speech_bubble_style.stroke,
                    state.default_speech_bubble_style.fill,
                )
            }
            (InteractionState::CreatingRectangle(_), ActiveTool::RectangleHighlight) => {
                CreationColors::new(
                    state.default_rectangle_highlight_style.stroke,
                    state.default_rectangle_highlight_style.fill,
                )
            }
            (InteractionState::CreatingRectangle(_), ActiveTool::Magnifier) => {
                CreationColors::new(state.default_magnifier_style.stroke, ColorRgba8::default())
            }
            (InteractionState::CreatingArrow(_), ActiveTool::Line) => CreationColors::new(
                state.default_line_style.stroke,
                state.default_line_style.fill,
            ),
            (InteractionState::CreatingArrow(_), ActiveTool::Distance) => {
                CreationColors::new(state.default_distance_style.stroke, ColorRgba8::default())
            }
            (InteractionState::CreatingArrow(_), ActiveTool::Angle) => {
                CreationColors::new(state.default_angle_style.stroke, ColorRgba8::default())
            }
            (InteractionState::CreatingArrow(_), ActiveTool::Bracket) => {
                CreationColors::new(state.default_bracket_style.stroke, ColorRgba8::default())
            }
            (InteractionState::CreatingPenHighlight(_), _) => CreationColors::new(
                state.default_pen_highlight_style.stroke,
                state.default_pen_highlight_style.fill,
            ),
            (InteractionState::CreatingSerialNumber(creation), _)
                if creation.mode != SerialNumberDragMode::Text =>
            {
                CreationColors::new(
                    state.bound_styles.serial_number_arrow.stroke,
                    ColorRgba8::default(),
                )
            }
            (InteractionState::CreatingSerialNumber(_), _) => CreationColors {
                stroke: state.bound_styles.serial_number_text.stroke,
                fill: state.bound_styles.serial_number_text.fill,
                foreground: state.bound_styles.serial_number_text.color,
            },
            // Freehand strokes and active text drafts already own their styles.
            _ => return,
        };
        self.state.creation_colors = Some(colors);
    }

    pub(crate) fn creation_stroke(&self, color: ColorRgba8) -> ColorRgba8 {
        self.state
            .creation_colors
            .map_or(color, |colors| colors.stroke)
    }

    pub(crate) fn creation_fill(&self, color: ColorRgba8) -> ColorRgba8 {
        self.state
            .creation_colors
            .map_or(color, |colors| colors.fill)
    }

    /// Explicit toolbar edits still affect the gesture, but preference synchronization does not.
    pub(crate) fn edit_creation_colors(
        &mut self,
        tool: ActiveTool,
        stroke: Option<ColorRgba8>,
        fill: Option<ColorRgba8>,
    ) {
        if self.state.active_tool != tool {
            return;
        }
        if let Some(colors) = &mut self.state.creation_colors {
            if let Some(stroke) = stroke {
                colors.stroke = stroke;
            }
            if let Some(fill) = fill {
                colors.fill = fill;
            }
        }
    }

    /// Updates preferences without recoloring selections, drafts, or active creation gestures.
    pub fn set_creation_color(&mut self, color: ColorRgba8) {
        self.preserve_creation_colors();
        for style in [
            &mut self.state.default_rectangle_shape_style,
            &mut self.state.default_speech_bubble_style,
        ] {
            style.stroke = color;
            style.fill = color;
        }
        for style in [
            &mut self.state.default_line_style,
            &mut self.state.default_free_draw_style,
            &mut self.state.default_rectangle_highlight_style,
            &mut self.state.default_pen_highlight_style,
        ] {
            style.stroke = color;
            style.fill = color;
        }
        let bound = &self.state.bound_styles;
        if bound.serial_number_arrow.stroke != color
            || [
                &bound.serial_number_text,
                &bound.shape_text,
                &bound.speech_bubble_text,
                &bound.arrow_text,
            ]
            .iter()
            .any(|s| s.color != color || s.fill != color || s.stroke != color)
        {
            let bound = std::sync::Arc::make_mut(&mut self.state.bound_styles);
            bound.serial_number_arrow.stroke = color;
            for text in [
                &mut bound.serial_number_text,
                &mut bound.speech_bubble_text,
                &mut bound.shape_text,
                &mut bound.arrow_text,
            ] {
                text.color = color;
                text.stroke = color;
                text.fill = color;
            }
        }
        self.state.default_arrow_style.stroke = color;
        self.state.default_distance_style.stroke = color;
        self.state.default_angle_style.stroke = color;
        self.state.default_bracket_style.stroke = color;
        self.state.default_magnifier_style.stroke = color;
        self.state.default_text.color = color;
        self.state.default_text.stroke = color;
        self.state.default_text.fill = color;
        self.state.default_serial_number.color = color;
        self.state.default_serial_number.fill = color;
    }
}
