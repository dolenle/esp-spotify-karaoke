/*
MIT License

Copyright (c) 2022 Dolen Le

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <time.h>
#include <LittleFS.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <Ticker.h>

#include "secrets.h"
#include "lcd2004.h"

#define PLAYBACK_REFRSH_INTERVAL        2000
#define PLAYBACK_RETRY_INTERVAL         250
#define REQUEST_TIMEOUT_MS              500

LCD2004 lcd(2);
Ticker displayTicker;
ESP8266WebServer server(80);

typedef struct {
    String accessToken;
    String refreshToken;
} SpotifyToken;
SpotifyToken auth;

typedef struct {
    unsigned long millis;
    unsigned int progress;
    unsigned int duration;
    String track_name;
    String album_name;
    String artist_name;
    String track_id;
    bool playing;
} SpotifyPlayback;
SpotifyPlayback playback;

String lastTrack;

StaticJsonDocument<12288> lyricDoc;
const char* p_lyric_start;
const char* p_lyric_next = NULL;
const char* p_lyric_current = NULL;
unsigned int next_lyric_ms;

String spotifyAuth() {
    String oneWayCode = "";

    if(!MDNS.begin(MDNS_HOSTNAME)) {
        Serial.println(F("FATAL: MDNS error"));
        while(1) yield();
    }
    Serial.println(F("mDNS started"));

    // Redirect user to Spotify authorization (login) page
    server.on("/", []() {
        server.sendHeader("Location", F("https://accounts.spotify.com/authorize/?client_id=" SP_CLIENT_ID \
                                        "&response_type=code&redirect_uri=" SP_REDIRECT_URI \
                                        "&scope=user-read-private%20user-read-currently-playing%20user-read-playback-state"), true);
        server.send ( 302, "text/plain", "");
    });

    // Retrieve auth code returned by Spotify
    server.on ("/callback/", [&oneWayCode](){
        if(!server.hasArg("code")) {
            server.send(500, "text/plain", "BAD ARGS");
        } else {
            oneWayCode = server.arg("code");
            server.send (200, "text/html", F("Spotify authorization complete. You can close this window."));
        }
    });

    server.begin();
    Serial.println(F("HTTP server started"));

    while(oneWayCode == "") {
        server.handleClient();
        MDNS.update();
        yield();
    }
    server.stop();
    MDNS.close();
    return oneWayCode;
}

void getToken(bool refresh, String code) {
    WiFiClientSecure client;
    client.setInsecure();
    const char* host = "accounts.spotify.com";
    const int port = 443;
    String url = "/api/token";
    if (!client.connect(host, port)) {
        Serial.println("connection failed");
        return;
    }

    String codeParam = "code";
    String grantType = "authorization_code";
    if (refresh) {
        grantType = codeParam = "refresh_token";
    }
    String authorization = base64::encode(F(SP_CLIENT_ID ":" SP_CLIENT_SECRET), false);
    // This will send the request to the server
    String content = "grant_type=" + grantType + "&" + codeParam + "=" + code + "&redirect_uri=" SP_REDIRECT_URI;
    String request = String("POST ") + url + " HTTP/1.1\r\n" +
                             "Host: " + host + "\r\n" +
                             "Authorization: Basic " + authorization + "\r\n" +
                             "Content-Length: " + String(content.length()) + "\r\n" +
                             "Content-Type: application/x-www-form-urlencoded\r\n" +
                             "Connection: close\r\n\r\n" +
                             content;
    client.print(request);

    unsigned long req_start = millis();
    while(!client.available()) {
        if(millis() - req_start > REQUEST_TIMEOUT_MS) {
            Serial.println(F("Request Timeout"));
            return;
        }
        delay(10);
    }

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") {
                break;
        }
    }

    StaticJsonDocument<32> filter;
    filter["access_token"] = true;
    filter["refresh_token"] = true;
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, client, DeserializationOption::Filter(filter));
    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }
    auth.accessToken = (const char*)doc["access_token"];
    auth.refreshToken = (const char*)doc["refresh_token"];
}

int updatePlayback() {
    int ret_code = 0;
    WiFiClientSecure client;
    client.setInsecure();

    String host = "api.spotify.com";
    const int port = 443;
    String url = "/v1/me/player/currently-playing";
    if (!client.connect(host.c_str(), port)) {
        Serial.println(F("Connection failed"));
        return ret_code;
    }

    String request = "GET " + url + " HTTP/1.1\r\n" +
                             "Host: " + host + "\r\n" +
                             "Authorization: Bearer " + auth.accessToken + "\r\n" +
                             "Connection: close\r\n\r\n";
    client.print(request);

    unsigned long req_start = millis();
    while(!client.available()) {
        if(millis() - req_start > REQUEST_TIMEOUT_MS) {
            Serial.println(F("Request Timeout"));
            return ret_code;
        }
        delay(10);
    }

    // Discard header, get HTTP code
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if(line.startsWith(F("HTTP/1"))) {
            ret_code = line.substring(9, line.indexOf(' ', 9)).toInt();
        }
        if(line == "\r") {
                break;
        }
    }

    if(ret_code == 200) {
        StaticJsonDocument<192> filter;
        filter["progress_ms"] = true;
        filter["is_playing"] = true;
        JsonObject filter_item = filter.createNestedObject("item");
        filter_item["name"] = true;
        filter_item["album"]["name"] = true;
        filter_item["duration_ms"] = true;
        filter_item["id"] = true;
        filter_item["artists"][0]["name"] = true;
        StaticJsonDocument<512> doc;

        DeserializationError error = deserializeJson(doc, client, DeserializationOption::Filter(filter));
        if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
            return false;
        }

        playback.progress = doc["progress_ms"];

        JsonObject item = doc["item"];
        playback.album_name = (const char*)item["album"]["name"];
        playback.artist_name = (const char*)item["artists"][0]["name"];
        playback.duration = item["duration_ms"];
        playback.track_id = (const char*)item["id"];
        playback.track_name = (const char*)item["name"];
        playback.playing = doc["is_playing"];

        playback.millis = millis();
    }

    return ret_code;
}

// From https://github.com/plageoj/urlencode
String urlEncode(const char *msg)
{
    const char *hex = "0123456789ABCDEF";
    String encodedMsg;
    encodedMsg.reserve(strlen(msg) + 16);
    encodedMsg = "";

    while (*msg != '\0') {
        if (('a' <= *msg && *msg <= 'z') || ('A' <= *msg && *msg <= 'Z') || ('0' <= *msg && *msg <= '9') || *msg == '-' || *msg == '_' || *msg == '.' || *msg == '~') {
            encodedMsg += *msg;
        } else {
            encodedMsg += '%';
            encodedMsg += hex[*msg >> 4];
            encodedMsg += hex[*msg & 0xf];
        }
        msg++;
    }
    return encodedMsg;
}

void getLyrics() {
    static String mxmCookie;
    WiFiClientSecure client;
    HTTPClient http;
    p_lyric_next = p_lyric_current = p_lyric_start = NULL;
    client.setInsecure(); //Bad!
    String track = urlEncode(playback.track_name.c_str());
    String artist = urlEncode(playback.artist_name.c_str());
    String uri = "https://apic-desktop.musixmatch.com/ws/1.1/macro.subtitles.get?format=json&namespace=lyrics_synched&subtitle_format=lrc&app_id=web-desktop-app-v1.0&usertoken=" MM_TOKEN \
        "&q_track="+ track +
        "&q_artist=" + artist +
        "&q_duration=" + playback.duration;
    Serial.println(uri);

    http.begin(client, uri);
    const char* headers[] = {"Set-Cookie"};
    http.collectHeaders(headers, 1);

    if(mxmCookie != "") {
        http.addHeader("Cookie", mxmCookie);
    }

    int code = http.GET();
    Serial.print("Response code: ");
    Serial.println(code);
    if(code == 301) {
        // Redirect: save the cookies and send them back.
        mxmCookie = http.header("Set-Cookie");
        delay(100);
        http.addHeader("Cookie", mxmCookie);
        code = http.GET();
        Serial.print("Response code: ");
        Serial.println(code);
    } else if(code != 200) {
        return;
    }

    StaticJsonDocument<192> filter;

    JsonObject lyric_filter = filter["message"]["body"]["macro_calls"]["track.subtitles.get"].createNestedObject("message");
    lyric_filter["header"]["available"] = true;
    lyric_filter["body"]["subtitle_list"][0]["subtitle"]["subtitle_body"] = true;

    DeserializationError error = deserializeJson(lyricDoc, client, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(12));
    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }

    int lyric_available = lyricDoc["message"]["body"]["macro_calls"]["track.subtitles.get"]["message"]["header"]["available"];
    if(lyric_available) {
        p_lyric_start = lyricDoc["message"]["body"]["macro_calls"]["track.subtitles.get"]["message"]["body"]["subtitle_list"][0]["subtitle"]["subtitle_body"];
        p_lyric_next = p_lyric_start;
    }

    http.end();
}

void saveRefreshToken(String refreshToken) {
    File f = LittleFS.open(F("/sptoken.txt"), "w");
    if (!f) {
        Serial.println(F("Failed to write sptoken"));
        return;
    }
    f.println(refreshToken);
    f.close();
    Serial.println(F("Saved token"));
}

String loadRefreshToken() {
    File f = LittleFS.open(F("/sptoken.txt"), "r");
    if (!f) {
        Serial.println(F("Failed to read sptoken"));
        return "";
    }
    while(f.available()) {
        String token = f.readStringUntil('\r');
        Serial.println(F("Loaded token"));
        f.close();
        return token;
    }
    return "";
}

unsigned int parseInt(const char* ptr) {
    unsigned int ret = 0;
    while(*ptr >= '0' && *ptr <= '9') {
        ret *= 10;
        ret += (*ptr++ - '0');
    }
    return ret;
}

// Advance the lyric pointer to the next line to be displayed
//[MM:SS.TT] Lyric Line\n
bool nextLyric() {
    unsigned int lyric_min = 0;
    unsigned int lyric_sec = 0;
    unsigned int lyric_ms = 0;
    if(p_lyric_next && *p_lyric_next++ == '[') {
        lyric_min = parseInt(p_lyric_next);
        while(*p_lyric_next++ != ':');
        lyric_sec = parseInt(p_lyric_next);
        while(*p_lyric_next++ != '.');
        lyric_ms = parseInt(p_lyric_next);
        while(*p_lyric_next++ != ' ');

        next_lyric_ms = lyric_min*60000 + lyric_sec*1000 + lyric_ms;

        if(!*p_lyric_next) { // The last lyric is an empty string.
            p_lyric_next = NULL;
            next_lyric_ms = UINT_MAX;
        }
        return true;
    } else {
        p_lyric_next = NULL;
        next_lyric_ms = UINT_MAX;
        return false; //error
    }
}

// Print the string to the LCD with word wrapping.
// The diplayed string is truncated if it is too long.
// Returns the length of the string.
size_t printWrap(const char* str, const char end) {
    lcd.clear();
    char c;
    size_t len = 0;
    unsigned int col = 0;
    unsigned int line = 0;
    while((c = *str++) != end) {
        len++;
        unsigned int wcnt = 1;
        if(c == ' ') {
            const char* tmp = str;
            while(*tmp && *tmp != ' ' && *tmp++ != end) {
                wcnt++;
            }
            if(col == 0) {
                continue;
            } else if(col + wcnt > LCD_COLS && wcnt < LCD_COLS) {
                lcd.setCursor(0, ++line);
                col = 0;
                continue;
            }
        }
        if(col < LCD_COLS && line < LCD_LINES) {
            lcd.write(c);
        }
        if(++col == LCD_COLS) {
            lcd.setCursor(0, ++line);
            col = 0;
        }
    }
    return len;
}

void displayLyric() {
    p_lyric_current = p_lyric_next;
    size_t len = printWrap(p_lyric_next, '\n');
    p_lyric_next+= (len+1);
    if(nextLyric()) {
        unsigned int lyric_delay = next_lyric_ms - ((unsigned int)(millis() - playback.millis) + playback.progress);
        displayTicker.once_ms(lyric_delay, displayLyric);
    }
}

void startLyric(bool force) {
    unsigned int progress_ms = (millis() - playback.millis) + playback.progress;
    const char* last_lyric = NULL;
    while(progress_ms > next_lyric_ms) {
        last_lyric = p_lyric_next;
        while(*p_lyric_next++ != '\n'); // skip
        if(!nextLyric() || !p_lyric_next) {
            break;
        }
    }
    // Display the last lyric
    if(force || p_lyric_current != last_lyric) {
        Serial.println("<RESYNC>");
        if(last_lyric) {
            printWrap(last_lyric, '\n');
        } else if(!force) {
            lcd.clear();
        }
        p_lyric_current = last_lyric;
    }
    // Schedule the next lyric
    if(p_lyric_next) {
        unsigned int lyric_delay = next_lyric_ms - progress_ms;
        displayTicker.once_ms(lyric_delay, displayLyric);
    }
}

void setup() {
    Serial.begin(115200);
    if(!LittleFS.begin()) {
        Serial.println(F("FATAL: filesystem error"));
        while(1) yield();
    }
    lcd.begin();
    lcd.clear();
    lcd.print("Connecting to");
    lcd.setCursor(0,1);
    lcd.print(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while(WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print('.');
    }
    Serial.println(F("Connected"));
    Serial.println(WiFi.localIP());
    lcd.clear();
    lcd.print("Connected");
    lcd.setCursor(0,1);
    lcd.print(WiFi.localIP());

    String refreshToken = loadRefreshToken();
    if (refreshToken == "") {
        lcd.setCursor(0,0);
        lcd.print("Please visit");
        lcd.setCursor(0,2);
        lcd.print("in your web browser");
        String authCode = spotifyAuth();
        getToken(false, authCode);
        lcd.clear();
        lcd.print("Connected!");
    } else {
        getToken(true, refreshToken);
    }
    if (auth.refreshToken != "") {
        saveRefreshToken(auth.refreshToken);
    } else if(auth.accessToken == "") {
        Serial.println(F("Auth failed! Please check API credentials."));
        lcd.clear();
        lcd.print("Spotify auth failed!");
        LittleFS.remove(F("/sptoken.txt"));
        while(1) yield();
    }
}

void loop() {
    static unsigned long last_update = 0;
    unsigned long now = millis();
    unsigned int progress_ms = (unsigned int)(now - playback.millis) + playback.progress;
    static const char* last_printed = NULL;
        static int flag = 0;

    if(now - last_update > PLAYBACK_REFRSH_INTERVAL || progress_ms > playback.duration) {
        int ret_code = updatePlayback();
        last_update = millis();
        if(ret_code == 200) {
            // Serial.print("UpdateTime: ");
            // Serial.println(last_update-now);

            displayTicker.detach();
            if(playback.playing) {
                // If current track changed, reload lyrics
                if(playback.track_id != lastTrack) {
                    Serial.println();
                    Serial.println(playback.track_name);
                    Serial.println(playback.artist_name);
                    char line_buf[21];
                    lcd.clear();
                    snprintf(line_buf, sizeof(line_buf), playback.track_name.c_str());
                    lcd.print(line_buf);
                    snprintf(line_buf, sizeof(line_buf), playback.artist_name.c_str());
                    lcd.setCursor(0,1);
                    lcd.print(line_buf);
                    lastTrack = playback.track_id;
                    progress_ms = playback.progress;
                    getLyrics();
                    if(!nextLyric()) {
                        lcd.setCursor(0,3);
                        lcd.print("(No Synced Lyrics)");
                    } else {
                        startLyric(true);
                    }
                } else if(p_lyric_start) {
                    // Re-sync lyrics
                    p_lyric_next = p_lyric_start;
                    if(nextLyric()) {
                        startLyric(false);
                    }
                }
            } else {
                Serial.println("<PAUSED>");
            }
        } else if(ret_code == 401) { // Unauthorized (access token expired)
            String refreshToken = loadRefreshToken();
            getToken(true, refreshToken);
            if(auth.refreshToken != "") {
                saveRefreshToken(auth.refreshToken);
            }
        } else if(ret_code == 204) { // No Content (nothing playing)
            Serial.println("<STOPPED>");
            displayTicker.detach();
            lcd.clear();
            lcd.print("Playback Stopped.");
        } else {
            Serial.print("Retry ");
            Serial.println(ret_code);
            last_update += PLAYBACK_REFRSH_INTERVAL - PLAYBACK_RETRY_INTERVAL;
        }
    } else if(p_lyric_current && p_lyric_current != last_printed && !flag) {
        const char* c = p_lyric_current;
        while(*c != '\n') {
            Serial.write(*c++);
        }
        Serial.write('\n');
        last_printed = p_lyric_current;
    }
}
