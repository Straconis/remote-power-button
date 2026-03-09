#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>

#include "lgfx_tdisplay_s3.h"
#include "Indicators.h"
#include "ConfigPortal.h"

LGFX lcd;
WebServer server(80);

ConfigPortal::Settings CFG;

// ---------------- Target machine ----------------
String linuxIpStr = "resolving...";
bool linuxResolved = false;
bool linuxUp = false;

// Re-check target once per hour
static const unsigned long RESOLVE_INTERVAL_MS = 60UL * 60UL * 1000UL;
unsigned long lastResolveMs = 0;

// Flash rate for DNS resolve failure
static const unsigned long FLASH_INTERVAL_MS = 500;
unsigned long lastFlashMs = 0;
bool flashOn = true;

// ---------------- Behavior ----------------
static const uint32_t MAX_HOLD_MS = 8000;
static const uint32_t SHOW_IP_MS  = 15UL * 1000UL;

// Emergency reset via BOOT long-hold
static const uint32_t BOOT_RESET_HOLD_MS = 30UL * 1000UL;

// Force AP mode at startup if BOOT held
static const uint32_t BOOT_FORCE_AP_HOLD_MS = 1200;

// ---------------- State ----------------
String espIpStr = "no wifi";

// Config AP mode
bool apMode = false;
String apSsid = "T-Display-S3-Setup";
String apPass = "12345678"; // WPA2 requires >= 8 chars

// Holds initiated from web endpoint
bool webHoldActive = false;
uint32_t webHoldUntil = 0;

// Case passthrough
bool caseMirrorActive = false;
uint32_t caseMirrorStart = 0;

// KEY passthrough
bool keyHoldActive = false;
uint32_t keyHoldStart = 0;

// Button edge tracking
int bootPrev = HIGH;

// BOOT long-hold tracking
bool bootHoldActive = false;
uint32_t bootHoldStartMs = 0;
bool bootHoldDidReset = false;

// Screen / activity
bool screenOn = true;
uint32_t lastActivityMs = 0;
uint32_t showIpUntilMs = 0;

// ---------------- Indicators (LCD dots) ----------------
Indicators::Config indCfg;
Indicators::StateCache indCache;

// ---------------- Helpers ----------------
static inline void markActivity() { lastActivityMs = millis(); }

static inline uint32_t screenTimeoutMs() {
  if (CFG.screenTimeoutMin == 0) return 0; // disabled
  return (uint32_t)CFG.screenTimeoutMin * 60UL * 1000UL;
}

static inline bool casePressed() { return digitalRead(CFG.pinCaseBtn) == LOW; }
static inline bool keyPressed()  { return digitalRead(CFG.pinKeyBtn)  == LOW; }
static inline bool bootPressed() { return digitalRead(CFG.pinBootBtn) == LOW; }

static inline bool buttonReadState() { return casePressed(); }
static inline bool keyReadState()    { return keyPressed(); }
static inline bool moboOutputState() { return digitalRead(CFG.pinMoboOut) == HIGH; }

// Motherboard button output
static void setMoboBtn(bool on) {
  digitalWrite(CFG.pinMoboOut, on ? HIGH : LOW);
}

// ---------------- Display on/off ----------------
static void displayOn(uint8_t brightness = 200) {
  if (screenOn) return;
  lcd.setBrightness(brightness);
  screenOn = true;
}

static void displayOff() {
  if (!screenOn) return;
  lcd.fillScreen(TFT_BLACK);
  lcd.setBrightness(0);
  screenOn = false;
}

// ---------------- Linux status ----------------
static uint16_t linuxColor() {
  if (!linuxResolved) return flashOn ? TFT_RED : TFT_BLACK;
  if (CFG.targetPort == 0) return TFT_GREEN; // resolve-only
  return linuxUp ? TFT_GREEN : TFT_RED;
}

static const char* linuxText() {
  if (!linuxResolved) return "RESOLVE FAIL";
  return linuxIpStr.c_str();
}

