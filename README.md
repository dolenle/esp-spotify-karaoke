# ESP8266 Spotify Karaoke

This project uses an ESP8266 to display time-synchronized lyrics for the currently playing track on Spotify. It uses the Spotify REST API to fetch playback status and track information, and queries Musixmatch for synced lyrics which are then shown on the LCD screen. The ArduinoJSON library is used to parse the responses from the Spotify and Musixmatch APIs.

## Setup Information
- Spotify API related functions are adapted from the [esp8266-spotify-remote](https://github.com/ThingPulse/esp8266-spotify-remote) project.
- Place API keys and WiFi credentials in the `include/secrets.h` file. You will need to set up a Spotify developer app and authorize it to read your playback information.
- See [this page](https://github.com/khanhas/genius-spicetify/blob/master/README.md) for information on getting a Musixmatch API token.
- Lyrics are also printed over UART as the song plays. If lyrics are not available, only the track title and artist will be displayed.

## Demo
[![YouTube Video](https://img.youtube.com/vi/Cu1QnanJCE4/0.jpg)](https://www.youtube.com/watch?v=Cu1QnanJCE4)

## Known Limitations
- Delay when fetching lyrics at the start of a new track. If the vocals start right at the beginning of the song, they might get skipped.
  - The Spotify REST API [does not support the play queue](https://github.com/spotify/web-api/issues/462). Otherwise, it would be possible to prefetch lyrics before the next song starts playing.
- Only English lyrics are supported
  - Non-ASCII characters such as diacritics will not be displayed correctly by the LCD.
- The lyric buffer is statically allocated and in theory an extremely long and verbose song would be too large to fit.
- Lyric re-syncronization sometimes causes display to glitch.
- Lines which don't fit on the display are truncated.
- HTTPS requests are performed with TLS verification disabled
- The code is messy. It is mostly proof of concept.

## TODO/Ideas
- Avoid truncating track and artist names which don't fit on a single line. (Maybe scroll them?)
- Show playback progress bar (when lyrics aren't available).
- Show all artist names, not just the first one.
- Implement configuration GUI (e.g. WifiManager) instead of compile-time settings.
- Use the LCD controller's custom character slots to display unsupported characters, where possible.
- Look into other lyric APIs.
- For testing I am using a 20x4 HD44780 character LCD, though eventually I'd like to use a larger LED matrix display.
