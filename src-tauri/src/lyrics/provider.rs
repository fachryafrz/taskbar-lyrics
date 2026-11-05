use super::parser::{parse_lrc, TimedLine};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;
use std::sync::RwLock;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LyricsResult {
    pub lines: Vec<TimedLine>,
    pub message: String,
}

#[derive(Deserialize)]
struct LrcLibResponse {
    instrumental: Option<bool>,
    #[serde(rename = "syncedLyrics")]
    synced_lyrics: Option<String>,
}

pub struct LyricsProvider {
    client: reqwest::Client,
    memory_cache: RwLock<HashMap<String, LyricsResult>>,
}

impl LyricsProvider {
    pub fn new() -> Self {
        Self {
            client: reqwest::Client::builder()
                .user_agent("Lyrix/2.0 (Windows desktop overlay)")
                .timeout(std::time::Duration::from_secs(5))
                .build()
                .unwrap_or_default(),
            memory_cache: RwLock::new(HashMap::new()),
        }
    }

    fn cache_dir() -> Option<PathBuf> {
        let base = dirs::data_local_dir()?;
        let dir = base.join("Lyrix").join("lyrics_cache");
        let _ = fs::create_dir_all(&dir);
        Some(dir)
    }

    fn cache_key(title: &str, artist: &str, album: &str, duration_secs: u32) -> String {
        format!("{}\n{}\n{}\n{}", title, artist, album, duration_secs)
    }

    fn disk_cache_path(key: &str) -> Option<PathBuf> {
        let dir = Self::cache_dir()?;
        // Safe filename sanitize
        let safe_name: String = key
            .chars()
            .map(|c| if c.is_alphanumeric() { c } else { '_' })
            .take(64)
            .collect();
        let hash = format!("{:x}", key.bytes().fold(0u64, |acc, b| acc.wrapping_mul(31).wrapping_add(b as u64)));
        Some(dir.join(format!("{}_{}.json", safe_name, hash)))
    }

    pub async fn fetch(
        &self,
        title: &str,
        artist: &str,
        album: &str,
        duration_secs: u32,
    ) -> LyricsResult {
        if title.is_empty() && artist.is_empty() {
            return LyricsResult {
                lines: Vec::new(),
                message: "Track metadata incomplete".into(),
            };
        }

        let key = Self::cache_key(title, artist, album, duration_secs);

        // 1. Check memory cache
        if let Ok(cache) = self.memory_cache.read() {
            if let Some(res) = cache.get(&key) {
                return res.clone();
            }
        }

        // 2. Check disk cache
        if let Some(disk_path) = Self::disk_cache_path(&key) {
            if let Ok(content) = fs::read_to_string(&disk_path) {
                if let Ok(res) = serde_json::from_str::<LyricsResult>(&content) {
                    if let Ok(mut cache) = self.memory_cache.write() {
                        cache.insert(key.clone(), res.clone());
                    }
                    return res;
                }
            }
        }

        // 3. Fetch from LRCLIB
        let mut query = vec![
            ("track_name", title),
            ("artist_name", artist),
        ];
        if !album.is_empty() {
            query.push(("album_name", album));
        }
        let dur_str = duration_secs.to_string();
        if duration_secs > 0 && duration_secs <= 3600 {
            query.push(("duration", &dur_str));
        }

        let result = match self.client.get("https://lrclib.net/api/get").query(&query).send().await {
            Ok(resp) => {
                let status = resp.status();
                if status == reqwest::StatusCode::NOT_FOUND {
                    LyricsResult {
                        lines: Vec::new(),
                        message: "No lyrics found for this track".into(),
                    }
                } else if status == reqwest::StatusCode::TOO_MANY_REQUESTS {
                    LyricsResult {
                        lines: Vec::new(),
                        message: "Lyrics service rate limit reached".into(),
                    }
                } else if !status.is_success() {
                    LyricsResult {
                        lines: Vec::new(),
                        message: "Lyrics service unavailable".into(),
                    }
                } else {
                    match resp.json::<LrcLibResponse>().await {
                        Ok(data) => {
                            if data.instrumental.unwrap_or(false) {
                                LyricsResult {
                                    lines: Vec::new(),
                                    message: "Instrumental track".into(),
                                }
                            } else if let Some(synced) = data.synced_lyrics {
                                let lines = parse_lrc(&synced);
                                if lines.is_empty() {
                                    LyricsResult {
                                        lines: Vec::new(),
                                        message: "No synchronized lyrics available".into(),
                                    }
                                } else {
                                    LyricsResult {
                                        lines,
                                        message: String::new(),
                                    }
                                }
                            } else {
                                LyricsResult {
                                    lines: Vec::new(),
                                    message: "No synchronized lyrics available".into(),
                                }
                            }
                        }
                        Err(_) => LyricsResult {
                            lines: Vec::new(),
                            message: "Invalid lyrics response".into(),
                        },
                    }
                }
            }
            Err(_) => LyricsResult {
                lines: Vec::new(),
                message: "Could not connect to lyrics service".into(),
            },
        };

        // Save to caches
        if let Ok(mut cache) = self.memory_cache.write() {
            cache.insert(key.clone(), result.clone());
        }
        if let Some(disk_path) = Self::disk_cache_path(&key) {
            if let Ok(json) = serde_json::to_string(&result) {
                let _ = fs::write(disk_path, json);
            }
        }

        result
    }
}
