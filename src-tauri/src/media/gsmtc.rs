use serde::{Deserialize, Serialize};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;
use windows::Media::Control::{
    GlobalSystemMediaTransportControlsSessionManager,
    GlobalSystemMediaTransportControlsSessionPlaybackStatus,
};
use windows::Storage::Streams::DataReader;
use windows::Win32::System::Com::{CoInitializeEx, COINIT_MULTITHREADED};

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct MediaUpdatePayload {
    pub title: String,
    pub artist: String,
    pub album: String,
    pub album_art_base64: Option<String>,
    pub is_playing: bool,
    pub position_ms: u64,
    pub duration_ms: u64,
    pub has_media: bool,
}

pub fn get_current_track() -> Result<MediaUpdatePayload, Box<dyn std::error::Error>> {
    unsafe {
        let _ = CoInitializeEx(None, COINIT_MULTITHREADED);
    }

    let session_manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync()?.get()?;
    let session = match session_manager.GetCurrentSession() {
        Ok(s) => s,
        Err(_) => {
            return Ok(MediaUpdatePayload::default());
        }
    };

    let media_props = session.TryGetMediaPropertiesAsync()?.get()?;
    let playback_info = session.GetPlaybackInfo()?;
    let is_playing = playback_info
        .PlaybackStatus()
        .map(|s| s == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
        .unwrap_or(false);

    let timeline = session.GetTimelineProperties();
    let (position_ms, duration_ms) = if let Ok(tl) = timeline {
        let base_pos = tl.Position().map(|d| (d.Duration / 10_000) as u64).unwrap_or(0);
        let dur = tl.EndTime().map(|d| (d.Duration / 10_000) as u64).unwrap_or(0);

        let pos = if is_playing && base_pos > 0 {
            if let Ok(lut) = tl.LastUpdatedTime() {
                let now_since_epoch = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default();
                let now_filetime = (now_since_epoch.as_nanos() / 100) as i64 + 116_444_736_000_000_000;
                let diff_100ns = now_filetime - lut.UniversalTime;
                if diff_100ns > 0 {
                    let elapsed_ms = (diff_100ns / 10_000) as u64;
                    let adjusted = base_pos + elapsed_ms;
                    if dur > 0 {
                        adjusted.min(dur)
                    } else {
                        adjusted
                    }
                } else {
                    base_pos
                }
            } else {
                base_pos
            }
        } else {
            base_pos
        };

        (pos, dur)
    } else {
        (0, 0)
    };

    let title = media_props.Title().map(|s| s.to_string()).unwrap_or_default();
    let artist = media_props.Artist().map(|s| s.to_string()).unwrap_or_default();
    let album = media_props.AlbumTitle().map(|s| s.to_string()).unwrap_or_default();

    let mut album_art_base64 = None;
    if let Ok(thumb_ref) = media_props.Thumbnail() {
        if let Ok(stream) = thumb_ref.OpenReadAsync() {
            if let Ok(stream_res) = stream.get() {
                if let Ok(size) = stream_res.Size() {
                    if size > 0 && size < 5 * 1024 * 1024 {
                        if let Ok(reader) = DataReader::CreateDataReader(&stream_res) {
                            if reader.LoadAsync(size as u32).and_then(|op| op.get()).is_ok() {
                                let mut bytes = vec![0u8; size as usize];
                                if reader.ReadBytes(&mut bytes).is_ok() {
                                    use base64::engine::general_purpose::STANDARD as BASE64;
                                    use base64::Engine;
                                    let b64 = BASE64.encode(&bytes);
                                    album_art_base64 = Some(format!("data:image/jpeg;base64,{}", b64));
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    let has_media = !title.is_empty();

    Ok(MediaUpdatePayload {
        title,
        artist,
        album,
        album_art_base64,
        is_playing,
        position_ms,
        duration_ms,
        has_media,
    })
}

pub struct MediaSessionWorker {
    running: Arc<AtomicBool>,
}

impl MediaSessionWorker {
    pub fn new() -> Self {
        Self {
            running: Arc::new(AtomicBool::new(false)),
        }
    }

    #[allow(dead_code)]
    pub fn stop(&self) {
        self.running.store(false, Ordering::SeqCst);
    }

    pub fn start<F>(&self, on_update: F)
    where
        F: Fn(MediaUpdatePayload) + Send + Sync + 'static,
    {
        self.running.store(true, Ordering::SeqCst);
        let running = Arc::clone(&self.running);

        std::thread::spawn(move || {
            while running.load(Ordering::SeqCst) {
                if let Ok(track) = get_current_track() {
                    on_update(track);
                }
                std::thread::sleep(Duration::from_millis(1000));
            }
        });
    }
}
