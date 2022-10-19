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

#include "secrets.h"
#include "lcd2004.h"

#define PLAYBACK_REFRSH_INTERVAL    5000
#define PLAYBACK_REFRESH_MARGIN     2000
#define PLAYBACK_RETRY_INTERVAL     250
#define REQUEST_TIMEOUT_MS          500

LCD2004 lcd(D3, D4);

ESP8266WebServer server(80);

typedef struct {
  String accessToken;
  String refreshToken;
} SpotifyToken;
SpotifyToken auth;

typedef struct {
  unsigned long timestamp;
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
const char* p_lyric = NULL;
unsigned int next_lyric_ms;

String spotifyAuth() {
  String oneWayCode = "";

  server.on ( "/", []() {
    Serial.println(clientId);
    Serial.println(redirectUri);
    server.sendHeader("Location", String("https://accounts.spotify.com/authorize/?client_id=" 
      + clientId 
      + "&response_type=code&redirect_uri=" 
      + redirectUri 
      + "&scope=user-read-private%20user-read-currently-playing%20user-read-playback-state"), true);
    server.send ( 302, "text/plain", "");
  } );

  server.on ( "/callback/", [&oneWayCode](){
    if(!server.hasArg("code")) {server.send(500, "text/plain", "BAD ARGS"); return;}
    
    oneWayCode = server.arg("code");
    Serial.printf("Code: %s\n", oneWayCode.c_str());
  
    String message = "<html><head></head><body>Succesfully authentiated This device with Spotify. Restart your device now</body></html>";
  
    server.send ( 200, "text/html", message );
  } );

  server.begin();

  if (WiFi.status() == WL_CONNECTED) {
	  Serial.println("WiFi connected!");
  } else {
	  Serial.println("WiFi not connected!");
  }

  Serial.println ( "HTTP server started" );

  while(oneWayCode == "") {
    server.handleClient();
    MDNS.update();
    yield();
  }
  server.stop();
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
  String authorizationRaw = clientId + ":" + clientSecret;
  String authorization = base64::encode(authorizationRaw, false);
  // This will send the request to the server
  String content = "grant_type=" + grantType + "&" + codeParam + "=" + code;
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

bool updatePlayback() {
  WiFiClientSecure client;
  client.setInsecure();

  String host = "api.spotify.com";
  const int port = 443;
  String url = "/v1/me/player/currently-playing";
  if (!client.connect(host.c_str(), port)) {
    Serial.println("connection failed");
    return false;
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
      return false;
    }
    delay(10);
  }
  
  // Discard header
  while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") {
          break;
      }
  }

  StaticJsonDocument<192> filter;
  filter["timestamp"] = true;
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

  // long long timestamp = doc["timestamp"];
  playback.progress = doc["progress_ms"];

  JsonObject item = doc["item"];
  playback.album_name = (const char*)item["album"]["name"];
  playback.artist_name = (const char*)item["artists"][0]["name"];
  playback.duration = item["duration_ms"];
  playback.track_id = (const char*)item["id"];
  playback.track_name = (const char*)item["name"];
  playback.playing = doc["is_playing"];

  playback.millis = millis();

  return true;
}

