#[allow(dead_code)]
pub mod parser;
#[allow(dead_code)]
pub mod provider;
#[allow(dead_code)]
pub mod sync;

#[allow(unused_imports)]
pub use provider::LyricsProvider;
#[allow(unused_imports)]
pub use sync::{LyricsSyncEngine, PlaybackState};