// ---------------- Main screen ----------------
static void drawMainScreen() {
  if (apMode) return;

  displayOn(200);
  lcd.fillScreen(TFT_BLACK);

  // Title
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(2);
  lcd.setCursor(10, 8);
  lcd.println(CFG.deviceTitle);

  int y = 42;

  // ESP IP label
  lcd.setTextSize(1);
  lcd.setCursor(10, y);
  lcd.println("ESP IP:");
  y += 16;

  // ESP IP big
  lcd.setTextSize(3);
  lcd.setCursor(10, y);
  lcd.setTextColor(TFT_WHITE);
  lcd.println(espIpStr);
  y += 3 * 8 + 20;

  // Target label
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_WHITE);
  lcd.setCursor(10, y);
  lcd.println("target:");
  y += 16;

  // Target big (colored)
  lcd.setTextSize(3);
  lcd.setTextColor(linuxColor());
  lcd.setCursor(10, y);
  lcd.println(linuxText());

  lcd.setTextColor(TFT_WHITE);

  // Indicator dots (right side)
  Indicators::redrawAll(lcd, buttonReadState(), keyReadState(), moboOutputState(), indCache, indCfg);
}

// BOOT button “show IP” view
static void showIpScreenNow() {
  uint32_t now = millis();
  showIpUntilMs = now + SHOW_IP_MS;
  markActivity();
  drawMainScreen();
}

// ---------------- “Ping” without ICMP: TCP probe ----------------
static bool tcpProbe(IPAddress ip, uint16_t port) {
  if (port == 0) return true; // resolve-only
  WiFiClient client;
  client.setTimeout(800);
  bool ok = client.connect(ip, port);
  client.stop();
  return ok;
}

// Resolve + update linuxResolved/linuxUp/linuxIpStr
static void resolveAndCheck(bool redraw) {
  if (apMode) return;

  IPAddress ip;
  linuxResolved = false;
  linuxUp = false;

  if (WiFi.status() == WL_CONNECTED) {
    bool isIp = ip.fromString(CFG.targetFqdn);
    if (isIp) {
      linuxResolved = true;
      linuxIpStr = ip.toString();
    } else if (WiFi.hostByName(CFG.targetFqdn.c_str(), ip) && ip != INADDR_NONE) {
      linuxResolved = true;
      linuxIpStr = ip.toString();
    }
  }

  if (linuxResolved) {
    linuxUp = tcpProbe(ip, CFG.targetPort);
  } else {
    linuxIpStr = "not found";
  }

  if (redraw) drawMainScreen();
}

// ---------------- Config AP splash ----------------
static void startConfigAP() {
  apMode = true;

  WiFi.mode(WIFI_AP_STA);
  delay(50);
  WiFi.softAP(apSsid.c_str(), apPass.c_str());
  delay(50);

  espIpStr = WiFi.softAPIP().toString();

  // Screen (3/2/2)
  displayOn(180);
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE);

  lcd.setTextSize(3);
  lcd.setCursor(10, 8);
  lcd.println("CONFIG");

  lcd.setTextSize(2); // 2
  lcd.setCursor(10, 52);
  lcd.println("SSID:");
  lcd.setCursor(10, 72);
  lcd.println(apSsid);

  lcd.setCursor(10, 104);
  lcd.println("PASS:");
  lcd.setCursor(10, 124);
  lcd.println(apPass);

  lcd.setTextSize(2); // 2
  lcd.setCursor(10, 156);
  lcd.println("Open:");
  lcd.setCursor(10, 176);
  lcd.println("192.168.4.1/config");

  linuxResolved = true;
  linuxUp = false;
  linuxIpStr = "AP MODE";

  lastActivityMs = millis();
  showIpUntilMs = 0;
}

// ---------------- Button passthrough logic ----------------
static void startWebHold(uint32_t ms) {
  ms = constrain(ms, 50UL, MAX_HOLD_MS);
  webHoldActive = true;
  webHoldUntil = millis() + ms;
  setMoboBtn(true);
  markActivity();
}

static void updateWebHold() {
  if (!webHoldActive) return;

  if ((int32_t)(millis() - webHoldUntil) >= 0) {
    webHoldActive = false;
    setMoboBtn(false);
  } else {
    setMoboBtn(true);
  }
}

