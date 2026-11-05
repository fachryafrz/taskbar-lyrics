use super::parser::TimedLine;
use serde::{Deserialize, Serialize};
use std::time::Instant;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum PlaybackState {
    Playing,
    Paused,
}

pub struct LyricsSyncEngine {
    lines: Vec<TimedLine>,
    position_ms: u64,
    state: PlaybackState,
    observed_at: Instant,
    offset_ms: i64,
}

impl LyricsSyncEngine {
    pub fn new() -> Self {
        Self {
            lines: Vec::new(),
            position_ms: 0,
            state: PlaybackState::Paused,
            observed_at: Instant::now(),
            offset_ms: 0,
        }
    }

    pub fn set_lyrics(&mut self, mut lines: Vec<TimedLine>) {
        lines.sort_by_key(|l| l.timestamp_ms);
        self.lines = lines;
    }

    pub fn update_timeline(&mut self, position_ms: u64, state: PlaybackState, observed_at: Instant) {
        self.position_ms = position_ms;
        self.state = state;
        self.observed_at = observed_at;
    }

    pub fn set_offset(&mut self, offset_ms: i64) {
        self.offset_ms = offset_ms;
    }

    pub fn active_line(&self, now: Instant) -> Option<&TimedLine> {
        if self.lines.is_empty() {
            return None;
        }

        let mut effective_position = self.position_ms as i64 + self.offset_ms;
        if self.state == PlaybackState::Playing && now >= self.observed_at {
            let elapsed_ms = (now - self.observed_at).as_millis() as i64;
            effective_position += elapsed_ms;
        }

        if effective_position < 0 {
            effective_position = 0;
        }

        let target_ms = effective_position as u64;

        // Binary search: find the largest line where line.timestamp_ms <= target_ms
        let idx = match self.lines.binary_search_by_key(&target_ms, |l| l.timestamp_ms) {
            Ok(exact) => exact,
            Err(insertion_point) => {
                if insertion_point == 0 {
                    return None; // Song hasn't reached first lyric yet
                }
                insertion_point - 1
            }
        };

        self.lines.get(idx)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_sync_engine_timing() {
        let mut engine = LyricsSyncEngine::new();
        engine.set_lyrics(vec![
            TimedLine { timestamp_ms: 1000, text: "Line 1".into() },
            TimedLine { timestamp_ms: 3000, text: "Line 2".into() },
            TimedLine { timestamp_ms: 5000, text: "Line 3".into() },
        ]);

        let start = Instant::now();
        engine.update_timeline(500, PlaybackState::Playing, start);

        // At 500ms -> None (before first lyric)
        assert_eq!(engine.active_line(start), None);

        // At 1500ms (1000ms after start) -> Line 1
        let t1 = start + std::time::Duration::from_millis(1000);
        assert_eq!(engine.active_line(t1).unwrap().text, "Line 1");

        // At 3500ms (3000ms after start) -> Line 2
        let t2 = start + std::time::Duration::from_millis(3000);
        assert_eq!(engine.active_line(t2).unwrap().text, "Line 2");
    }

    #[test]
    fn test_sync_engine_with_offset() {
        let mut engine = LyricsSyncEngine::new();
        engine.set_lyrics(vec![
            TimedLine { timestamp_ms: 1000, text: "Line 1".into() },
            TimedLine { timestamp_ms: 3000, text: "".into() }, // instrumental
            TimedLine { timestamp_ms: 5000, text: "Line 3".into() },
        ]);
        engine.set_offset(300);

        let now = Instant::now();
        // Raw position 700ms + offset 300ms = 1000ms -> should match Line 1
        engine.update_timeline(700, PlaybackState::Paused, now);
        assert_eq!(engine.active_line(now).unwrap().text, "Line 1");

        // Raw position 2700ms + offset 300ms = 3000ms -> instrumental break ("")
        engine.update_timeline(2700, PlaybackState::Paused, now);
        assert_eq!(engine.active_line(now).unwrap().text, "");

        // Raw position 600ms + offset 300ms = 900ms -> before line 1 (None)
        engine.update_timeline(600, PlaybackState::Paused, now);
        assert_eq!(engine.active_line(now), None);
    }
}
