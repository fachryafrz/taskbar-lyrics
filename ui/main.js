// Lyrix Overlay Client Logic (High Precision Sync & LRCLIB Matching)
document.addEventListener("DOMContentLoaded", () => {
  const overlay = document.getElementById("overlay");
  const albumArtContainer = document.getElementById("album-art-container");
  const albumArt = document.getElementById("album-art");
  const activeLyric = document.getElementById("active-lyric");

  let isDragging = false;
  let startX = 0;
  let startY = 0;
  let isLocked = false;

  let lyricOffsetMs = 300;
  let currentDisplayedText = "";
  let currentTrackKey = "";
  let currentLyrics = []; // [{ timeMs, text }]
  let lyricsCache = new Map();

  let track = {
    title: "",
    artist: "",
    album: "",
    album_art_base64: null,
    is_playing: false,
    position_ms: 0,
    duration_ms: 0,
    has_media: false,
  };

  // --- 1. Mouse Dragging & Positioning ---
  overlay.addEventListener("mousedown", (e) => {
    if (e.button !== 0 || isLocked) return;
    isDragging = true;
    startX = e.screenX;
    startY = e.screenY;
    overlay.classList.add("dragging");
  });

  window.addEventListener("mousemove", (e) => {
    if (!isDragging) return;
    const dx = e.screenX - startX;
    const dy = e.screenY - startY;
    if (dx !== 0 || dy !== 0) {
      startX = e.screenX;
      startY = e.screenY;
      if (window.__TAURI__ && window.__TAURI__.core) {
        window.__TAURI__.core.invoke("move_window_by", { deltaX: dx, deltaY: dy });
      }
    }
  });

  window.addEventListener("mouseup", () => {
    if (isDragging) {
      isDragging = false;
      overlay.classList.remove("dragging");
      if (window.__TAURI__ && window.__TAURI__.core) {
        window.__TAURI__.core.invoke("save_window_position");
      }
    }
  });

  // --- 2. LRC Parser ---
  function parseLrc(lrcText) {
    if (!lrcText) return [];
    const lines = lrcText.split("\n");
    const result = [];
    const timeRegex = /\[(\d{2}):(\d{2})\.(\d{2,3})\]/g;

    for (const rawLine of lines) {
      const trimmed = rawLine.trim();
      if (!trimmed) continue;
      if (/^\[(ti|ar|al|by|re|ve|length|offset):/i.test(trimmed)) continue;

      let match;
      const timestamps = [];
      let text = trimmed;

      while ((match = timeRegex.exec(trimmed)) !== null) {
        const minutes = parseInt(match[1], 10);
        const seconds = parseInt(match[2], 10);
        const fraction = match[3];
        const ms = fraction.length === 2 ? parseInt(fraction, 10) * 10 : parseInt(fraction, 10);
        timestamps.push(minutes * 60 * 1000 + seconds * 1000 + ms);
        text = text.replace(match[0], "").trim();
      }

      for (const timeMs of timestamps) {
        result.push({ timeMs, text });
      }
    }

    return result.sort((a, b) => a.timeMs - b.timeMs);
  }

  // --- 3. Track Metadata Normalization ---
  function normalizeTrackIdentifier(rawTitle, rawArtist) {
    let title = (rawTitle || "").trim();
    let artist = (rawArtist || "").trim();

    title = title.replace(/^[▶❚►|•\s]+/, "").trim();
    title = title.replace(/^\(\d+\+?\)\s*/, "").trim();

    title = title
      .replace(/\s*-\s*YouTube Music$/i, "")
      .replace(/\s*-\s*YouTube$/i, "")
      .replace(/\s*-\s*Topic$/i, "")
      .trim();

    artist = artist
      .replace(/\s*-\s*YouTube Music$/i, "")
      .replace(/\s*-\s*YouTube$/i, "")
      .replace(/\s*-\s*Topic$/i, "")
      .trim();

    const isBrowser = /chrome|msedge|firefox|opera|brave/i.test(artist);

    if (title.includes(" - ")) {
      const parts = title.split(" - ");
      if (parts.length >= 2) {
        if (isBrowser || !artist || artist.toLowerCase() === "unknown") {
          artist = parts[0].trim();
          title = parts.slice(1).join(" - ").trim();
        }
      }
    }

    const cleanTitle = title
      .replace(/\[(official\s*(music)?\s*video|official\s*audio|mv|visualizer|hd|4k|lyric\s*video)\]/gi, "")
      .replace(/\((official\s*(music)?\s*video|official\s*audio|mv|visualizer|hd|4k|lyric\s*video|lyrics)\)/gi, "")
      .replace(/ft\..*$/i, "")
      .replace(/feat\..*$/i, "")
      .trim();

    const cleanArtist = artist.trim();
    const key = `${cleanTitle.toLowerCase()}:::${cleanArtist.toLowerCase()}`;

    return { key, cleanTitle: cleanTitle || title, cleanArtist };
  }

  function findBestLyricsMatch(items, targetTitle, targetArtist, targetDurationMs) {
    if (!items || items.length === 0) return null;
    const normTitle = targetTitle.toLowerCase().trim();
    const normArtist = targetArtist.toLowerCase().trim();

    let bestItem = null;
    let highestScore = -1;

    for (const item of items) {
      if (!item.syncedLyrics && !item.plainLyrics) continue;
      let score = 0;
      const itemTitle = (item.trackName || "").toLowerCase().trim();
      const itemArtist = (item.artistName || "").toLowerCase().trim();

      if (item.syncedLyrics && item.syncedLyrics.trim().length > 0) {
        score += 50;
      }

      if (itemTitle === normTitle) {
        score += 100;
      } else if (itemTitle.includes(normTitle) || normTitle.includes(itemTitle)) {
        score += 40;
      }

      if (normArtist && !/chrome|msedge|firefox|opera|brave/i.test(normArtist)) {
        if (itemArtist === normArtist) {
          score += 80;
        } else if (itemArtist.includes(normArtist) || normArtist.includes(itemArtist)) {
          score += 30;
        }
      }

      if (targetDurationMs && targetDurationMs > 0 && item.duration) {
        const durDiff = Math.abs(item.duration - Math.round(targetDurationMs / 1000));
        if (durDiff <= 3) {
          score += 40;
        } else if (durDiff <= 10) {
          score += 20;
        }
      }

      if (score > highestScore) {
        highestScore = score;
        bestItem = item;
      }
    }

    return bestItem || items.find((i) => i.syncedLyrics) || items[0] || null;
  }

  // --- 4. Fetch Lyrics from LRCLIB ---
  async function fetchLyrics(rawTitle, rawArtist, album, durationMs) {
    const { key, cleanTitle, cleanArtist } = normalizeTrackIdentifier(rawTitle, rawArtist);
    if (!cleanTitle) return [];

    if (lyricsCache.has(key)) {
      return lyricsCache.get(key);
    }

    try {
      // 1. Direct /api/get
      if (cleanArtist && !/chrome|msedge|firefox|opera|brave/i.test(cleanArtist)) {
        const params = new URLSearchParams({
          track_name: cleanTitle,
          artist_name: cleanArtist,
        });
        if (album) params.append("album_name", album);
        if (durationMs > 0) {
          params.append("duration", Math.round(durationMs / 1000).toString());
        }

        const res = await fetch(`https://lrclib.net/api/get?${params.toString()}`);
        if (res.ok) {
          const data = await res.json();
          if (data.syncedLyrics) {
            const parsed = parseLrc(data.syncedLyrics);
            lyricsCache.set(key, parsed);
            return parsed;
          }
        }
      }

      // 2. Search query with "Title Artist"
      const query = cleanArtist && !/chrome|msedge|firefox|opera|brave/i.test(cleanArtist)
        ? `${cleanTitle} ${cleanArtist}`
        : cleanTitle;

      const searchRes = await fetch(`https://lrclib.net/api/search?q=${encodeURIComponent(query)}`);
      if (searchRes.ok) {
        const items = await searchRes.json();
        if (Array.isArray(items) && items.length > 0) {
          const matched = findBestLyricsMatch(items, cleanTitle, cleanArtist, durationMs);
          if (matched && matched.syncedLyrics) {
            const parsed = parseLrc(matched.syncedLyrics);
            lyricsCache.set(key, parsed);
            return parsed;
          }
        }
      }

      // 3. Fallback search with clean title alone
      if (cleanTitle !== query) {
        const fallbackRes = await fetch(`https://lrclib.net/api/search?q=${encodeURIComponent(cleanTitle)}`);
        if (fallbackRes.ok) {
          const items = await fallbackRes.json();
          if (Array.isArray(items) && items.length > 0) {
            const matched = findBestLyricsMatch(items, cleanTitle, cleanArtist, durationMs);
            if (matched && matched.syncedLyrics) {
              const parsed = parseLrc(matched.syncedLyrics);
              lyricsCache.set(key, parsed);
              return parsed;
            }
          }
        }
      }
    } catch (e) {
      console.warn("LRCLIB fetch error:", e);
    }

    return [];
  }

  // --- 5. Display & Fade-Up Transition ---
  function updateLyricDisplay(text) {
    if (!text || text === currentDisplayedText) return;
    currentDisplayedText = text;

    activeLyric.classList.remove("fade-up");
    void activeLyric.offsetWidth; // Force CSS reflow to restart animation
    activeLyric.textContent = text;
    activeLyric.classList.add("fade-up");
  }

  // --- 6. Active Lyric Evaluator ---
  function updateActiveLyric() {
    if (!track.has_media || (!track.title && !track.artist)) {
      updateLyricDisplay("Waiting for playback...");
      return;
    }

    if (!currentLyrics || currentLyrics.length === 0) {
      const banner = track.title && track.artist ? `${track.title} - ${track.artist}` : (track.title || "Waiting for playback...");
      updateLyricDisplay(banner);
      return;
    }

    const currentMs = track.position_ms + lyricOffsetMs;
    let foundIdx = -1;

    for (let i = 0; i < currentLyrics.length; i++) {
      if (currentMs >= currentLyrics[i].timeMs) {
        foundIdx = i;
      } else {
        break;
      }
    }

    if (foundIdx < 0) {
      // Intro before first lyric arrives
      const banner = track.title && track.artist ? `${track.title} - ${track.artist}` : (track.title || "♪");
      updateLyricDisplay(banner);
    } else {
      const lineText = currentLyrics[foundIdx].text;
      if (!lineText || lineText.trim().length === 0) {
        // Instrumental interlude / music only
        updateLyricDisplay("♪");
      } else {
        updateLyricDisplay(lineText);
      }
    }
  }

  // --- 7. Track Synchronization & Handling ---
  async function handleTrackUpdate(currentTrack) {
    if (!currentTrack) return;

    const hasValidMedia = currentTrack.has_media && currentTrack.title.trim().length > 0;
    if (!hasValidMedia) {
      track.has_media = false;
      track.is_playing = false;
      currentTrackKey = "";
      currentLyrics = [];
      albumArt.src = "";
      albumArtContainer.classList.add("hidden");
      updateActiveLyric();
      return;
    }

    const { key, cleanTitle, cleanArtist } = normalizeTrackIdentifier(currentTrack.title, currentTrack.artist);

    // Album Art
    if (currentTrack.album_art_base64) {
      albumArt.src = currentTrack.album_art_base64;
      albumArtContainer.classList.remove("hidden");
    } else {
      albumArt.src = "";
      albumArtContainer.classList.add("hidden");
    }

    const isSongChanged = currentTrackKey !== "" && currentTrackKey !== key;

    if (isSongChanged || currentTrackKey === "") {
      currentTrackKey = key;
      track = {
        title: cleanTitle,
        artist: cleanArtist,
        album: currentTrack.album || "",
        album_art_base64: currentTrack.album_art_base64,
        is_playing: currentTrack.is_playing,
        position_ms: currentTrack.position_ms || 0,
        duration_ms: currentTrack.duration_ms || 0,
        has_media: true,
      };

      currentLyrics = [];
      updateActiveLyric();

      // Asynchronous lyrics fetch
      const capturedKey = key;
      fetchLyrics(cleanTitle, cleanArtist, currentTrack.album, currentTrack.duration_ms).then((lyrics) => {
        if (currentTrackKey === capturedKey) {
          currentLyrics = lyrics;
          updateActiveLyric();
        }
      });

      return;
    }

    track.is_playing = currentTrack.is_playing;
    track.has_media = true;
    if (currentTrack.duration_ms > 0) {
      track.duration_ms = currentTrack.duration_ms;
    }

    // Smart clock synchronization with GSMTC
    if (currentTrack.position_ms > 0) {
      const diff = Math.abs(currentTrack.position_ms - track.position_ms);
      if (diff > 1200) {
        // User clicked seek
        track.position_ms = currentTrack.position_ms;
      } else if (diff > 200) {
        // Smooth drift correction
        track.position_ms = Math.round(track.position_ms * 0.2 + currentTrack.position_ms * 0.8);
      }
    } else if (!currentTrack.is_playing && currentTrack.position_ms > 0) {
      track.position_ms = currentTrack.position_ms;
    }

    updateActiveLyric();
  }

  // --- 8. High-Precision Playback Clock (50ms / 20 FPS) ---
  let lastTick = performance.now();
  setInterval(() => {
    const now = performance.now();
    const delta = now - lastTick;
    lastTick = now;

    if (!track.is_playing) return;
    const maxLimit = track.duration_ms > 0 ? track.duration_ms : Infinity;
    track.position_ms = Math.min(maxLimit, Math.round(track.position_ms + delta));
    updateActiveLyric();
  }, 50);

  // --- 9. GSMTC Polling Fallback (1000ms) ---
  async function pollMedia() {
    if (window.__TAURI__ && window.__TAURI__.core && window.__TAURI__.core.invoke) {
      try {
        const data = await window.__TAURI__.core.invoke("get_current_media");
        handleTrackUpdate(data);
      } catch (e) {}
    }
  }

  pollMedia();
  setInterval(pollMedia, 1000);

  // --- 10. Tauri Event & Config Listeners ---
  function updateConfigState(cfg) {
    if (!cfg) return;
    if (cfg.locked !== undefined) {
      isLocked = cfg.locked;
      overlay.classList.toggle("locked", isLocked);
    }
    if (cfg.lyricOffsetMilliseconds !== undefined) {
      lyricOffsetMs = cfg.lyricOffsetMilliseconds;
      updateActiveLyric();
    }
    if (cfg.fontSize) {
      activeLyric.style.fontSize = `${cfg.fontSize}px`;
    }
    if (cfg.wrapLongLyrics !== undefined) {
      activeLyric.style.whiteSpace = cfg.wrapLongLyrics ? "normal" : "nowrap";
    }
  }

  if (window.__TAURI__ && window.__TAURI__.event) {
    window.__TAURI__.event.listen("media-update", (event) => {
      handleTrackUpdate(event.payload);
    });

    window.__TAURI__.event.listen("config-updated", (event) => {
      updateConfigState(event.payload);
    });

    window.__TAURI__.event.listen("offset-changed", (event) => {
      if (typeof event.payload === "number") {
        lyricOffsetMs = event.payload;
        updateActiveLyric();
      }
    });

    if (window.__TAURI__.core && window.__TAURI__.core.invoke) {
      window.__TAURI__.core.invoke("get_config").then(updateConfigState).catch(() => {});
    }
  }
});
