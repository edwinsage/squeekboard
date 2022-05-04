/* Copyright (C) 2021 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 */

/*! Application-wide state is stored here.
 * It's driven by the loop defined in the loop module. */

use crate::animation;
use crate::debug;
use crate::imservice::{ ContentHint, ContentPurpose };
use crate::main::Commands;
use crate::outputs;
use crate::outputs::{Millimeter, OutputId, OutputState};
use crate::panel;
use crate::panel::PixelSize;
use crate::util::Rational;
use std::cmp;
use std::collections::HashMap;
use std::time::Instant;


#[derive(Clone, Copy, Debug)]
pub enum Presence {
    Present,
    Missing,
}

#[derive(Clone, Debug)]
pub struct InputMethodDetails {
    pub hint: ContentHint,
    pub purpose: ContentPurpose,
}

#[derive(Clone, Debug)]
pub enum InputMethod {
    Active(InputMethodDetails),
    InactiveSince(Instant),
}

/// Incoming events.
/// This contains events that cause a change to the internal state.
#[derive(Clone, Debug)]
pub enum Event {
    InputMethod(InputMethod),
    Visibility(visibility::Event),
    PhysicalKeyboard(Presence),
    Output(outputs::Event),
    Debug(debug::Event),
    /// Event triggered because a moment in time passed.
    /// Use to animate state transitions.
    /// The value is the ideal arrival time.
    TimeoutReached(Instant),
}

impl From<InputMethod> for Event {
    fn from(im: InputMethod) -> Self {
        Self::InputMethod(im)
    }
}

impl From<outputs::Event> for Event {
    fn from(ev: outputs::Event) -> Self {
        Self::Output(ev)
    }
}

pub mod visibility {
    #[derive(Clone, Debug)]
    pub enum Event {
        /// User requested the panel to show
        ForceVisible,
        /// The user requested the panel to go down
        ForceHidden,
    }

    #[derive(Clone, PartialEq, Debug, Copy)]
    pub enum State {
        /// Last interaction was user forcing the panel to go visible
        ForcedVisible,
        /// Last interaction was user forcing the panel to hide
        ForcedHidden,
        /// Last interaction was the input method changing active state
        NotForced,
    }
}

/// The outwardly visible state.
#[derive(Clone, Debug)]
pub struct Outcome {
    pub visibility: animation::Outcome,
    pub im: InputMethod,
}

impl Outcome {
    /// Returns the commands needed to apply changes as required by the new state.
    /// This implementation doesn't actually take the old state into account,
    /// instead issuing all the commands as needed to reach the new state.
    /// The receivers of the commands bear the burden
    /// of checking if the commands end up being no-ops.
    pub fn get_commands_to_reach(&self, new_state: &Self) -> Commands {
        let layout_hint_set = match new_state {
            Outcome {
                visibility: animation::Outcome::Visible{..},
                im: InputMethod::Active(hints),
            } => Some(hints.clone()),
            
            Outcome {
                visibility: animation::Outcome::Visible{..},
                im: InputMethod::InactiveSince(_),
            } => Some(InputMethodDetails {
                hint: ContentHint::NONE,
                purpose: ContentPurpose::Normal,
            }),
            
            Outcome {
                visibility: animation::Outcome::Hidden,
                ..
            } => None,
        };
// FIXME: handle switching outputs
        let (dbus_visible_set, panel_visibility) = match new_state.visibility {
            animation::Outcome::Visible{output, height}
                => (Some(true), Some(panel::Command::Show{output, height})),
            animation::Outcome::Hidden => (Some(false), Some(panel::Command::Hide)),
        };

        Commands {
            panel_visibility,
            layout_hint_set,
            dbus_visible_set,
        }
    }
}

