use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct UpdateInfo {
    pub current_version: String,
    pub latest_version: String,
    pub update_available: bool,
    pub release_name: String,
    pub release_notes: String,
    pub html_url: String,
    pub published_at: Option<String>,
    pub etag: Option<String>,
}

#[derive(Debug, Deserialize)]
struct GitHubRelease {
    tag_name: String,
    name: Option<String>,
    body: Option<String>,
    html_url: String,
    published_at: Option<String>,
}

pub fn parse_version(v: &str) -> Option<(u32, u32, u32)> {
    let clean = v.trim().trim_start_matches(|c| c == 'v' || c == 'V');
    let mut parts = clean.split('.');
    let major = parts.next()?.parse().ok()?;
    let minor = parts.next()?.parse().ok()?;
    let patch = parts
        .next()?
        .split(|c: char| !c.is_ascii_digit())
        .next()?
        .parse()
        .ok()?;
    Some((major, minor, patch))
}

pub fn is_newer_version(latest: &str, current: &str) -> bool {
    match (parse_version(latest), parse_version(current)) {
        (Some((l_maj, l_min, l_patch)), Some((c_maj, c_min, c_patch))) => {
            (l_maj, l_min, l_patch) > (c_maj, c_min, c_patch)
        }
        _ => {
            let l_clean = latest.trim().trim_start_matches(|c| c == 'v' || c == 'V');
            let c_clean = current.trim().trim_start_matches(|c| c == 'v' || c == 'V');
            !l_clean.is_empty() && l_clean != c_clean
        }
    }
}

fn cache_file_path() -> Option<PathBuf> {
    let base = dirs::data_local_dir()?;
    Some(base.join("TaskbarLyrics").join("update_cache.json"))
}

pub fn load_cached_update() -> Option<UpdateInfo> {
    let path = cache_file_path()?;
    let data = std::fs::read_to_string(path).ok()?;
    serde_json::from_str(&data).ok()
}

pub fn save_cached_update(info: &UpdateInfo) {
    if let Some(path) = cache_file_path() {
        if let Some(parent) = path.parent() {
            let _ = std::fs::create_dir_all(parent);
        }
        if let Ok(data) = serde_json::to_string_pretty(info) {
            let _ = std::fs::write(path, data);
        }
    }
}

pub async fn fetch_latest_release(
    repo: &str,
    current_version: &str,
    cached_info: Option<&UpdateInfo>,
) -> Result<UpdateInfo, String> {
    let url = format!("https://api.github.com/repos/{}/releases/latest", repo);
    let client = reqwest::Client::builder()
        .timeout(std::time::Duration::from_secs(10))
        .build()
        .map_err(|e| e.to_string())?;

    let mut request = client
        .get(&url)
        .header("User-Agent", format!("taskbar-lyrics/{}", current_version))
        .header("Accept", "application/vnd.github.v3+json");

    if let Some(cached) = cached_info {
        if let Some(ref etag) = cached.etag {
            request = request.header("If-None-Match", etag.as_str());
        }
    }

    let response = request
        .send()
        .await
        .map_err(|e| format!("Failed to reach update server: {}", e))?;

    // HTTP 304 Not Modified: GitHub release has not changed; consumes 0 rate limit quota
    if response.status() == reqwest::StatusCode::NOT_MODIFIED {
        if let Some(cached) = cached_info {
            let mut refreshed = cached.clone();
            refreshed.current_version = current_version.to_string();
            refreshed.update_available = is_newer_version(&refreshed.latest_version, current_version);
            save_cached_update(&refreshed);
            return Ok(refreshed);
        }
    }

    if response.status() == reqwest::StatusCode::NOT_FOUND {
        let not_found_info = UpdateInfo {
            current_version: current_version.to_string(),
            latest_version: current_version.to_string(),
            update_available: false,
            release_name: String::new(),
            release_notes: String::new(),
            html_url: format!("https://github.com/{}", repo),
            published_at: None,
            etag: None,
        };
        save_cached_update(&not_found_info);
        return Ok(not_found_info);
    }

    if response.status() == reqwest::StatusCode::FORBIDDEN
        || response.status() == reqwest::StatusCode::TOO_MANY_REQUESTS
    {
        if let Some(cached) = cached_info {
            return Ok(cached.clone());
        }
        return Err("GitHub API rate limit exceeded. Please try again later.".to_string());
    }

    if !response.status().is_success() {
        return Err(format!("Update check failed with HTTP {}", response.status()));
    }

    let etag = response
        .headers()
        .get(reqwest::header::ETAG)
        .and_then(|val| val.to_str().ok())
        .map(|s| s.to_string());

    let release: GitHubRelease = response
        .json()
        .await
        .map_err(|e| format!("Failed to parse release payload: {}", e))?;

    let clean_latest = release
        .tag_name
        .trim_start_matches(|c| c == 'v' || c == 'V')
        .to_string();
    let has_update = is_newer_version(&clean_latest, current_version);

    let info = UpdateInfo {
        current_version: current_version.to_string(),
        latest_version: clean_latest,
        update_available: has_update,
        release_name: release.name.unwrap_or_else(|| release.tag_name.clone()),
        release_notes: release.body.unwrap_or_default(),
        html_url: release.html_url,
        published_at: release.published_at,
        etag,
    };

    save_cached_update(&info);
    Ok(info)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_version() {
        assert_eq!(parse_version("0.1.0"), Some((0, 1, 0)));
        assert_eq!(parse_version("v0.1.0"), Some((0, 1, 0)));
        assert_eq!(parse_version("V1.2.3"), Some((1, 2, 3)));
        assert_eq!(parse_version("v1.2.3-beta1"), Some((1, 2, 3)));
        assert_eq!(parse_version("invalid"), None);
    }

    #[test]
    fn test_is_newer_version() {
        assert!(is_newer_version("0.2.0", "0.1.0"));
        assert!(is_newer_version("v0.2.0", "0.1.0"));
        assert!(is_newer_version("v1.0.0", "v0.9.9"));
        assert!(is_newer_version("0.1.1", "0.1.0"));
        assert!(!is_newer_version("0.1.0", "0.1.0"));
        assert!(!is_newer_version("v0.1.0", "0.1.0"));
        assert!(!is_newer_version("0.0.9", "0.1.0"));
    }
}