static void updateCaseButtonMirror() {
  if (webHoldActive || keyHoldActive) return;

  uint32_t now = millis();
  bool pressed = casePressed();

  if (pressed && !caseMirrorActive) {
    caseMirrorActive = true;
    caseMirrorStart = now;
    setMoboBtn(true);
    markActivity();
  } else if (!pressed && caseMirrorActive) {
    caseMirrorActive = false;
    setMoboBtn(false);
    markActivity();
  }

  if (caseMirrorActive && (now - caseMirrorStart) > MAX_HOLD_MS) {
    caseMirrorActive = false;
    setMoboBtn(false);
  }
}

static void updateKeyButtonMirror() {
  uint32_t now = millis();
  bool pressed = keyPressed();

  if (pressed && !keyHoldActive && !webHoldActive) {
    keyHoldActive = true;
    keyHoldStart = now;
    setMoboBtn(true);
    markActivity();
  }

  if (keyHoldActive) {
    if (!pressed || webHoldActive || (now - keyHoldStart) > MAX_HOLD_MS) {
      keyHoldActive = false;
      if (!webHoldActive) setMoboBtn(false);
      markActivity();
    }
  }
}

// Emergency reset if BOOT held for 30 seconds (works even if locked out)
static void doEmergencyResetNow() {
  // Show something on LCD even if in AP mode / screen off
  displayOn(200);
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(2);
  lcd.setCursor(10, 20);
  lcd.println("FACTORY RESET");
  lcd.setCursor(10, 44);
  lcd.println("Clearing config...");
  delay(250);

  ConfigPortal::factoryReset();
  ConfigPortal::clearWifiCredentials();

  lcd.setCursor(10, 72);
  lcd.println("Restarting...");
  delay(800);
  ESP.restart();
}

static void updateBootButton() {
  bool pressed = bootPressed();
  uint32_t now = millis();

  // Long-hold tracking for emergency reset
  if (pressed && !bootHoldActive) {
    bootHoldActive = true;
    bootHoldStartMs = now;
    bootHoldDidReset = false;
  } else if (!pressed && bootHoldActive) {
    bootHoldActive = false;
  }

  if (bootHoldActive && !bootHoldDidReset) {
    if ((now - bootHoldStartMs) >= BOOT_RESET_HOLD_MS) {
      bootHoldDidReset = true;
      doEmergencyResetNow();
      return; // won't return, but safe
    }
  }

  // Normal press edge -> show IP
  int nowVal = pressed ? LOW : HIGH;
  if (bootPrev == HIGH && nowVal == LOW) {
    if (!apMode) showIpScreenNow();
  }
  bootPrev = nowVal;
}

// ---------------- Screen timeout ----------------
static void updateScreenTimeout() {
  if (apMode) return;

  uint32_t now = millis();

  if (showIpUntilMs != 0 && (int32_t)(showIpUntilMs - now) > 0) {
    displayOn();
    return;
  }

  uint32_t toMs = screenTimeoutMs();
  if (toMs == 0) return;

  if (screenOn && (now - lastActivityMs) > toMs) displayOff();
}

static void wakeIfNeeded() {
  if (!screenOn && !apMode) showIpScreenNow();
}

// ---------------- Web auth + token ----------------
static bool frontAuthOk() {
  if (CFG.frontUser.length() == 0) return true;
  if (server.authenticate(CFG.frontUser.c_str(), CFG.frontPass.c_str())) return true;
  server.requestAuthentication();
  return false;
}

static bool tokenOk() {
  return server.hasArg("token") && server.arg("token") == CFG.token;
}

