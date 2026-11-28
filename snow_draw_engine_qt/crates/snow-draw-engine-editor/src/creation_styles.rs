use super::*;
use serde::{Deserialize, Serialize};
use snow_draw_engine_document::{TextHorizontalAlign, TextVerticalAlign};
use std::sync::Arc;

/// Values are also the bits returned by the C style-target query.
#[repr(u32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TextCreationScenario {
    Standalone = 1,
    SerialNumber = 2,
    SpeechBubble = 4,
    Shape = 8,
    Arrow = 16,
}

impl TextCreationScenario {
    pub const ALL: [Self; 5] = [
        Self::Standalone,
        Self::SerialNumber,
        Self::SpeechBubble,
        Self::Shape,
        Self::Arrow,
    ];
}

#[repr(u32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ArrowCreationScenario {
    Standalone = 1,
    SerialNumber = 2,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct BoundCreationStyles {
    pub serial_number_text: TextStyle,
    pub speech_bubble_text: TextStyle,
    pub shape_text: TextStyle,
    pub arrow_text: TextStyle,
    pub serial_number_arrow: ArrowStyle,
}

impl BoundCreationStyles {
    pub fn from_standalone(text: &TextStyle, arrow: ArrowStyle) -> Self {
        let centered = TextStyle {
            horizontal_align: TextHorizontalAlign::Center,
            vertical_align: TextVerticalAlign::Center,
            ..text.clone()
        };
        Self {
            serial_number_text: text.clone(),
            shape_text: text.clone(),
            speech_bubble_text: centered.clone(),
            arrow_text: centered,
            serial_number_arrow: arrow,
        }
    }

    pub(crate) fn validate(&self) -> Result<(), ErrorCode> {
        for style in [
            &self.serial_number_text,
            &self.speech_bubble_text,
            &self.shape_text,
            &self.arrow_text,
        ] {
            style::validate_text_style(style)?;
        }
        style::validate_arrow_style(self.serial_number_arrow)
    }
}

pub(crate) fn text_scenario(
    document: &DocumentModel,
    id: ElementId,
) -> Option<TextCreationScenario> {
    if let Some(owner) = document.arrow_id_for_text(id) {
        return document
            .arrow(owner)
            .ok()?
            .is_regular_arrow()
            .then_some(TextCreationScenario::Arrow);
    }
    Some(if document.speech_bubble_id_for_text(id).is_some() {
        TextCreationScenario::SpeechBubble
    } else if document.is_text_bound_to_serial_number(id) {
        TextCreationScenario::SerialNumber
    } else if !document.shapes_for_text(id).is_empty() {
        TextCreationScenario::Shape
    } else {
        TextCreationScenario::Standalone
    })
}

