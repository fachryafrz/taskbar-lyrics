pub mod config;
pub mod updater;

pub use config::{AppConfig, ConfigManager, SettingsUpdate};
pub use updater::{fetch_latest_release, load_cached_update, UpdateInfo};
