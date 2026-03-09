#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>

namespace ConfigPortal {

static const char* NVS_NS = "s3d_cfg";

// Hardened persistence markers
static const uint32_t CFG_MAGIC = 0x53334431; // "S3D1"
static const uint16_t CFG_VER   = 1;

struct Settings {
  bool configured = false;

  // Device UI
  String deviceTitle = "T-Display-S3";
  uint32_t screenTimeoutMin = 15; // 0 = disabled

  // WiFi
  String wifiSsid = "";
  String wifiPass = "";

  // Token for /tap /hold
  String token = "1EH5GsOF6mZQZye";

  // Target
  String targetFqdn = "itx-nix.gha.chartermi.net";
  uint16_t targetPort = 22; // 0 = resolve-only

  // Pins
  int pinPowerOn = 15;
  int pinBl      = 38;
  int pinMoboOut = 21;
  int pinCaseBtn = 16;
  int pinBootBtn = 0;
  int pinKeyBtn  = 14;

  // Auth (blank username disables)
  String cfgUser = "";
  String cfgPass = "";
  String frontUser = "";
  String frontPass = "";
};

inline String htmlEscape(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&': o += F("&amp;"); break;
      case '<': o += F("&lt;"); break;
      case '>': o += F("&gt;"); break;
      case '"': o += F("&quot;"); break;
      case '\'': o += F("&#39;"); break;
      default: o += c; break;
    }
  }
  return o;
}

inline String jsonEscape(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '\\': o += "\\\\"; break;
      case '"':  o += "\\\""; break;
      case '\n': o += "\\n";  break;
      case '\r': o += "\\r";  break;
      case '\t': o += "\\t";  break;
      default:   o += c;      break;
    }
  }
  return o;
}

inline bool hasValidConfig() {
  Preferences p;
  if (!p.begin(NVS_NS, true)) return false;
  uint32_t magic = p.getUInt("magic", 0);
  uint16_t ver   = (uint16_t)p.getUShort("ver", 0);
  p.end();
  return (magic == CFG_MAGIC) && (ver == CFG_VER);
}

inline void load(Settings& cfg) {
  bool valid = hasValidConfig();

  Preferences p;
  p.begin(NVS_NS, true);

  cfg.configured       = p.getBool("cfg", false);
  cfg.deviceTitle      = p.getString("title", cfg.deviceTitle);
  cfg.screenTimeoutMin = (uint32_t)p.getUInt("stmin", cfg.screenTimeoutMin);

  cfg.wifiSsid         = p.getString("ssid",  cfg.wifiSsid);
  cfg.wifiPass         = p.getString("wpass", cfg.wifiPass);

  cfg.token            = p.getString("token", cfg.token);

  cfg.targetFqdn       = p.getString("fqdn",  cfg.targetFqdn);
  cfg.targetPort       = (uint16_t)p.getUInt("tport", cfg.targetPort);

  cfg.pinPowerOn       = p.getInt("pwr",  cfg.pinPowerOn);
  cfg.pinBl            = p.getInt("bl",   cfg.pinBl);
  cfg.pinMoboOut       = p.getInt("mbo",  cfg.pinMoboOut);
  cfg.pinCaseBtn       = p.getInt("case", cfg.pinCaseBtn);
  cfg.pinBootBtn       = p.getInt("boot", cfg.pinBootBtn);
  cfg.pinKeyBtn        = p.getInt("key",  cfg.pinKeyBtn);

  cfg.cfgUser          = p.getString("cuser", cfg.cfgUser);
  cfg.cfgPass          = p.getString("cpass", cfg.cfgPass);
  cfg.frontUser        = p.getString("fuser", cfg.frontUser);
  cfg.frontPass        = p.getString("fpass", cfg.frontPass);

  p.end();

  // Basic sanity
  if (cfg.deviceTitle.length() == 0) cfg.deviceTitle = "T-Display-S3";
  if (cfg.screenTimeoutMin > 1440) cfg.screenTimeoutMin = 1440;

  // If config isn't valid, force unconfigured state
  if (!valid) {
    cfg.configured = false;
    cfg.wifiSsid = "";
    cfg.wifiPass = "";
  }

  // If marked not configured, make sure WiFi creds are blank
  if (!cfg.configured) {
    cfg.wifiSsid = "";
    cfg.wifiPass = "";
  }
}

