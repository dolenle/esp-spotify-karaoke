#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <time.h>
#include <LittleFS.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>

#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

#include "secrets.h"

LiquidCrystal_I2C lcd(0x27,2,1,0,4,5,6,7,3,POSITIVE);
ESP8266WebServer server(80);

typedef struct {
  String accessToken;
  String refreshToken;
} SpotifyAuth;
SpotifyAuth auth;

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

StaticJsonDocument<8192> lyricDoc;
const char* lyricP;
unsigned int nextLyricMs;

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
  //https://accounts.spotify.com/api/token
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
  // Serial.println(request);
  client.print(request);
  
  int retryCounter = 0;
  while(!client.available()) {
    // executeCallback();
    retryCounter++;
    if (retryCounter > 20) {
      Serial.println("RetryFail");
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

uint16_t updatePlayback() {
  WiFiClientSecure client;
  client.setInsecure();

  String host = "api.spotify.com";
  const int port = 443;
  String url = "/v1/me/player/currently-playing";
  if (!client.connect(host.c_str(), port)) {
    Serial.println("connection failed");
    return 0;
  }

  String request = "GET " + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Authorization: Bearer " + auth.accessToken + "\r\n" +
               "Connection: close\r\n\r\n";
  client.print(request);
  
  int retryCounter = 0;
  while(!client.available()) {
    retryCounter++;
    if (retryCounter > 20) {
      Serial.println("RetryFail");
      return 0;
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
  // char c;
  // int size = 0;
  // client.setNoDelay(false);
  // while(client.available()) {
  //   while((size = client.available()) > 0) {
  //     c = client.read();
  //     Serial.print(c);
  //   }
  // }

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
    return 0;
  }

  // long long timestamp = doc["timestamp"]; // 1665468979630
  playback.progress = doc["progress_ms"]; // 915

  JsonObject item = doc["item"];
  playback.album_name = (const char*)item["album"]["name"]; // "Late Night Tales: Groove Armada"
  playback.artist_name = (const char*)item["artists"][0]["name"];
  playback.duration = item["duration_ms"];
  playback.track_id = (const char*)item["id"]; // "6pj2yQlRvxYKLMxMTJaUUT"
  playback.track_name = (const char*)item["name"]; // "Roscoe - Beyond the Wizard's Sleeve Remix"
  playback.playing = doc["is_playing"];

  playback.millis = millis();

  return 1;
}

void getLyrics() {
  static String mxmCookie;
  WiFiClientSecure client;
  HTTPClient http;
  lyricP = NULL;
  client.setInsecure(); //Bad!
  playback.track_name.replace(" ", "+");
  playback.artist_name.replace(" ", "+");
  String uri = "https://apic-desktop.musixmatch.com/ws/1.1/macro.subtitles.get?format=json&namespace=lyrics_synched&subtitle_format=lrc&app_id=web-desktop-app-v1.0&q_track="+ playback.track_name +
    "&q_artist=" + playback.artist_name +
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
    lyricP = lyricDoc["message"]["body"]["macro_calls"]["track.subtitles.get"]["message"]["body"]["subtitle_list"][0]["subtitle"]["subtitle_body"];
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

void setup() {
  Serial.begin(115200);
  if(!LittleFS.begin()) {
    Serial.println(F("FATAL: filesystem error"));
    while(1);
  }
  lcd.begin(20,4);
  lcd.clear();
  lcd.print("Connecting to");
  lcd.setCursor(0,1);
  lcd.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long conn_start = millis();
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

  Serial.println(auth.accessToken);
  Serial.println(auth.refreshToken);
}

unsigned int parseInt(const char* ptr) {
  unsigned int ret = 0;
  while(*ptr >= '0' && *ptr <= '9') {
    ret *= 10;
    ret += (*ptr++ - '0');
  }
  return ret;
}

//[MM:SS.TT] Lyric Line\n
bool nextLyric() {
  unsigned int lyricMin = 0;
  unsigned int lyricSec = 0;
  unsigned int lyricMs = 0;
  if(lyricP && *lyricP++ == '[') {
    lyricMin = parseInt(lyricP);
    while(*lyricP++ != ':');
    lyricSec = parseInt(lyricP);
    while(*lyricP++ != '.');
    lyricMs = parseInt(lyricP);
    while(*lyricP++ != ' ');
    
    nextLyricMs = lyricMin*60000 + lyricSec*1000 + lyricMs;
    return true;
  } else {
    Serial.println("Lyric err");
    lyricP = NULL;
    return false; //error
  }
}

void loop() {
  static unsigned long lastUpdate;
  unsigned long now = millis();
  unsigned int progress_ms = (unsigned int)(now - playback.millis) + playback.progress;

  if((playback.playing && progress_ms > playback.duration) || (now - lastUpdate > 5000 && (!playback.playing || nextLyricMs > progress_ms && nextLyricMs-progress_ms > 2000))) {
    unsigned long updateStart = millis();
    if(updatePlayback()) {
      Serial.print("UpdateTime: ");
      Serial.println(millis() - updateStart);
      lastUpdate = now;
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
        yield();
        getLyrics();
        if(!nextLyric()) {
          lcd.setCursor(0,3);
          lcd.print("(No Synced Lyrics)");
        }
      }
      Serial.println(playback.progress);
    } else {
      delay(250); //retry
    }
  } else if(playback.playing && lyricP && *lyricP) {
    if(progress_ms > nextLyricMs) {
      lcd.clear();
      char c;
      int cnt = 0;
      int line = 0;
      while((c = *lyricP++) != '\n') {
        Serial.write(c);
        int wcnt = 1;
        if(c == ' ') {
          const char* tmp = lyricP;
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
        lcd.write(c);
        if(++cnt == 20) {
          lcd.setCursor(0, ++line);
          cnt = 0;
        }

      }
      Serial.write('\n');
      nextLyric();
    }
  }
}