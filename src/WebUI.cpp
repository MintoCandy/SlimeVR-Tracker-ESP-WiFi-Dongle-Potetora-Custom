#include "WebUI.h"
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

// main.cppで定義されている変数を、このファイルでも使えるように引っ張ってくる
extern String currentSsid;
extern String currentPassword;

AsyncWebServer server(80);

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>SlimeVR Dongle Setup</title>
  <style>
    body { background-color: #121212; color: #e0e0e0; font-family: 'Segoe UI', sans-serif; text-align: center; padding-top: 5vh; }
    .container { background-color: #1e1e1e; padding: 40px; border-radius: 12px; display: inline-block; box-shadow: 0 8px 16px rgba(0,0,0,0.8); max-width: 400px; width: 80%; }
    input { width: 90%; padding: 12px; margin: 10px 0 20px 0; border: 1px solid #444; border-radius: 6px; background-color: #2a2a2a; color: white; font-size: 16px; box-sizing: border-box; }
    input[type="submit"] { background-color: #00ADB5; color: #121212; font-weight: bold; cursor: pointer; transition: 0.2s ease; border: none; }
    input[type="submit"]:hover { background-color: #008C9E; }
  </style>
</head>
<body>
  <div class="container">
    <h2>SlimeVR WiFi Setup</h2>
    <form action="/save" method="POST">
      SSID:<br>
      <input type="text" name="ssid" value="%CURRENT_SSID%" autocomplete="off"><br>
      Password:<br>
      <input type="password" name="pass" value="%CURRENT_PASS%" autocomplete="off"><br>
      <input type="submit" value="Save & Restart">
    </form>
  </div>
</body>
</html>
)rawliteral";

String processor(const String& var){
  if(var == "CURRENT_SSID") return currentSsid;
  if(var == "CURRENT_PASS") return currentPassword;
  return String();
}

void setupWebServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", index_html, processor);
    });

    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
        String newSsid;
        String newPass;
        
        if (request->hasParam("ssid", true)) newSsid = request->getParam("ssid", true)->value();
        if (request->hasParam("pass", true)) newPass = request->getParam("pass", true)->value();
        
        Preferences prefs;
        prefs.begin("wifi_config", false);
        prefs.putString("ssid", newSsid);
        prefs.putString("password", newPass);
        prefs.end();

        request->send(200, "text/html", "<body style='background-color:#121212; color:#00ADB5; text-align:center; padding-top:20vh; font-family:sans-serif;'><h2>Saved! Rebooting Dongle...</h2><p>Please wait 5 seconds and reconnect your trackers.</p></body>");
        
        delay(1000);
        ESP.restart();
    });

    server.begin();
    Serial.println("[Web] Server started on port 80");
}