inline bool save(Settings& cfg) {
  cfg.configured = (cfg.wifiSsid.length() > 0);

  if (cfg.deviceTitle.length() == 0) cfg.deviceTitle = "T-Display-S3";
  if (cfg.screenTimeoutMin > 1440) cfg.screenTimeoutMin = 1440;

  Preferences p;
  if (!p.begin(NVS_NS, false)) return false;

  p.putUInt("magic", CFG_MAGIC);
  p.putUShort("ver", CFG_VER);

  p.putBool("cfg", cfg.configured);
  p.putString("title", cfg.deviceTitle);
  p.putUInt("stmin", (unsigned int)cfg.screenTimeoutMin);

  p.putString("ssid",  cfg.wifiSsid);
  p.putString("wpass", cfg.wifiPass);

  p.putString("token", cfg.token);

  p.putString("fqdn",  cfg.targetFqdn);
  p.putUInt("tport",   (unsigned int)cfg.targetPort);

  p.putInt("pwr",  cfg.pinPowerOn);
  p.putInt("bl",   cfg.pinBl);
  p.putInt("mbo",  cfg.pinMoboOut);
  p.putInt("case", cfg.pinCaseBtn);
  p.putInt("boot", cfg.pinBootBtn);
  p.putInt("key",  cfg.pinKeyBtn);

  p.putString("cuser", cfg.cfgUser);
  p.putString("cpass", cfg.cfgPass);
  p.putString("fuser", cfg.frontUser);
  p.putString("fpass", cfg.frontPass);

  p.end();
  delay(100);
  return true;
}

inline void factoryReset() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.clear();
  p.end();
  delay(100);
}

inline void clearWifiCredentials() {
  // Best-effort for ESP32 Arduino: wipe stored WiFi + disconnect
  WiFi.mode(WIFI_MODE_NULL);
  delay(100);
  esp_wifi_restore();
  delay(200);
  WiFi.disconnect(true, true);
  delay(150);
}

inline bool requireAuth(WebServer& server, const String& user, const String& pass) {
  if (user.length() == 0) return true; // disabled
  if (server.authenticate(user.c_str(), pass.c_str())) return true;
  server.requestAuthentication();
  return false;
}

inline String scanJson() {
  // Clean up any old/incomplete scan first
  WiFi.scanDelete();
  delay(50);

  // Keep AP alive while ensuring STA is enabled for scanning
  WiFi.mode(WIFI_AP_STA);
  delay(150);

  int n = WiFi.scanNetworks(false, true); // async=false, show_hidden=true

  if (n < 0) {
    WiFi.scanDelete();
    return F("{\"ok\":false,\"error\":\"scan_failed\",\"count\":0,\"items\":[]}");
  }

  if (n == 0) {
    WiFi.scanDelete();
    return F("{\"ok\":true,\"count\":0,\"items\":[]}");
  }

  const int MAX_LIST = 15;
  int count = (n > MAX_LIST) ? MAX_LIST : n;

  int idx[MAX_LIST];
  for (int i = 0; i < count; i++) idx[i] = i;

  // sort by RSSI desc
  for (int i = 0; i < count - 1; i++) {
    int best = i;
    int bestRssi = WiFi.RSSI(idx[i]);
    for (int j = i + 1; j < count; j++) {
      int r = WiFi.RSSI(idx[j]);
      if (r > bestRssi) { best = j; bestRssi = r; }
    }
    if (best != i) { int t = idx[i]; idx[i] = idx[best]; idx[best] = t; }
  }

  String out;
  out.reserve(1800);
  out += F("{\"ok\":true,\"count\":");
  out += String(count);
  out += F(",\"items\":[");

  for (int k = 0; k < count; k++) {
    int i = idx[k];
    String ssid = WiFi.SSID(i);
    int32_t rssi = WiFi.RSSI(i);
    bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);

    if (k) out += ',';
    out += F("{\"ssid\":\"");
    out += jsonEscape(ssid);
    out += F("\",\"rssi\":");
    out += String(rssi);
    out += F(",\"open\":");
    out += (open ? F("true") : F("false"));
    out += '}';
  }
  out += F("]}");

  WiFi.scanDelete();
  return out;
}

