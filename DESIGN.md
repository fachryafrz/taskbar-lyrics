# Taskbar Lyrics design direction

- **Source:** user-provided TrafficMonitor reference and implementation brief.
- **User and task:** a Windows listener glances at the taskbar for the current track and active lyric without losing attention from the foreground application.
- **Surface:** a non-interactive, compact Windows taskbar overlay. Settings live in the tray menu; the overlay itself has no controls.
- **Hierarchy:** square album art, then title, then artist, then the active lyric. The lyric gets the greatest contrast and truncates rather than covering taskbar controls.
- **Character:** utilitarian, dense, calm. It should look native beside TrafficMonitor, not like a music player card.
- **Theme:** follow Windows high-contrast preference where detected; otherwise white text over a translucent black surface. This preserves legibility over arbitrary taskbar colors.
- **Typography:** Segoe UI, because it matches Windows system text and remains clear at taskbar scale.
- **Palette:** near-black surface, white primary text, muted gray metadata. No decorative accent or gradient.
- **Dials:** ENERGY 1 / RHYTHM 1 / MOTION 1. A short cross-fade is reserved for lyric changes to confirm fresh playback state without distracting from work.
- **Purpose notes:** album art identifies the current media at a glance; the translucent surface separates text from changing taskbar backgrounds; no borders, cards, badges, or decorative icons are used.