/// The actual logic of the program.
/// At this moment, limited to calculating visibility and IM hints.
///
/// It keeps the panel visible for a short time period after each hide request.
/// This prevents flickering on quick successive enable/disable events.
/// It does not treat user-driven hiding in a special way.
///
/// This is the "functional core".
/// All state changes return the next state and the optimal time for the next check.
///
/// This state tracker can be driven by any event loop.
#[derive(Clone, Debug)]
pub struct Application {
    pub im: InputMethod,
    pub visibility_override: visibility::State,
    pub physical_keyboard: Presence,
    pub debug_mode_enabled: bool,
    /// The output on which the panel should appear.
    /// This is stored as part of the state
    /// because it's not clear how to derive the output from the rest of the state.
    /// It should probably follow the focused input,
    /// but not sure about being allowed on non-touch displays.
    pub preferred_output: Option<OutputId>,
    pub outputs: HashMap<OutputId, OutputState>,
}

impl Application {
    /// A conservative default, ignoring the actual state of things.
    /// It will initially show the keyboard for a blink.
    // The ignorance might actually be desired,
    // as it allows for startup without waiting for a system check.
    // The downside is that adding actual state should not cause transitions.
    // Another acceptable alternative is to allow explicitly uninitialized parts.
    pub fn new(now: Instant) -> Self {
        Self {
            im: InputMethod::InactiveSince(now),
            visibility_override: visibility::State::NotForced,
            physical_keyboard: Presence::Missing,
            debug_mode_enabled: false,
            preferred_output: None,
            outputs: Default::default(),
        }
    }

