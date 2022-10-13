# ESP8266 Spotify Lyric Thing

This project uses an ESP8266 to fetch and display time-synchronized lyrics for the currently playing track on Spotify. It uses the Spotify API to fetch playback status and track information, and queries MusicXMatch for synced lyrics which are then displayed as the song plays.

## Information

- The code is messy. It is mostly proof of concept.
- Spotify API functions are adapted from the [esp8266-spotify-remote](https://github.com/ThingPulse/esp8266-spotify-remote) project.
- API keys and WiFi credentials go in the `include/secrets.h` file.
- For testing I am using an 20x4 HD44780 character LCD via a PCF8574 I2C expander board, though eventually I'd like a larger LED matrix display.
