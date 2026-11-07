use serde::{Deserialize, Serialize};
use std::fs;
use std::path::PathBuf;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
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

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SettingsUpdate {
    pub width: Option<i32>,
    pub height: Option<i32>,
    pub lyric_offset_milliseconds: Option<i32>,
    pub font_size: Option<i32>,
    pub wrap_long_lyrics: Option<bool>,
    pub always_on_top: Option<bool>,
}

impl SettingsUpdate {
    pub fn apply_to(&self, config: &mut AppConfig) -> Result<(), String> {
        if self.width.is_some_and(|width| width < 400) {
            return Err("Width must be at least 400 px.".into());
        }
        if self.height.is_some_and(|height| height < 42) {
            return Err("Height must be at least 42 px.".into());
        }
        if self.font_size.is_some_and(|font_size| font_size < 1) {
            return Err("Font size must be at least 1 px.".into());
        }

        if let Some(width) = self.width {
            config.width = width;
        }
        if let Some(height) = self.height {
            config.height = height;
        }
        if let Some(offset) = self.lyric_offset_milliseconds {
            config.lyric_offset_milliseconds = offset;
        }
        if let Some(font_size) = self.font_size {
            config.font_size = font_size;
        }
        if let Some(wrap) = self.wrap_long_lyrics {
            config.wrap_long_lyrics = wrap;
        }
        if let Some(always_on_top) = self.always_on_top {
            config.always_on_top = always_on_top;
        }

        Ok(())
    }
}

impl Default for AppConfig {
    fn default() -> Self {
        Self {
            width: 400,
            height: 42,
            taskbar_offset_x: 0,
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
        let new_path = base.join("TaskbarLyrics").join("config.json");
        let old_path = base.join("Lyrix").join("config.json");

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

    pub fn apply_settings_update(
        current: &mut AppConfig,
        update: &SettingsUpdate,
    ) -> Result<(), String> {
        Self::apply_settings_update_with(current, update, |config| {
            if Self::save(config) {
                Ok(())
            } else {
                Err("Could not save settings to the Taskbar Lyrics config file.".into())
            }
        })
    }

    fn apply_settings_update_with<F>(
        current: &mut AppConfig,
        update: &SettingsUpdate,
        persist: F,
    ) -> Result<(), String>
    where
        F: FnOnce(&AppConfig) -> Result<(), String>,
    {
        let mut updated = current.clone();
        update.apply_to(&mut updated)?;
        persist(&updated)?;
        *current = updated;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_default_config_position() {
        let config = AppConfig::default();
        assert_eq!(config.taskbar_offset_x, 0);
        assert_eq!(config.taskbar_offset_y, 0);
        assert_eq!(config.width, 400);
        assert_eq!(config.height, 42);
    }

    #[test]
    fn settings_update_changes_only_provided_fields() {
        let mut config = AppConfig {
            taskbar_offset_x: 17,
            taskbar_offset_y: -8,
            locked: true,
            visible: false,
            ..AppConfig::default()
        };

        SettingsUpdate {
            width: Some(640),
            always_on_top: Some(false),
            ..SettingsUpdate::default()
        }
        .apply_to(&mut config)
        .unwrap();

        assert_eq!(config.width, 640);
        assert!(!config.always_on_top);
        assert_eq!(config.taskbar_offset_x, 17);
        assert_eq!(config.taskbar_offset_y, -8);
        assert!(config.locked);
        assert!(!config.visible);
        assert_eq!(config.height, 42);
    }

    #[test]
    fn settings_update_rejects_dimensions_below_existing_minimums() {
        let mut config = AppConfig::default();

        assert!(SettingsUpdate {
            width: Some(399),
            ..SettingsUpdate::default()
        }
        .apply_to(&mut config)
        .is_err());
        assert!(SettingsUpdate {
            height: Some(41),
            ..SettingsUpdate::default()
        }
        .apply_to(&mut config)
        .is_err());
        assert_eq!(config, AppConfig::default());
    }

    #[test]
    fn settings_update_rejects_non_positive_font_size() {
        let mut config = AppConfig::default();

        assert!(SettingsUpdate {
            font_size: Some(0),
            ..SettingsUpdate::default()
        }
        .apply_to(&mut config)
        .is_err());
        assert_eq!(config, AppConfig::default());
    }

    #[test]
    fn failed_settings_persistence_does_not_change_current_config() {
        let mut config = AppConfig::default();
        let before = config.clone();
        let result = ConfigManager::apply_settings_update_with(
            &mut config,
            &SettingsUpdate {
                width: Some(640),
                ..SettingsUpdate::default()
            },
            |_| Err("write failed".into()),
        );

        assert!(result.is_err());
        assert_eq!(config, before);
    }
}
