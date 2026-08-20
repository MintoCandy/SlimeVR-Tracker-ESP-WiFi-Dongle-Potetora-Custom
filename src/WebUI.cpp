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
    .container { background-color: #1e1e1e; padding: 40px; border-radius: 12px; display: inline-block; box-shadow: 0 8px 16px rgba(0,0,0,0.8); max-width: 400px; width: 85%; box-sizing: border-box; }
    
    input[type="text"], input[type="password"] { width: 100%; padding: 12px; border: 1px solid #444; border-radius: 6px; background-color: #2a2a2a; color: white; font-size: 16px; box-sizing: border-box; }
    
    /* パスワード入力欄とアイコンを重ねるための枠組み */
    .password-wrapper { position: relative; width: 100%; margin: 10px 0 20px 0; }
    .password-wrapper input { padding-right: 45px; margin: 0; }
    
    /* 目のアイコンの配置とデザイン */
    .toggle-eye { position: absolute; top: 50%; right: 12px; transform: translateY(-50%); cursor: pointer; color: #aaa; display: flex; align-items: center; justify-content: center; padding: 5px; }
    .toggle-eye:hover { color: #fff; }
    .toggle-eye svg { width: 22px; height: 22px; }
    
    input[type="submit"] { width: 100%; padding: 12px; background-color: #00ADB5; color: #121212; font-weight: bold; cursor: pointer; transition: 0.2s ease; border: none; border-radius: 6px; font-size: 16px; margin-top: 10px; }
    input[type="submit"]:hover { background-color: #008C9E; }
  </style>
</head>
<body>
  <div class="container">
    <h2>SlimeVR WiFi Setup</h2>
    <form action="/save" method="POST">
      <div style="text-align: left; margin-bottom: 5px;">SSID:</div>
      <input type="text" name="ssid" value="%CURRENT_SSID%" autocomplete="off" style="margin-bottom: 20px;"><br>
      
      <div style="text-align: left; margin-bottom: 5px;">Password:</div>
      <div class="password-wrapper">
        <input type="password" id="passInput" name="pass" value="%CURRENT_PASS%" autocomplete="off" minlength="8" placeholder="8文字以上 (なしも可)">
        
        <!-- アイコン部分（クリックで切り替え） -->
        <span class="toggle-eye" onclick="togglePassword()">
          <!-- 開いた目 (伏せ字の時に表示) -->
          <svg id="eyeOpen" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2" fill="none" stroke-linecap="round" stroke-linejoin="round" style="display: block;">
            <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"></path>
            <circle cx="12" cy="12" r="3"></circle>
          </svg>
          <!-- 閉じた目 / 斜線入り (文字が見えている時に表示) -->
          <svg id="eyeClosed" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2" fill="none" stroke-linecap="round" stroke-linejoin="round" style="display: none;">
            <path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24"></path>
            <line x1="1" y1="1" x2="23" y2="23"></line>
          </svg>
        </span>
      </div>

      <input type="submit" value="Save & Restart">
    </form>
  </div>

  <script>
    function togglePassword() {
      var passInput = document.getElementById("passInput");
      var eyeOpen = document.getElementById("eyeOpen");
      var eyeClosed = document.getElementById("eyeClosed");
      
      if (passInput.type === "password") {
        passInput.type = "text";            
        eyeOpen.style.display = "none";     
        eyeClosed.style.display = "block";  
      } else {
        passInput.type = "password";        
        eyeOpen.style.display = "block";    
        eyeClosed.style.display = "none";   
      }
    }
  </script>
</body>
</html>
)rawliteral";

void setupWebServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        String html = String(index_html);
        html.replace("%CURRENT_SSID%", currentSsid);
        html.replace("%CURRENT_PASS%", currentPassword);
        
        request->send(200, "text/html", html);
    });

    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
        String newSsid;
        String newPass;
        
        if (request->hasParam("ssid", true)) newSsid = request->getParam("ssid", true)->value();
        if (request->hasParam("pass", true)) newPass = request->getParam("pass", true)->value();

        if (newPass.length() > 0 && newPass.length() < 8) {
            request->send(400, "text/html", "<body style='background-color:#121212; color:#ff4c4c; text-align:center; padding-top:20vh; font-family:sans-serif;'><h2>Error: Password must be at least 8 characters.</h2><p>パスワードは8文字以上で入力してください。</p><br><a href='/' style='color:#00ADB5; text-decoration:none; font-size:18px;'>← 戻る (Go Back)</a></body>");
            return; 
        }
        
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