// From https://github.com/plageoj/urlencode
String urlEncode(const char *msg)
{
    const char *hex = "0123456789ABCDEF";
    String encodedMsg;
    encodedMsg.reserve(strlen(msg) + 32);
    encodedMsg = "";

    while (*msg != '\0')
    {
        if (('a' <= *msg && *msg <= 'z') || ('A' <= *msg && *msg <= 'Z') || ('0' <= *msg && *msg <= '9') || *msg == '-' || *msg == '_' || *msg == '.' || *msg == '~')
        {
            encodedMsg += *msg;
        }
        else
        {
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
  p_lyric = NULL;
  client.setInsecure(); //Bad!
  String track = urlEncode(playback.track_name.c_str());
  String artist = urlEncode(playback.artist_name.c_str());
  String uri = "https://apic-desktop.musixmatch.com/ws/1.1/macro.subtitles.get?format=json&namespace=lyrics_synched&subtitle_format=lrc&app_id=web-desktop-app-v1.0&q_track="+ track +
    "&q_artist=" + artist +
    "&q_duration=" + playback.duration +
    "&usertoken=" + mm_token;
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
    Serial.println("cookies!");
    // Save the cookies and send them back.
    mxmCookie = http.header("Set-Cookie");
    delay(100);
    http.addHeader("Cookie", mxmCookie);
    Serial.println(http.GET());
  } else if(code == 404) {
    Serial.println("404");
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
    p_lyric = lyricDoc["message"]["body"]["macro_calls"]["track.subtitles.get"]["message"]["body"]["subtitle_list"][0]["subtitle"]["subtitle_body"];
  }

  http.end();
}

void saveRefreshToken(String refreshToken) {
  File f = LittleFS.open("/sptoken.txt", "w+");
  if (!f) {
    Serial.println("Failed to open sptoken");
    return;
  }
  f.println(refreshToken);
  f.close();
  Serial.println("Saved sptoken");
}

String loadRefreshToken() {
  File f = LittleFS.open(F("/sptoken.txt"), "r");
  if (!f) {
    Serial.println("Failed to open sptoken");
    return "";
  }
  while(f.available()) {
      //Lets read line by line from the file
      String token = f.readStringUntil('\r');
      Serial.print("Loaded Token: ");
      Serial.println(token);
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
  if(p_lyric && *p_lyric++ == '[') {
    lyric_min = parseInt(p_lyric);
    while(*p_lyric++ != ':');
    lyric_sec = parseInt(p_lyric);
    while(*p_lyric++ != '.');
    lyric_ms = parseInt(p_lyric);
    while(*p_lyric++ != ' ');

    next_lyric_ms = lyric_min*60000 + lyric_sec*1000 + lyric_ms;

    if(!*p_lyric) { // The last lyric is an empty string.
      p_lyric = NULL;
      next_lyric_ms = UINT_MAX;
    }
    return true;
  } else {
    Serial.println("Lyric err");
    p_lyric = NULL;
    next_lyric_ms = UINT_MAX;
    return false; //error
  }
}

void setup() {
  Serial.begin(115200);
  if(!LittleFS.begin()) {
    Serial.println(F("FATAL: filesystem error"));
    while(1);
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
  Serial.println("Connected");
  Serial.println(WiFi.localIP());
  lcd.clear();
  lcd.print("Connected");
  lcd.setCursor(0,1);
  lcd.print(WiFi.localIP());

  if (!MDNS.begin("esp8266")) {
    Serial.println("Error setting up MDNS responder!");
  }
  Serial.println("mDNS responder started");

  String refreshToken = loadRefreshToken();
  if (refreshToken == "") {
    String authCode = spotifyAuth();
    getToken(false, authCode);
  } else {
    getToken(true, refreshToken);
  }
  if (auth.refreshToken != "") {
    saveRefreshToken(auth.refreshToken);
  }
}

void loop() {
  static unsigned long last_update = 0;
  unsigned long now = millis();
  unsigned int progress_ms = (unsigned int)(now - playback.millis) + playback.progress;

  if(playback.playing && p_lyric && progress_ms > next_lyric_ms) {
      lcd.clear();
      char c;
      int cnt = 0;
      int line = 0;
      while((c = *p_lyric++) != '\n') {
        Serial.write(c);
        int wcnt = 1;
        if(c == ' ') {
          // Find the length of the next word and wrap to next line if needed
          const char* tmp = p_lyric;
          while(*tmp && *tmp != ' ' && *tmp++ != '\n') {
            wcnt++;
          }
          if(cnt == 0) {
            continue;
          } else if(cnt + wcnt > 20 && wcnt < 20) {
            lcd.setCursor(0, ++line);
            cnt = 0;
            continue;
          }
        }
        if(cnt < 20 && line < 4) {
          lcd.write(c);
        }
        if(++cnt == 20) {
          lcd.setCursor(0, ++line);
          cnt = 0;
        }
      }
      Serial.write('\n');
      nextLyric();
  } else {
    if(now - last_update > PLAYBACK_REFRSH_INTERVAL || progress_ms > playback.duration) {
      if(!playback.playing || next_lyric_ms - progress_ms > PLAYBACK_REFRESH_MARGIN) {
        unsigned long start = millis();
        if(updatePlayback()) {
          Serial.print("UpdateTime: ");
          Serial.println(millis() - start);
          last_update = now;

          // If current track changed, reload lyrics
          if(playback.playing && playback.track_id != lastTrack) {
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
            getLyrics();
            if(!nextLyric()) {
              lcd.setCursor(0,3);
              lcd.print("(No Synced Lyrics)");
            }
          }
          Serial.println(playback.progress);
        } else {
          last_update = now + PLAYBACK_REFRSH_INTERVAL - PLAYBACK_RETRY_INTERVAL;  //retry
        }
      }
    }
  }
}