// ---------------- Live status endpoint ----------------
static void handleStatus() {
  if (!frontAuthOk()) return;

  bool btn = buttonReadState();
  bool key = keyReadState();
  bool mbo = moboOutputState();

  String mode = apMode ? "AP" : "STA";

  String target = linuxResolved ? linuxIpStr : String("RESOLVE FAIL");
  bool targetGreen = false;
  if (linuxResolved) {
    targetGreen = (CFG.targetPort == 0) ? true : linuxUp;
  }

  String json;
  json.reserve(260);
  json += "{";
  json += "\"mode\":\"" + mode + "\",";
  json += "\"espIp\":\"" + espIpStr + "\",";
  json += "\"targetText\":\"" + target + "\",";
  json += "\"targetOk\":" + String(targetGreen ? "true" : "false") + ",";
  json += "\"btn\":" + String(btn ? "true" : "false") + ",";
  json += "\"key\":" + String(key ? "true" : "false") + ",";
  json += "\"mbo\":" + String(mbo ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}

void handleCheck() {
  if (!frontAuthOk()) return;
  wakeIfNeeded();
  resolveAndCheck(false);
  handleStatus();
}

// ---------------- Web UI bits ----------------

static String webHeadHtml(const String& title) {
  String h;
  h.reserve(2600);
  h += "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  h += "<title>" + title + "</title>";
  h += "<style>"
       ":root{--bg:#ffffff;--fg:#111111;--muted:#555555;--card:#f7f7f7;--border:#d0d0d0;--accent:#2563eb;--ok:#19c37d;--bad:#e53e3e;}"
       "body.dark{--bg:#0f172a;--fg:#e5e7eb;--muted:#94a3b8;--card:#111827;--border:#334155;--accent:#60a5fa;--ok:#22c55e;--bad:#f87171;}"
       "body{font-family:Arial,sans-serif;background:var(--bg);color:var(--fg);margin:18px;line-height:1.45;}"
       "a{color:var(--accent)}"
       ".topbar{display:flex;gap:10px;align-items:center;justify-content:space-between;flex-wrap:wrap;margin-bottom:12px}"
       ".card{padding:10px;border:1px solid var(--border);background:var(--card);border-radius:10px;max-width:560px}"
       ".btn{display:inline-block;padding:8px 12px;border-radius:8px;border:1px solid var(--border);background:var(--card);color:var(--fg);text-decoration:none;cursor:pointer}"
       ".btn.primary{border-color:var(--accent)}"
       ".muted{color:var(--muted)}"
       ".dot{display:inline-block;width:14px;height:14px;border-radius:50%;margin-right:8px;border:1px solid var(--border);background:var(--bad);vertical-align:middle}"
       "</style>";
  h += "<script>"
       "function applyTheme(mode){"
       "  const dark=(mode==='dark');"
       "  document.body.classList.toggle('dark',dark);"
       "  const b=document.getElementById('themeBtn');"
       "  if(b) b.textContent=dark?'Light mode':'Dark mode';"
       "}"
       "function initTheme(){"
       "  const stored=localStorage.getItem('rpm_theme')||'light';"
       "  applyTheme(stored);"
       "}"
       "function toggleTheme(){"
       "  const next=document.body.classList.contains('dark')?'light':'dark';"
       "  localStorage.setItem('rpm_theme',next);"
       "  applyTheme(next);"
       "}"
       "</script>";
  h += "</head><body onload='initTheme()'>";
  h += "<div class='topbar'><div><h2 style='margin:0'>" + title + "</h2></div>"
       "<div><button id='themeBtn' class='btn' type='button' onclick='toggleTheme()'>Dark mode</button></div></div>";
  return h;
}

static String liveBkmCardHtml() {
  String h;
  h.reserve(2200);

  h += "<div class='card'>";
  h += "<h3 style='margin:0 0 8px 0'>BKM Indicators (Live)</h3>";

  h += "<div style='margin:6px 0'><span id='dotBtn' class='dot'></span>"
       "<b>Button Read</b>: <span id='txtBtn'>OFF</span></div>";

  h += "<div style='margin:6px 0'><span id='dotKey' class='dot'></span>"
       "<b>Key Press</b>: <span id='txtKey'>OFF</span></div>";

  h += "<div style='margin:6px 0'><span id='dotMbo' class='dot'></span>"
       "<b>MOBO Output</b>: <span id='txtMbo'>OFF</span></div>";

  h += "<hr style='border:none;border-top:1px solid var(--border);margin:10px 0'>";

  h += "<div style='margin:6px 0'><b>Mode:</b> <span id='txtMode'>...</span></div>";
  h += "<div style='margin:6px 0'><b>ESP IP:</b> <span id='txtEspIp'>...</span></div>";
  h += "<div style='margin:6px 0'><b>Target:</b> <span id='txtTarget'>...</span> "
       "<span id='txtTargetState' style='font-weight:bold'>(...)</span></div>";

  h += "<div style='margin-top:12px;display:flex;gap:10px;align-items:center;flex-wrap:wrap'>"
       "<button class='btn primary' type='button' onclick='runCheck()'>Live Check Now</button>"
       "<span id='checkStatus' class='muted'>Idle</span>"
       "</div>";

  h += "<small class='muted'>Status updates automatically. Use Live Check Now to force a fresh target resolve/probe.</small>";
  h += "</div>";

  return h;
}

static String liveUpdateScript(uint16_t intervalMs = 350) {
  String s;
  s.reserve(2600);

  s += "<script>";
  s += "function setDot(dotId, txtId, on){"
       "  const d=document.getElementById(dotId);"
       "  const t=document.getElementById(txtId);"
       "  if(d) d.style.background= on ? 'var(--ok)' : 'var(--bad)';"
       "  if(t) t.textContent= on ? 'ON' : 'OFF';"
       "}"
       "function applyStatus(j){"
       "  setDot('dotBtn','txtBtn',j.btn);"
       "  setDot('dotKey','txtKey',j.key);"
       "  setDot('dotMbo','txtMbo',j.mbo);"
       "  const m=document.getElementById('txtMode'); if(m) m.textContent=j.mode;"
       "  const e=document.getElementById('txtEspIp'); if(e) e.textContent=j.espIp;"
       "  const tt=document.getElementById('txtTarget'); if(tt) tt.textContent=j.targetText;"
       "  const ts=document.getElementById('txtTargetState');"
       "  if(ts){ ts.textContent=j.targetOk ? '(OK)' : '(DOWN)'; ts.style.color=j.targetOk ? 'var(--ok)' : 'var(--bad)'; }"
       "}"
       "async function poll(){"
       "  try{"
       "    const r=await fetch('/status',{cache:'no-store'});"
       "    if(!r.ok) return;"
       "    const j=await r.json();"
       "    applyStatus(j);"
       "  }catch(e){}"
       "}"
       "async function runCheck(){"
       "  const st=document.getElementById('checkStatus');"
       "  if(st) st.textContent='Checking...';"
       "  try{"
       "    const r=await fetch('/check',{cache:'no-store'});"
       "    if(!r.ok) throw new Error('HTTP '+r.status);"
       "    const j=await r.json();"
       "    applyStatus(j);"
       "    if(st) st.textContent='Updated';"
       "  }catch(e){"
       "    if(st) st.textContent='Check failed';"
       "  }"
       "}"
       "poll();"
       "setInterval(poll," + String(intervalMs) + ");";
  s += "</script>";

  return s;
}

static String bootHelpHtml() {
  String h;
  h.reserve(500);
  h += "<div style='margin:12px 0;padding:10px;border:1px dashed #bbb;border-radius:10px;max-width:560px'>";
  h += "<b>BOOT button:</b><br>";
  h += "<span>&#8226;</span> Tap: show IP screen on timeout (LCD)<br>";
  h += "<span>&#8226;</span> Hold 30s: <b>factory reset</b> (if you lose backend password)<br>";
  h += "<span>&#8226;</span> Hold ~1s during power-up: start config hotspot (no wipe)<br>";
  h += "</div>";
  return h;
}

static String okPageHtml(const String& msg) {
  String h;
  h.reserve(5200);

  h += webHeadHtml(CFG.deviceTitle + " PC Power");
  h += "<p><b>" + msg + "</b></p>";
  h += liveBkmCardHtml();
  h += "<p style='margin-top:14px'><a href='/' class='btn'>Back</a></p>";
  h += liveUpdateScript();
  h += "</body></html>";

  return h;
}

// ---------------- Web handlers ----------------
void handleRoot() {
  if (!frontAuthOk()) return;
  wakeIfNeeded();

  String html;
  html.reserve(7800);

  html += webHeadHtml(CFG.deviceTitle + " PC Power");
  html += "<p><a class='btn' href='/tap?token=" + CFG.token + "'>Tap Power (250ms)</a></p>";
  html += "<p><a class='btn' href='/hold?token=" + CFG.token + "&ms=5000'>Hold 5s</a></p>";
  html += bootHelpHtml();
  html += "<hr style='border:none;border-top:1px solid var(--border);margin:16px 0'>";
  html += liveBkmCardHtml();
  html += "<hr style='border:none;border-top:1px solid var(--border);margin:16px 0'><p><a class='btn' href='/config'>Config</a></p>";
  html += liveUpdateScript();
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleTap() {
  if (!frontAuthOk()) return;
  if (!tokenOk()) { server.send(403, "text/plain", "Forbidden"); return; }
  wakeIfNeeded();
  startWebHold(250);
  server.send(200, "text/html", okPageHtml("OK"));
}

void handleHold() {
  if (!frontAuthOk()) return;
  if (!tokenOk()) { server.send(403, "text/plain", "Forbidden"); return; }
  wakeIfNeeded();
  uint32_t ms = server.hasArg("ms") ? (uint32_t)server.arg("ms").toInt() : 5000;
  startWebHold(ms);
  server.send(200, "text/html", okPageHtml("OK"));
}

// ---------------- Setup/Loop ----------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("BOOT");

  ConfigPortal::load(CFG);

  // Power/backlight enables
  pinMode(CFG.pinPowerOn, OUTPUT);
  digitalWrite(CFG.pinPowerOn, HIGH);

  pinMode(CFG.pinBl, OUTPUT);
  digitalWrite(CFG.pinBl, HIGH);

  // Motherboard output
  pinMode(CFG.pinMoboOut, OUTPUT);
  setMoboBtn(false);

  // Buttons
  pinMode(CFG.pinCaseBtn, INPUT_PULLUP);
  pinMode(CFG.pinBootBtn, INPUT_PULLUP);
  pinMode(CFG.pinKeyBtn,  INPUT_PULLUP);
  bootPrev = HIGH;

  // Display
  lcd.init();
  lcd.setRotation(1);
  lcd.setBrightness(200);
  screenOn = true;

  // Indicator dot placement (right side)
  int16_t w = lcd.width();
  indCfg.r = 10;
  indCfg.btnX  = w - 18; indCfg.btnY  = 18;
  indCfg.keyX  = w - 18; indCfg.keyY  = 46;
  indCfg.moboX = w - 18; indCfg.moboY = 74;

  lastActivityMs = millis();
  showIpUntilMs = 0;

  WiFi.persistent(false);

  // If BOOT held during startup, force AP mode (no wipe)
  bool forceAP = false;
  {
    uint32_t t0 = millis();
    while ((millis() - t0) < BOOT_FORCE_AP_HOLD_MS) {
      if (!bootPressed()) { forceAP = false; break; }
      forceAP = true;
      delay(10);
      yield();
    }
  }

  bool canTrySta = (!forceAP) && CFG.configured && (CFG.wifiSsid.length() > 0);

  if (canTrySta) {
    WiFi.mode(WIFI_STA);
    delay(50);
    WiFi.begin(CFG.wifiSsid.c_str(), CFG.wifiPass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
      delay(250);
      yield();
    }

    if (WiFi.status() == WL_CONNECTED) {
      apMode = false;
      espIpStr = WiFi.localIP().toString();
      resolveAndCheck(false);
      drawMainScreen();
      lastResolveMs = millis();
    } else {
      startConfigAP();
      lastResolveMs = 0;
    }
  } else {
    startConfigAP();
    lastResolveMs = 0;
  }

  // Web routes
  server.on("/", handleRoot);
  server.on("/tap", handleTap);
  server.on("/hold", handleHold);

  // Live status + manual check endpoints
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/check", HTTP_GET, handleCheck);

  // Config portal routes
  ConfigPortal::registerRoutes(server, CFG);

  server.begin();
}

void loop() {
  server.handleClient();

  // Holds + buttons + screen
  updateWebHold();
  updateBootButton();
  updateKeyButtonMirror();
  updateCaseButtonMirror();
  updateScreenTimeout();

  // Update LCD indicator dots live
  if (!apMode && screenOn) {
    Indicators::update(lcd, buttonReadState(), keyReadState(), moboOutputState(), indCache, indCfg);
  }

  // Flash if resolve failed
  if (!apMode && !linuxResolved) {
    uint32_t now = millis();
    if ((now - lastFlashMs) >= FLASH_INTERVAL_MS) {
      lastFlashMs = now;
      flashOn = !flashOn;
      if (screenOn) drawMainScreen();
    }
  }

  // Hourly re-resolve/re-check
  if (!apMode && WiFi.status() == WL_CONNECTED && lastResolveMs != 0) {
    uint32_t now = millis();
    if ((now - lastResolveMs) >= RESOLVE_INTERVAL_MS) {
      lastResolveMs = now;
      resolveAndCheck(true);
    }
  }

  yield();
}
