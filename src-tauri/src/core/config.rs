use serde::{Deserialize, Serialize};
use std::fs;
use std::path::PathBuf;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AppConfig {
    pub width: i32,
    pub height: i32,
    pub taskbar_offset_x: i32,
    pub taskbar_offset_y: i32,
    pub lyric_offset_milliseconds: i32,
    pub font_size: i32,
    pub locked: bool,
    pub wrap_long_lyrics: bool,
    pub visible: bool,
    pub always_on_top: bool,
}

impl Default for AppConfig {
    fn default() -> Self {
        Self {
            width: 400,
            height: 42,
            taskbar_offset_x: 180,
            taskbar_offset_y: 0,
            lyric_offset_milliseconds: 300,
            font_size: 12,
            locked: false,
            wrap_long_lyrics: true,
            visible: true,
            always_on_top: true,
        }
    }
}

pub struct ConfigManager;

impl ConfigManager {
    pub fn config_path() -> Option<PathBuf> {
        let base = dirs::data_local_dir()?;
        let new_path = base.join("Lyrix").join("config.json");
        let old_path = base.join("TaskbarLyrics").join("config.json");

        if !new_path.exists() && old_path.exists() {
            if let Some(parent) = new_path.parent() {
                let _ = fs::create_dir_all(parent);
            }
            let _ = fs::copy(&old_path, &new_path);
        }

        Some(new_path)
    }

    pub fn load() -> AppConfig {
        let Some(path) = Self::config_path() else {
            return AppConfig::default();
        };

        if let Ok(content) = fs::read_to_string(path) {
            if let Ok(mut config) = serde_json::from_str::<AppConfig>(&content) {
                if config.width < 400 {
                    config.width = 400;
                }
                if config.height < 42 {
                    config.height = 42;
                }
                return config;
            }
        }

        AppConfig::default()
    }

    pub fn save(config: &AppConfig) -> bool {
        let Some(path) = Self::config_path() else {
            return false;
        };

        if let Some(parent) = path.parent() {
            let _ = fs::create_dir_all(parent);
        }

        if let Ok(json) = serde_json::to_string_pretty(config) {
            return fs::write(path, json).is_ok();
        }

        false
    }
}
