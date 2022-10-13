# ESP8266 Spotify Lyric Thing

This project uses an ESP8266 to fetch and display time-synchronized lyrics for the currently playing track on Spotify. It uses the Spotify API to fetch playback status and track information, and queries Musicxmatch for synced lyrics which are then displayed as the song plays.

## Information

- The code is messy. It is mostly proof of concept.
- Spotify API functions are adapted from the [esp8266-spotify-remote](https://github.com/ThingPulse/esp8266-spotify-remote) project.
- API keys and WiFi credentials go in the `include/secrets.h` file.
- For testing I am using an 20x4 HD44780 character LCD via a PCF8574 I2C expander, though eventually I'd like to use larger LED matrix display.

## Known Issues
- Use of dynamic Strings. I suspect these are fragmenting the heap and causing instability over time.
- Playback updates interfere with lyric display timing
- Large latency when fetching lyrics at the start of a new track.
- Seeking backwards is not supported.
- Spotify auth needs renewal after some time.
- Only English lyrics are supported (due to LCD font)