pub(crate) fn arrow_scenario(document: &DocumentModel, arrow: &ArrowData) -> ArrowCreationScenario {
    if [&arrow.start_binding, &arrow.end_binding]
        .into_iter()
        .flatten()
        .any(|binding| document.serial_number(binding.element_id).is_ok())
    {
        ArrowCreationScenario::SerialNumber
    } else {
        ArrowCreationScenario::Standalone
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) struct GestureArrowStyles {
    pub standalone: ArrowStyle,
    pub serial_number: ArrowStyle,
}

impl Editor {
    pub fn text_creation_style(&self, scenario: TextCreationScenario) -> TextStyle {
        let bound = &self.state.bound_styles;
        match scenario {
            TextCreationScenario::Standalone => TextStyle::from_text(&self.state.default_text),
            TextCreationScenario::SerialNumber => bound.serial_number_text.clone(),
            TextCreationScenario::SpeechBubble => bound.speech_bubble_text.clone(),
            TextCreationScenario::Shape => bound.shape_text.clone(),
            TextCreationScenario::Arrow => bound.arrow_text.clone(),
        }
    }

    pub(crate) fn text_creation_template(&self, scenario: TextCreationScenario) -> TextData {
        text::text_with_style_attributes(
            &self.state.default_text,
            &self.text_creation_style(scenario),
        )
    }

    pub fn text_style_targets(&self, document: &DocumentModel, draft: bool) -> u32 {
        if draft {
            return match self
                .state
                .active_text_draft
                .as_ref()
                .map(|draft| draft.target)
            {
                Some(ActiveTextDraftTarget::New) => TextCreationScenario::Standalone as u32,
                Some(ActiveTextDraftTarget::NewArrow(_)) => TextCreationScenario::Arrow as u32,
                Some(ActiveTextDraftTarget::NewSpeechBubble(_)) => {
                    TextCreationScenario::SpeechBubble as u32
                }
                Some(ActiveTextDraftTarget::Existing(id)) => {
                    text_scenario(document, id).map_or(0, |s| s as u32)
                }
                None => 0,
            };
        }
        self.state
            .selection
            .ids
            .iter()
            .copied()
            .filter(|id| document.text(*id).is_ok())
            .map(|id| text_scenario(document, id).map_or(0, |s| s as u32))
            .reduce(|a, b| a | b)
            .unwrap_or(TextCreationScenario::Standalone as u32)
    }

    pub(crate) fn gesture_arrow_scenario(&self, document: &DocumentModel) -> ArrowCreationScenario {
        if let Some(ElementCreationPreview::Arrow(arrow)) = &self.state.creation_preview {
            return arrow_scenario(document, arrow);
        }
        if matches!(&self.state.interaction, InteractionState::CreatingArrow(state) if state.serial_number_source.is_some())
        {
            ArrowCreationScenario::SerialNumber
        } else {
            ArrowCreationScenario::Standalone
        }
    }

    pub fn arrow_style_targets(&self, document: &DocumentModel) -> u32 {
        if matches!(&self.state.interaction, InteractionState::CreatingArrow(state) if state.arrow_styles.is_some())
        {
            return self.gesture_arrow_scenario(document) as u32;
        }
        self.state
            .selection
            .ids
            .iter()
            .filter_map(|id| document.arrow(*id).ok())
            .filter(|arrow| arrow.is_regular_arrow())
            .map(|arrow| arrow_scenario(document, arrow) as u32)
            .reduce(|a, b| a | b)
            .unwrap_or(ArrowCreationScenario::Standalone as u32)
    }

    pub(crate) fn gesture_arrow_style(&self, scenario: ArrowCreationScenario) -> ArrowStyle {
        if let InteractionState::CreatingArrow(state) = &self.state.interaction
            && let Some(styles) = state.arrow_styles
        {
            return match scenario {
                ArrowCreationScenario::Standalone => styles.standalone,
                ArrowCreationScenario::SerialNumber => styles.serial_number,
            };
        }
        match scenario {
            ArrowCreationScenario::Standalone => self.state.default_arrow_style,
            ArrowCreationScenario::SerialNumber => self.state.bound_styles.serial_number_arrow,
        }
    }

    pub(crate) fn patch_text_defaults(&mut self, targets: u32, style: &TextStyle, properties: u32) {
        for scenario in TextCreationScenario::ALL {
            if targets & scenario as u32 == 0 {
                continue;
            }
            let patched =
                style::patched_text_style(self.text_creation_style(scenario), style, properties);
            match scenario {
                TextCreationScenario::Standalone => {
                    self.state.default_text =
                        text::text_with_style_attributes(&self.state.default_text, &patched)
                }
                TextCreationScenario::SerialNumber => {
                    Arc::make_mut(&mut self.state.bound_styles).serial_number_text = patched
                }
                TextCreationScenario::SpeechBubble => {
                    Arc::make_mut(&mut self.state.bound_styles).speech_bubble_text = patched
                }
                TextCreationScenario::Shape => {
                    Arc::make_mut(&mut self.state.bound_styles).shape_text = patched
                }
                TextCreationScenario::Arrow => {
                    Arc::make_mut(&mut self.state.bound_styles).arrow_text = patched
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use snow_draw_engine_document::{
        ArrowEndpointBinding, ElementMeta, SerialNumberData, Transaction,
    };

    #[test]
    fn mixed_arrow_selection_remembers_only_the_edited_properties() {
        let mut editor = Editor::new(snow_draw_engine_core::EngineConfig::default()).unwrap();
        let mut document = DocumentModel::new();
        let badge = document.allocate_element_id();
        let ids = [
            document.allocate_element_id(),
            document.allocate_element_id(),
        ];
        let mut tx = Transaction::new("Insert plain and sequence-bound arrows");
        tx.insert_serial_number(badge, ElementMeta::default(), SerialNumberData::default());
        let arrow = preview_arrow_from_points(
            &[Point::new(30.0, 0.0), Point::new(180.0, 0.0)],
            editor.state.default_arrow_style,
        )
        .unwrap();
        tx.insert_arrow(ids[0], ElementMeta::default(), arrow.clone());
        let binding = ArrowEndpointBinding {
            element_id: badge,
            fixed_point: [0.5, 0.5],
            mode: snow_draw_engine_core::arrow::BindMode::Orbit,
        };
        tx.insert_arrow(
            ids[1],
            ElementMeta::default(),
            ArrowData {
                start_binding: Some(binding),
                ..arrow
            },
        );
        document.apply_transaction(tx).unwrap();
        Arc::make_mut(&mut editor.state.bound_styles)
            .serial_number_arrow
            .arrow_outline_width = 6.0;
        editor.set_selection_state(ids.to_vec(), Some(ids[0]));
        assert_eq!(
            editor.arrow_style_targets(&document),
            ArrowCreationScenario::Standalone as u32 | ArrowCreationScenario::SerialNumber as u32
        );
        let mut style = editor.shape_style(&document);
        style.stroke_width = 11.0;
        editor
            .set_shape_style_patch(
                &document,
                ShapeStylePatch {
                    kind: ShapeKind::Arrow,
                    style,
                    properties: SHAPE_STYLE_PROPERTY_STROKE_WIDTH,
                },
            )
            .unwrap()
            .unwrap();
        assert_eq!(editor.state.default_arrow_style.stroke_width, 11.0);
        assert_eq!(
            editor.state.bound_styles.serial_number_arrow.stroke_width,
            11.0
        );
        assert_eq!(editor.state.default_arrow_style.arrow_outline_width, 0.0);
        assert_eq!(
            editor
                .state
                .bound_styles
                .serial_number_arrow
                .arrow_outline_width,
            6.0
        );
    }

    #[test]
    fn mixed_text_selection_patches_only_explicit_fields_in_each_represented_scenario() {
        let mut editor = Editor::new(snow_draw_engine_core::EngineConfig::default()).unwrap();
        let mut document = DocumentModel::new();
        let labels = [(); 3].map(|()| document.allocate_element_id());
        let number = document.allocate_element_id();
        let shape = document.allocate_element_id();
        let mut tx = Transaction::new("Insert plain, sequence and shape text");
        for id in labels {
            tx.insert_text(id, ElementMeta::default(), TextData::default());
        }
        tx.insert_serial_number(
            number,
            ElementMeta::default(),
            SerialNumberData {
                text_element_id: Some(labels[1]),
                ..Default::default()
            },
        );
        let (rectangle, _) = editor.preview_rectangle_with_snapping(
            &document,
            Point::default(),
            Point::new(60.0, 40.0),
            Modifiers::default(),
        );
        let mut rectangle = rectangle.unwrap();
        rectangle.text_element_id = Some(labels[2]);
        tx.insert_rectangle(shape, ElementMeta::default(), rectangle);
        document.apply_transaction(tx).unwrap();
        let mut expected = TextCreationScenario::ALL.map(|s| editor.text_creation_style(s));
        for (index, scenario) in TextCreationScenario::ALL.into_iter().enumerate() {
            expected[index].font_size = 18.0 + index as f64 * 5.0;
            editor.patch_text_defaults(
                scenario as u32,
                &expected[index],
                TEXT_STYLE_MIXED_FONT_SIZE,
            );
        }
        editor.set_selection_state(labels.to_vec(), Some(labels[0]));
        let targets = TextCreationScenario::Standalone as u32
            | TextCreationScenario::SerialNumber as u32
            | TextCreationScenario::Shape as u32;
        assert_eq!(editor.text_style_targets(&document, false), targets);
        let mut patch = expected[4].clone();
        patch.opacity = 0.42;
        editor
            .set_text_style(&document, patch, TEXT_STYLE_MIXED_OPACITY, &[])
            .unwrap()
            .unwrap();
        // The Select tool's generic opacity command edits the document only.
        editor
            .set_selected_opacity(&document, 0.25)
            .unwrap()
            .unwrap();
        for (mut style, scenario) in expected.into_iter().zip(TextCreationScenario::ALL) {
            if targets & scenario as u32 != 0 {
                style.opacity = 0.42;
            }
            assert_eq!(editor.text_creation_style(scenario), style);
        }
        editor.state.active_tool = ActiveTool::SerialNumber;
        let badge_font = editor.state.default_serial_number.font_size;
        let layouts = labels.map(|id| TextLayoutOverride {
            id,
            size: document.text(id).unwrap().layout,
        });
        editor
            .adjust_font_size_step(&document, true, &layouts)
            .unwrap()
            .unwrap();
        assert_eq!(editor.state.default_serial_number.font_size, badge_font);
        assert_eq!(
            editor
                .text_creation_style(TextCreationScenario::Shape)
                .font_size,
            editor
                .text_creation_style(TextCreationScenario::SerialNumber)
                .font_size
        );
    }
}