inline String configPageHtml(const Settings& cfg) {
  String h;
  h.reserve(12000);

  h += F("<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>");
h += F("<style>"
       ":root{--bg:#ffffff;--fg:#111111;--muted:#444;--card:#f7f7f7;--border:#d0d0d0;--accent:#2563eb;}"
       "body.dark{--bg:#0f172a;--fg:#e5e7eb;--muted:#94a3b8;--card:#111827;--border:#334155;--accent:#60a5fa;}"
       "body{font-family:Arial;max-width:920px;margin:18px;background:var(--bg);color:var(--fg)}"
       "a{color:var(--accent)}"
       "input,select{width:320px;max-width:100%;background:var(--card);color:var(--fg);border:1px solid var(--border);padding:6px;border-radius:6px}"
       ".row{margin:8px 0}"
       "small{color:var(--muted)}"
       "button{background:var(--card);color:var(--fg);border:1px solid var(--border);border-radius:8px;padding:8px 12px}"
       "button.small{margin-left:6px;padding:4px 8px}"
       ".topbar{display:flex;gap:10px;align-items:center;justify-content:space-between;flex-wrap:wrap}"
       "</style>"
       "<script>"
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
       "</script></head><body onload='initTheme()'>");


  h += F("<div class='topbar'><div><h2 style='margin:0'>Configuration</h2></div><div><button id='themeBtn' type='button' onclick='toggleTheme()'>Dark mode</button></div></div>");
  h += F("<p><a href='/'>Back to status</a></p>");

  h += F("<form method='POST' action='/save'>");

  h += F("<h3>Device</h3>");
  h += F("<div class='row'>Title: <input name='title' value='");
  h += htmlEscape(cfg.deviceTitle);
  h += F("'></div>");
  h += F("<div class='row'>Screen timeout (minutes, 0=disabled): <input name='stmin' type='number' min='0' max='1440' value='");
  h += String(cfg.screenTimeoutMin);
  h += F("'></div>");

  h += F("<h3>WiFi</h3>");
  h += F("<div class='row'><button type='button' onclick='scanWifi()'>Scan Networks</button> ");
  h += F("<span id='scanStatus'></span></div>");
  h += F("<div class='row'>Pick SSID: <select id='ssidPick' onchange='pickSsid()'>");
  h += F("<option value=''>-- scan first --</option></select></div>");

  h += F("<div class='row'>SSID: <input id='ssid' name='ssid' value='"); h += htmlEscape(cfg.wifiSsid); h += F("'></div>");

  h += F("<div class='row'>Password: "
         "<input id='wpass' type='password' name='wpass' value='");
  h += htmlEscape(cfg.wifiPass);
  h += F("'>"
         "<button class='small' type='button' onclick=\"togglePw('wpass', this)\">Show</button>"
         "</div>");

  h += F("<h3>Target</h3>");
  h += F("<div class='row'>Host or IP: <input name='fqdn' value='"); h += htmlEscape(cfg.targetFqdn); h += F("'></div>");
  h += F("<div class='row'>Probe port (0=disabled): <input name='tport' type='number' min='0' max='65535' value='");
  h += String(cfg.targetPort);
  h += F("'></div>");
  h += F("<small>"
         "Helper: 22=SSH (Linux), 3389=RDP (Windows), 445=SMB (Windows file sharing). "
         "If nothing is listening, set port to 0 (green = resolved only)."
         "</small>");

  h += F("<h3>Security Token</h3>");
  h += F("<div class='row'>Token: <input name='token' value='"); h += htmlEscape(cfg.token); h += F("'></div>");

  h += F("<h3>Authentication</h3>");

  h += F("<div class='row'><b>Config (backend)</b> protects /config, /scan, /save, /reset</div>");
  h += F("<div class='row'>Username: <input name='cuser' value='"); h += htmlEscape(cfg.cfgUser); h += F("'></div>");
  h += F("<div class='row'>Password: "
         "<input id='cpass' type='password' name='cpass' value='");
  h += htmlEscape(cfg.cfgPass);
  h += F("'>"
         "<button class='small' type='button' onclick=\"togglePw('cpass', this)\">Show</button>"
         "</div>");
  h += F("<small>Leave username blank to disable backend auth.</small><br><br>");

  h += F("<div class='row'><b>Frontend</b> protects /, /tap, /hold</div>");
  h += F("<div class='row'>Username: <input name='fuser' value='"); h += htmlEscape(cfg.frontUser); h += F("'></div>");
  h += F("<div class='row'>Password: "
         "<input id='fpass' type='password' name='fpass' value='");
  h += htmlEscape(cfg.frontPass);
  h += F("'>"
         "<button class='small' type='button' onclick=\"togglePw('fpass', this)\">Show</button>"
         "</div>");
  h += F("<small>Leave username blank to disable frontend auth.</small>");

  h += F("<h3>Pins</h3>");
  h += F("<div class='row'>PIN_POWER_ON: <input name='pwr' type='number' value='"); h += cfg.pinPowerOn; h += F("'></div>");
  h += F("<div class='row'>PIN_BL: <input name='bl' type='number' value='"); h += cfg.pinBl; h += F("'></div>");
  h += F("<div class='row'>MB_PWR_OUT_PIN: <input name='mbo' type='number' value='"); h += cfg.pinMoboOut; h += F("'></div>");
  h += F("<div class='row'>CASE_BTN_IN_PIN: <input name='case' type='number' value='"); h += cfg.pinCaseBtn; h += F("'></div>");
  h += F("<div class='row'>BTN_BOOT_PIN: <input name='boot' type='number' value='"); h += cfg.pinBootBtn; h += F("'></div>");
  h += F("<div class='row'>BTN_KEY_PIN: <input name='key' type='number' value='"); h += cfg.pinKeyBtn; h += F("'></div>");

  h += F("<p><button type='submit'>Save & Restart</button></p>");
  h += F("</form>");

  h += F("<hr><h3>Factory Reset</h3>");
  h += F("<p><a href='/reset' onclick=\"return confirm('Factory reset config + WiFi creds?')\">Reset & restart</a></p>");
  h += F("<small>Recovery: hold BOOT for 30 seconds to factory reset if you lose backend auth.</small>");

  // JS: scan + select + show/hide password
  h += F("<script>"
         "function togglePw(id, btn){"
         "  const el=document.getElementById(id);"
         "  if(!el) return;"
         "  if(el.type==='password'){ el.type='text'; btn.textContent='Hide'; }"
         "  else { el.type='password'; btn.textContent='Show'; }"
         "}"
         "async function scanWifi(){"
         "  const st=document.getElementById('scanStatus');"
         "  const sel=document.getElementById('ssidPick');"
         "  st.textContent='Scanning...';"
         "  sel.innerHTML='<option value=\"\">-- scanning --</option>';"
         "  try{"
         "    const r=await fetch('/scan',{method:'GET',cache:'no-store',credentials:'same-origin'});"
         "    if(!r.ok) throw new Error('HTTP '+r.status);"
         "    const j=await r.json();"
         "    if(!j.ok){"
         "      st.textContent='Scan failed';"
         "      sel.innerHTML='<option value=\"\">-- scan failed --</option>';"
         "      return;"
         "    }"
         "    sel.innerHTML='';"
         "    sel.appendChild(new Option('-- choose --',''));"
         "    j.items.forEach(n=>{"
         "      const label=`${n.ssid} (${n.rssi} dBm) ${n.open?'[open]':''}`;"
         "      sel.appendChild(new Option(label,n.ssid));"
         "    });"
         "    st.textContent=`Found ${j.count}`;"
         "  }catch(e){"
         "    st.textContent='Scan failed';"
         "    sel.innerHTML='<option value=\"\">-- scan failed --</option>';"
         "    console.error('WiFi scan error:', e);"
         "  }"
         "}"
         "function pickSsid(){"
         "  const sel=document.getElementById('ssidPick');"
         "  const ssid=document.getElementById('ssid');"
         "  if(sel.value) ssid.value=sel.value;"
         "}"
         "</script>");

  h += F("</body></html>");
  return h;
}

