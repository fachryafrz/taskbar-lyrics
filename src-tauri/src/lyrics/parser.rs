use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct TimedLine {
    pub timestamp_ms: u64,
    pub text: String,
}

fn parse_timestamp(token: &str) -> Option<u64> {
    let colon = token.find(':')?;
    let minutes_str = &token[..colon];
    let seconds_str = &token[colon + 1..];

    let minutes: u64 = minutes_str.parse().ok()?;
    let seconds: f64 = seconds_str.parse().ok()?;

    if seconds < 0.0 || seconds >= 60.0 {
        return None;
    }

    let millis = (minutes * 60 * 1000) + (seconds * 1000.0).round() as u64;
    Some(millis)
}

pub fn parse_lrc(source: &str) -> Vec<TimedLine> {
    let mut lines = Vec::new();

    for raw_line in source.lines() {
        let trimmed = raw_line.trim_end_matches(['\r', '\n']);
        let mut timestamps = Vec::new();
        let mut cursor = 0;

        let bytes = trimmed.as_bytes();
        while cursor < bytes.len() && bytes[cursor] == b'[' {
            if let Some(close) = trimmed[cursor + 1..].find(']') {
                let close_idx = cursor + 1 + close;
                let token = &trimmed[cursor + 1..close_idx];
                if let Some(ts) = parse_timestamp(token) {
                    timestamps.push(ts);
                }
                cursor = close_idx + 1;
            } else {
                break;
            }
        }

        if timestamps.is_empty() {
            continue;
        }

        let text = trimmed[cursor..].trim().to_string();
        for ts in timestamps {
            lines.push(TimedLine {
                timestamp_ms: ts,
                text: text.clone(),
            });
        }
    }

    lines.sort_by_key(|line| line.timestamp_ms);
    lines
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_lrc_basic() {
        let lrc = "[00:12.50]Hello world\n[00:15.00]Second line\n";
        let parsed = parse_lrc(lrc);
        assert_eq!(parsed.len(), 2);
        assert_eq!(parsed[0].timestamp_ms, 12500);
        assert_eq!(parsed[0].text, "Hello world");
        assert_eq!(parsed[1].timestamp_ms, 15000);
        assert_eq!(parsed[1].text, "Second line");
    }

    #[test]
    fn test_parse_multiple_timestamps() {
        let lrc = "[00:10.00][00:20.00]Chorus line";
        let parsed = parse_lrc(lrc);
        assert_eq!(parsed.len(), 2);
        assert_eq!(parsed[0].timestamp_ms, 10000);
        assert_eq!(parsed[1].timestamp_ms, 20000);
        assert_eq!(parsed[0].text, "Chorus line");
        assert_eq!(parsed[1].text, "Chorus line");
    }
}
