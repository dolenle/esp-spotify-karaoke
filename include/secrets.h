#include <Arduino.h>

// Put secret info here!

// WiFi Credentials
#define WIFI_SSID           "wifi-name"
#define WIFI_PASS           "wifi-password"

// Spotify API
#define MDNS_HOSTNAME       "esp8266"
#define SP_CLIENT_ID        "spotify-client-id"
#define SP_CLIENT_SECRET    "spotify-client-secret"
#define SP_REDIRECT_URI     "http://" MDNS_HOSTNAME ".local/callback/"

// Musicxmatch
#define MM_TOKEN            "musicxmatch-token"