    pub fn apply_event(self, event: Event, now: Instant) -> Self {
        if self.debug_mode_enabled {
            println!(
                "Received event:
{:#?}",
                event,
            );
        }
        let state = match event {
            Event::Debug(dbg) => Self {
                debug_mode_enabled: match dbg {
                    debug::Event::Enable => true,
                    debug::Event::Disable => false,
                },
                ..self
            },

            Event::TimeoutReached(_) => self,

            Event::Visibility(visibility) => Self {
                visibility_override: match visibility {
                    visibility::Event::ForceHidden => visibility::State::ForcedHidden,
                    visibility::Event::ForceVisible => visibility::State::ForcedVisible,
                },
                ..self
            },

            Event::PhysicalKeyboard(presence) => Self {
                physical_keyboard: presence,
                ..self
            },

            Event::Output(outputs::Event { output, change }) => {
                let mut app = self;
                match change {
                    outputs::ChangeType::Altered(state) => {
                        app.outputs.insert(output, state);
                        app.preferred_output = app.preferred_output.or(Some(output));
                    },
                    outputs::ChangeType::Removed => {
                        app.outputs.remove(&output);
                        if app.preferred_output == Some(output) {
                            // There's currently no policy to choose one output over another,
                            // so just take whichever comes first.
                            app.preferred_output = app.outputs.keys().next().map(|output| *output);
                        }
                    },
                };
                app
            },

            Event::InputMethod(new_im) => match (self.im.clone(), new_im) {
                (InputMethod::Active(_old), InputMethod::Active(new_im))
                => Self {
                    im: InputMethod::Active(new_im),
                    ..self
                },
                // For changes in active state, remove user's visibility override.
                // Both cases spelled out explicitly, rather than by the wildcard,
                // to not lose the notion that it's the opposition that matters
                (InputMethod::InactiveSince(_old), InputMethod::Active(new_im))
                => Self {
                    im: InputMethod::Active(new_im),
                    visibility_override: visibility::State::NotForced,
                    ..self
                },
                (InputMethod::Active(_old), InputMethod::InactiveSince(since))
                => Self {
                    im: InputMethod::InactiveSince(since),
                    visibility_override: visibility::State::NotForced,
                    ..self
                },
                // This is a weird case, there's no need to update an inactive state.
                // But it's not wrong, just superfluous.
                (InputMethod::InactiveSince(old), InputMethod::InactiveSince(_new))
                => Self {
                    // New is going to be newer than old, so it can be ignored.
                    // It was already inactive at that moment.
                    im: InputMethod::InactiveSince(old),
                    ..self
                },
            }
        };

        if state.debug_mode_enabled {
            println!(
                "State is now:
{:#?}
Outcome:
{:#?}",
                state,
                state.get_outcome(now),
            );
        }
        state
    }

    fn get_preferred_height(output: &OutputState) -> Option<PixelSize> {
        output.get_pixel_size()
            .map(|px_size| {
                // Assume isotropy.
                // Pixels/mm.
                let density = output.get_physical_size()
                    .and_then(|size| size.width)
                    .map(|width| Rational {
                        numerator: px_size.width as i32,
                        denominator: width.0 as u32,
                    })
                    // Whatever the Librem 5 has,
                    // as a good default.
                    .unwrap_or(Rational {
                        numerator: 720,
                        denominator: 65,
                    });

                // Based on what works on the L5.
                // Exceeding that probably wastes space. Reducing makes typing harder.
                const IDEAL_TARGET_SIZE: Rational<Millimeter> = Rational {
                    numerator: Millimeter(1084),
                    denominator: 100,
                };

                // TODO: calculate based on selected layout
                const ROW_COUNT: u32 = 4;

                let height = {
                    let ideal_height = IDEAL_TARGET_SIZE * ROW_COUNT as i32;
                    let ideal_height_px = (ideal_height * density).ceil().0 as u32;

                    // Reduce height to match what the layout can fill.
                    // For this, we need to guess if normal or wide will be picked up.
                    // This must match `eek_gtk_keyboard.c::get_type`.
                    // TODO: query layout database and choose one directly
                    let abstract_width
                        = PixelSize {
                            scale_factor: output.scale as u32,
                            pixels: px_size.width,
                        } 
                        .as_scaled_ceiling();

                    let height_as_widths = {
                        if abstract_width < 540 {
                            // Normal
                            Rational {
                                numerator: 240,
                                denominator: 360,
                            }
                        } else {
                            // Wide
                            Rational {
                                numerator: 172,
                                denominator: 540,
                            }
                        }
                    };
                    cmp::min(
                        ideal_height_px,
                        (height_as_widths * px_size.width as i32).ceil() as u32,
                    )
                };
                PixelSize {
                    scale_factor: output.scale as u32,
                    pixels: cmp::min(height, px_size.height / 2),
                }
            })
    }

    pub fn get_outcome(&self, now: Instant) -> Outcome {
        // FIXME: include physical keyboard presence
        Outcome {
            visibility: match self.preferred_output {
                None => animation::Outcome::Hidden,
                Some(output) => {
                    // Hoping that this will get optimized out on branches not using `visible`.
                    let height = Self::get_preferred_height(self.outputs.get(&output).unwrap())
                        .unwrap_or(PixelSize{pixels: 0, scale_factor: 1});
                    // TODO: Instead of setting size to 0 when the output is invalid,
                    // simply go invisible.
                    let visible = animation::Outcome::Visible{ output, height };
                    
                    match (self.physical_keyboard, self.visibility_override) {
                        (_, visibility::State::ForcedHidden) => animation::Outcome::Hidden,
                        (_, visibility::State::ForcedVisible) => visible,
                        (Presence::Present, visibility::State::NotForced) => animation::Outcome::Hidden,
                        (Presence::Missing, visibility::State::NotForced) => match self.im {
                            InputMethod::Active(_) => visible,
                            InputMethod::InactiveSince(since) => {
                                if now < since + animation::HIDING_TIMEOUT { visible }
                                else { animation::Outcome::Hidden }
                            },
                        },
                    }
                }
            },
            im: self.im.clone(),
        }
    }

    /// Returns the next time to update the outcome.
    pub fn get_next_wake(&self, now: Instant) -> Option<Instant> {
        match self {
            Self {
                visibility_override: visibility::State::NotForced,
                im: InputMethod::InactiveSince(since),
                ..
            } => {
                let anim_end = *since + animation::HIDING_TIMEOUT;
                if now < anim_end { Some(anim_end) }
                else { None }
            }
            _ => None,
        }
    }
}


#[cfg(test)]
pub mod test {
    use super::*;
    use crate::outputs::c::WlOutput;
    use std::time::Duration;

    fn imdetails_new() -> InputMethodDetails {
        InputMethodDetails {
            purpose: ContentPurpose::Normal,
            hint: ContentHint::NONE,
        }
    }

    fn fake_output_id(id: usize) -> OutputId {
        OutputId(unsafe {
            std::mem::transmute::<_, WlOutput>(id)
        })
    }

    pub fn application_with_fake_output(start: Instant) -> Application {
        let id = fake_output_id(1);
        let mut outputs = HashMap::new();
        outputs.insert(
            id,
            OutputState {
                current_mode: None,
                geometry: None,
                scale: 1,
            },
        );
        Application {
            preferred_output: Some(id),
            outputs,
            ..Application::new(start)
        }
    }

    /// Test the original delay scenario: no flicker on quick switches.
    #[test]
    fn avoid_hide() {
        let start = Instant::now(); // doesn't matter when. It would be better to have a reproducible value though
        let mut now = start;
        let state = Application {
            im: InputMethod::Active(imdetails_new()),
            physical_keyboard: Presence::Missing,
            visibility_override: visibility::State::NotForced,
            ..application_with_fake_output(start)
        };

        let state = state.apply_event(Event::InputMethod(InputMethod::InactiveSince(now)), now);
        // Check 100ms at 1ms intervals. It should remain visible.
        for _i in 0..100 {
            now += Duration::from_millis(1);
            assert_matches!(
                state.get_outcome(now).visibility,
                animation::Outcome::Visible{..},
                "Hidden when it should remain visible: {:?}",
                now.saturating_duration_since(start),
            )
        }

        let state = state.apply_event(Event::InputMethod(InputMethod::Active(imdetails_new())), now);

        assert_matches!(state.get_outcome(now).visibility, animation::Outcome::Visible{..});
    }

    /// Make sure that hiding works when input method goes away
    #[test]
    fn hide() {
        let start = Instant::now(); // doesn't matter when. It would be better to have a reproducible value though
        let mut now = start;
        let state = Application {
            im: InputMethod::Active(imdetails_new()),
            physical_keyboard: Presence::Missing,
            visibility_override: visibility::State::NotForced,
            ..application_with_fake_output(start)
        };
        
        let state = state.apply_event(Event::InputMethod(InputMethod::InactiveSince(now)), now);

        while let animation::Outcome::Visible{..} = state.get_outcome(now).visibility {
            now += Duration::from_millis(1);
            assert!(
                now < start + Duration::from_millis(250),
                "Hiding too slow: {:?}",
                now.saturating_duration_since(start),
            );
        }
    }
    
    /// Check against the false showing bug.
    /// Expectation: it will get hidden and not appear again
    #[test]
    fn false_show() {
        let start = Instant::now(); // doesn't matter when. It would be better to have a reproducible value though
        let mut now = start;
        let state = Application {
            im: InputMethod::Active(imdetails_new()),
            physical_keyboard: Presence::Missing,
            visibility_override: visibility::State::NotForced,
            ..application_with_fake_output(start)
        };
        // This reflects the sequence from Wayland:
        // disable, disable, enable, disable
        // all in a single batch.
        let state = state.apply_event(Event::InputMethod(InputMethod::InactiveSince(now)), now);
        let state = state.apply_event(Event::InputMethod(InputMethod::InactiveSince(now)), now);
        let state = state.apply_event(Event::InputMethod(InputMethod::Active(imdetails_new())), now);
        let state = state.apply_event(Event::InputMethod(InputMethod::InactiveSince(now)), now);

        while let animation::Outcome::Visible{..} = state.get_outcome(now).visibility {
            now += Duration::from_millis(1);
            assert!(
                now < start + Duration::from_millis(250),
                "Still not hidden: {:?}",
                now.saturating_duration_since(start),
            );
        }
        
        // One second without appearing again
        for _i in 0..1000 {
            now += Duration::from_millis(1);
            assert_eq!(
                state.get_outcome(now).visibility,
                animation::Outcome::Hidden,
                "Appeared unnecessarily: {:?}",
                now.saturating_duration_since(start),
            );
        }
    }

    #[test]
    fn force_visible() {
        let start = Instant::now(); // doesn't matter when. It would be better to have a reproducible value though
        let mut now = start;
        let state = Application {
            im: InputMethod::InactiveSince(now),
            physical_keyboard: Presence::Missing,
            visibility_override: visibility::State::NotForced,
            ..application_with_fake_output(start)
        };
        now += Duration::from_secs(1);

        let state = state.apply_event(Event::Visibility(visibility::Event::ForceVisible), now);
        assert_matches!(
            state.get_outcome(now).visibility,
            animation::Outcome::Visible{..},
            "Failed to show: {:?}",
            now.saturating_duration_since(start),
        );
        
        now += Duration::from_secs(1);
        let state = state.apply_event(Event::InputMethod(InputMethod::Active(imdetails_new())), now);
        now += Duration::from_secs(1);
        let state = state.apply_event(Event::InputMethod(InputMethod::InactiveSince(now)), now);
        now += Duration::from_secs(1);

        assert_eq!(
            state.get_outcome(now).visibility,
            animation::Outcome::Hidden,
            "Failed to release forced visibility: {:?}",
            now.saturating_duration_since(start),
        );
    }

    #[test]
    fn keyboard_present() {
        let start = Instant::now(); // doesn't matter when. It would be better to have a reproducible value though
        let mut now = start;
        let state = Application {
            im: InputMethod::Active(imdetails_new()),
            physical_keyboard: Presence::Missing,
            visibility_override: visibility::State::NotForced,
            ..application_with_fake_output(start)
        };
        now += Duration::from_secs(1);

        let state = state.apply_event(Event::PhysicalKeyboard(Presence::Present), now);
        assert_eq!(
            state.get_outcome(now).visibility,
            animation::Outcome::Hidden,
            "Failed to hide: {:?}",
            now.saturating_duration_since(start),
        );
        
        now += Duration::from_secs(1);
        let state = state.apply_event(Event::InputMethod(InputMethod::InactiveSince(now)), now);
        now += Duration::from_secs(1);
        let state = state.apply_event(Event::InputMethod(InputMethod::Active(imdetails_new())), now);

        assert_eq!(
            state.get_outcome(now).visibility,
            animation::Outcome::Hidden,
            "Failed to remain hidden: {:?}",
            now.saturating_duration_since(start),
        );

        now += Duration::from_secs(1);
        let state = state.apply_event(Event::PhysicalKeyboard(Presence::Missing), now);

        assert_matches!(
            state.get_outcome(now).visibility,
            animation::Outcome::Visible{..},
            "Failed to appear: {:?}",
            now.saturating_duration_since(start),
        );

    }

    #[test]
    fn size_l5() {
        use crate::outputs::{Mode, Geometry, c, Size};
        assert_eq!(
            Application::get_preferred_height(&OutputState {
                current_mode: Some(Mode {
                    width: 720,
                    height: 1440,
                }),
                geometry: Some(Geometry{
                    transform: c::Transform::Normal,
                    phys_size: Size {
                        width: Some(Millimeter(65)),
                        height: Some(Millimeter(130)),
                    },
                }),
                scale: 2,
            }),
            Some(PixelSize {
                scale_factor: 2,
                pixels: 480,
            }),
        );
    }
}