inline void resetAllAndRestart(WebServer& server) {
  factoryReset();
  clearWifiCredentials();
  server.send(200, "text/plain", "Factory reset complete. Restarting...");
  delay(800);
  ESP.restart();
}

inline void registerRoutes(WebServer& server, Settings& cfg) {
  server.on("/config", HTTP_GET, [&server, &cfg]() {
    if (!requireAuth(server, cfg.cfgUser, cfg.cfgPass)) return;
    server.send(200, "text/html", configPageHtml(cfg));
  });

  server.on("/scan", HTTP_GET, [&server, &cfg]() {
    if (!requireAuth(server, cfg.cfgUser, cfg.cfgPass)) return;
    server.send(200, "application/json", scanJson());
  });

  server.on("/save", HTTP_POST, [&server, &cfg]() {
    if (!requireAuth(server, cfg.cfgUser, cfg.cfgPass)) return;

    if (server.hasArg("title")) cfg.deviceTitle = server.arg("title");
    if (server.hasArg("stmin")) cfg.screenTimeoutMin = (uint32_t)server.arg("stmin").toInt();

    if (server.hasArg("ssid"))  cfg.wifiSsid = server.arg("ssid");
    if (server.hasArg("wpass")) cfg.wifiPass = server.arg("wpass");

    if (server.hasArg("fqdn"))  cfg.targetFqdn = server.arg("fqdn");
    if (server.hasArg("tport")) cfg.targetPort = (uint16_t)server.arg("tport").toInt();

    if (server.hasArg("token")) cfg.token = server.arg("token");

    if (server.hasArg("cuser")) cfg.cfgUser = server.arg("cuser");
    if (server.hasArg("cpass")) cfg.cfgPass = server.arg("cpass");
    if (server.hasArg("fuser")) cfg.frontUser = server.arg("fuser");
    if (server.hasArg("fpass")) cfg.frontPass = server.arg("fpass");

    auto getIntArg = [&server](const char* name, int fallback) -> int {
      if (!server.hasArg(name)) return fallback;
      return server.arg(name).toInt();
    };

    cfg.pinPowerOn = getIntArg("pwr",  cfg.pinPowerOn);
    cfg.pinBl      = getIntArg("bl",   cfg.pinBl);
    cfg.pinMoboOut = getIntArg("mbo",  cfg.pinMoboOut);
    cfg.pinCaseBtn = getIntArg("case", cfg.pinCaseBtn);
    cfg.pinBootBtn = getIntArg("boot", cfg.pinBootBtn);
    cfg.pinKeyBtn  = getIntArg("key",  cfg.pinKeyBtn);

    (void)save(cfg);

    // Redirect back to /config, then restart shortly after
    server.sendHeader("Location", "/config");
    server.send(303, "text/plain", "Saved. Redirecting...");
    delay(2500);
    ESP.restart();
  });

  server.on("/reset", HTTP_GET, [&server, &cfg]() {
    if (!requireAuth(server, cfg.cfgUser, cfg.cfgPass)) return;
    resetAllAndRestart(server);
  });
}

} // namespace ConfigPortal
