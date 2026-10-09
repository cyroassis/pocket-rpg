// Software update over WiFi (Settings > Update).
//
// Check: join a saved WiFi network, read ota/version.json from the GitHub repository and compare its
// "version" with FW_VERSION. Install: download ota/PocketRPG_update.bin from the same place into the free
// app slot, check it, then restart into it. If anything fails on the way, the old version keeps running.
// If the new version crashes before its first screen, the board goes back to the old one by itself.
//
// WiFi setup: the board opens its own network ("PocketRPG-XXXX") with a small page where the phone picks a
// network and types its password. Up to 3 networks are kept (home, phone hotspot...), newest first, in their
// own storage, so Start over keeps them.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_crt_bundle.h>
#include "app.h"
#include "version.h"
#include "ota.h"

#define RELEASE_URL "https://raw.githubusercontent.com/" FW_REPO "/main/ota/"
static const char* const VERSION_URL = RELEASE_URL "version.json";
static const char* const BIN_URL = RELEASE_URL "PocketRPG_update.bin";

void platformRadio(bool on);   // the trade radio (in the sketch) shares the WiFi chip

static void (*redrawFn)() = nullptr;
static volatile int pending = -1;
static bool wifiOn = false, setupOn = false;
static uint32_t savedAt = 0;
static WebServer* server = nullptr;
static DNSServer* dns = nullptr;
static char apName[24];
#define SETUP_HOST "pocket"   // the setup page answers at http://pocket.local (and 192.168.4.1)

static void say(int state, const char* a, const char* b, int progress) {
  appUpdateStatus(state, a, b, progress);
  Serial.printf("update: %s %s %d\n", a, b ? b : "", progress);
  if (appUpdateOpen() && redrawFn) redrawFn();
}

// ---------------------------------------------------------------- saved networks
#define NETS 3
struct Net { char ssid[33]; char pass[65]; };
static int loadNets(Net* n) {
  Preferences p; int c = 0;
  if (!p.begin("pocketwifi", true)) return 0;
  for (int i = 0; i < NETS; i++) {
    char ks[4], kp[4]; snprintf(ks, sizeof ks, "s%d", i); snprintf(kp, sizeof kp, "p%d", i);
    if (!p.isKey(ks)) continue;
    strlcpy(n[c].ssid, p.getString(ks, "").c_str(), sizeof n[c].ssid);
    strlcpy(n[c].pass, p.isKey(kp) ? p.getString(kp, "").c_str() : "", sizeof n[c].pass);
    if (n[c].ssid[0]) c++;
  }
  p.end();
  return c;
}
static void saveNet(const char* ssid, const char* pass) {
  Net old[NETS], all[NETS]; int c = loadNets(old), k = 0;
  strlcpy(all[k].ssid, ssid, sizeof all[k].ssid); strlcpy(all[k].pass, pass, sizeof all[k].pass); k++;
  for (int i = 0; i < c && k < NETS; i++) if (strcmp(old[i].ssid, ssid)) all[k++] = old[i];
  Preferences p; p.begin("pocketwifi", false); p.clear();
  for (int i = 0; i < k; i++) {
    char ks[4], kp[4]; snprintf(ks, sizeof ks, "s%d", i); snprintf(kp, sizeof kp, "p%d", i);
    p.putString(ks, all[i].ssid); p.putString(kp, all[i].pass);
  }
  p.end();
}

// ---------------------------------------------------------------- WiFi on / off
static void stopSetup() {
  if (!setupOn) return;
  server->stop(); delete server; server = nullptr;
  dns->stop(); delete dns; dns = nullptr;
  MDNS.end();
  WiFi.softAPdisconnect(true);
  setupOn = false; savedAt = 0;
}
static void wifiOff() {
  stopSetup();
  if (wifiOn) { WiFi.disconnect(true); WiFi.mode(WIFI_OFF); wifiOn = false; }
}

static bool connect() {
  Net n[NETS]; int c = loadNets(n);
  if (!c) { say(U_NO_WIFI, "No WiFi saved", "Tap WiFi to add one", -1); return false; }
  if (wifiOn && WiFi.status() == WL_CONNECTED) return true;
  platformRadio(false);
  say(U_BUSY, "Looking for WiFi...", "", -1);
  WiFi.persistent(false);   // our own list keeps the networks: the WiFi driver must not reconnect by itself later
  WiFi.mode(WIFI_STA); wifiOn = true;
  WiFi.setAutoReconnect(false);
  int found = WiFi.scanNetworks();
  int pick = -1, best = -1000;
  for (int i = 0; i < found; i++)
    for (int j = 0; j < c; j++)
      if (WiFi.SSID(i) == n[j].ssid && WiFi.RSSI(i) > best) { best = WiFi.RSSI(i); pick = j; }
  WiFi.scanDelete();
  if (pick < 0) { say(U_ERROR, "Saved WiFi not found", "Get closer, or tap WiFi to add one", -1); wifiOff(); return false; }
  say(U_BUSY, "Connecting...", n[pick].ssid, -1);
  WiFi.begin(n[pick].ssid, n[pick].pass);
  uint32_t t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) delay(100);
  if (WiFi.status() != WL_CONNECTED) { say(U_ERROR, "Could not connect", "Wrong password? Tap WiFi", -1); wifiOff(); return false; }
  Serial.printf("update: on %s, %s\n", n[pick].ssid, WiFi.localIP().toString().c_str());
  return true;
}

// ---------------------------------------------------------------- check
static void httpConfig(esp_http_client_config_t& cfg, const char* url) {
  memset(&cfg, 0, sizeof cfg);
  cfg.url = url;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;   // the usual web certificates
  cfg.timeout_ms = 15000;
  cfg.buffer_size = 4096;      // GitHub's download links are long
  cfg.buffer_size_tx = 2048;
  cfg.user_agent = "PocketRPG";
  cfg.max_redirection_count = 5;
}
// version.json: {"version": 2, "notes": "New capes"}. Returns the version, or minus the HTTP status.
static int fetchVersion(char* notes, int notesSize) {
  esp_http_client_config_t cfg; httpConfig(cfg, VERSION_URL);
  esp_http_client_handle_t h = esp_http_client_init(&cfg);
  if (!h) return -1;
  static char body[1024];
  int len = -1, status = 0;
  for (int hop = 0; hop < 6; hop++) {   // GitHub answers with redirects to the file
    if (esp_http_client_open(h, 0) != ESP_OK) break;
    esp_http_client_fetch_headers(h);
    status = esp_http_client_get_status_code(h);
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      esp_http_client_set_redirection(h); esp_http_client_close(h); continue;
    }
    if (status == 200) len = esp_http_client_read_response(h, body, sizeof body - 1);
    break;
  }
  esp_http_client_close(h); esp_http_client_cleanup(h);
  Serial.printf("update: version.json status %d, %d bytes\n", status, len);
  if (status != 200 || len <= 0) return status ? -status : -1;
  body[len] = 0;
  const char* v = strstr(body, "\"version\"");
  if (!v || !(v = strchr(v, ':'))) return -1;
  int ver = atoi(v + 1);
  notes[0] = 0;
  const char* n = strstr(body, "\"notes\"");
  if (n && (n = strchr(n, ':')) && (n = strchr(n, '"'))) {
    n++; int k = 0;
    while (*n && *n != '"' && k < notesSize - 1) notes[k++] = *n++;
    notes[k] = 0;
  }
  return ver;
}
static void check() {
  if (!connect()) return;
  say(U_BUSY, "Checking...", "Asking GitHub", -1);
  char notes[64]; int v = fetchVersion(notes, sizeof notes);
  if (v <= 0) {
    say(U_ERROR, "Could not check", v == -404 ? "No release yet" : "Try again in a moment", -1);
    wifiOff(); return;
  }
  char a[40], b[64];
  if (v <= FW_VERSION) {
    snprintf(b, sizeof b, "Version %d is the newest", FW_VERSION);
    say(U_LATEST, "You're up to date", b, -1);
    wifiOff(); return;
  }
  snprintf(a, sizeof a, "Version %d is ready", v);
  say(U_AVAILABLE, a, notes[0] ? notes : "Tap Install to get it", -1);   // WiFi stays on for the download
}

// ---------------------------------------------------------------- install
static void install() {
  if (!connect()) return;
  say(U_DOWNLOAD, "Downloading", "Keep the board close to WiFi", 0);
  esp_http_client_config_t cfg; httpConfig(cfg, BIN_URL);
  cfg.keep_alive_enable = true;
  esp_https_ota_config_t oc; memset(&oc, 0, sizeof oc);
  oc.http_config = &cfg;
  esp_https_ota_handle_t oh = nullptr;
  if (esp_https_ota_begin(&oc, &oh) != ESP_OK) { say(U_ERROR, "Download failed", "Check the WiFi and try again", -1); wifiOff(); return; }
  int total = esp_https_ota_get_image_size(oh), last = -1;
  esp_err_t e;
  while ((e = esp_https_ota_perform(oh)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
    int got = esp_https_ota_get_image_len_read(oh);
    int p = total > 0 ? (int)((int64_t)got * 100 / total) : 0;
    if (p != last) { last = p; say(U_DOWNLOAD, "Downloading", "Keep the board close to WiFi", p); }
  }
  if (e != ESP_OK || !esp_https_ota_is_complete_data_received(oh)) {
    esp_https_ota_abort(oh);
    say(U_ERROR, "Download stopped", "Nothing changed. Try again", -1); wifiOff(); return;
  }
  if (esp_https_ota_finish(oh) != ESP_OK) { say(U_ERROR, "The file was damaged", "Nothing changed. Try again", -1); wifiOff(); return; }
  say(U_DONE, "Updated!", "Restarting...", 100);
  wifiOff();
  delay(1500);
  esp_restart();
}

// ---------------------------------------------------------------- WiFi setup page (on the phone)
static String nets[20];
static int netCount = 0;
static String esc(const String& s) {
  String o; o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;"; else if (c == '\'') o += "&#39;"; else o += c;
  }
  return o;
}
static const char PAGE_HEAD[] PROGMEM = R"(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Pocket RPG WiFi</title><style>
body{margin:0;background:#0E0D13;color:#EEE9F7;font:16px system-ui,sans-serif;padding:20px}
h1{font-size:22px;color:#E9B44C;margin:0 0 4px}p{color:#9C93B4;margin:0 0 18px}
label.n{display:flex;gap:10px;align-items:center;background:#1B1726;border:2px solid #3B3350;border-radius:12px;padding:12px;margin:8px 0}
input[type=text],input[type=password]{width:100%;box-sizing:border-box;background:#262036;color:#EEE9F7;border:2px solid #3B3350;border-radius:10px;padding:12px;font-size:16px;margin-top:6px}
button{width:100%;background:#E9B44C;color:#24180A;border:0;border-radius:12px;padding:14px;font-size:18px;font-weight:700;margin-top:18px}
.s{display:block;margin-top:16px;color:#9C93B4}</style></head><body>
<h1>Pocket RPG</h1><p>Pick the WiFi the board should use for updates.</p><form method="post" action="/save">)";
static void handlePage() {
  String h = FPSTR(PAGE_HEAD);
  for (int i = 0; i < netCount; i++)
    h += "<label class=n><input type=radio name=ssid value=\"" + esc(nets[i]) + "\"" + (i == 0 ? " checked" : "") + ">" + esc(nets[i]) + "</label>";
  h += "<label class=n><input type=radio name=ssid value=\"\"" + String(netCount ? "" : " checked") + ">Other network</label>";
  h += "<input type=text name=other placeholder=\"Network name (if Other)\" autocapitalize=none autocorrect=off>";
  h += "<span class=s>Password</span><input type=password name=pass id=pw autocomplete=off>";
  h += "<label style=\"display:block;margin-top:10px;color:#9C93B4\"><input type=checkbox onclick=\"pw.type=this.checked?'text':'password'\"> Show password</label>";
  h += "<button>Save</button></form></body></html>";
  server->send(200, "text/html", h);
}
static void handleSave() {
  String ssid = server->arg("ssid");
  if (!ssid.length()) ssid = server->arg("other");
  ssid.trim();
  String pass = server->arg("pass");
  if (!ssid.length() || ssid.length() > 32 || pass.length() > 64) {
    server->send(200, "text/html", "<meta name=viewport content='width=device-width'><body style='background:#0E0D13;color:#EEE9F7;font:18px system-ui;padding:20px'>Pick a network first. <a style='color:#E9B44C' href='/'>Back</a>");
    return;
  }
  saveNet(ssid.c_str(), pass.c_str());
  server->send(200, "text/html", "<meta name=viewport content='width=device-width'><body style='background:#0E0D13;color:#EEE9F7;font:18px system-ui;padding:20px'><h2 style='color:#E9B44C'>Saved!</h2>Look at your Pocket RPG: it is connecting to <b>" + esc(ssid) + "</b>. You can leave this WiFi now.");
  savedAt = millis();
}
static void startSetup() {
  platformRadio(false);
  stopSetup();
  say(U_BUSY, "Looking for WiFi...", "", -1);
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA); wifiOn = true;
  int found = WiFi.scanNetworks();
  netCount = 0;
  for (int i = 0; i < found && netCount < 20; i++) {   // strongest first, no repeats, no hidden ones
    String s = WiFi.SSID(i); if (!s.length()) continue;
    bool dup = false; for (int k = 0; k < netCount; k++) if (nets[k] == s) dup = true;
    if (!dup) nets[netCount++] = s;
  }
  WiFi.scanDelete();
  uint8_t mac[6]; WiFi.macAddress(mac);
  snprintf(apName, sizeof apName, "PocketRPG-%02X%02X", mac[4], mac[5]);
  IPAddress ip(192, 168, 4, 1);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName);
  dns = new DNSServer();
  dns->setErrorReplyCode(DNSReplyCode::NoError);
  dns->start(53, "*", ip);   // every name leads here, so phones open the page by themselves
  MDNS.begin(SETUP_HOST);    // "pocket.local" also works on phones that ask for .local names separately
  server = new WebServer(80);
  server->on("/", HTTP_GET, handlePage);
  server->on("/save", HTTP_POST, handleSave);
  server->onNotFound([] { server->sendHeader("Location", "http://192.168.4.1/", true); server->send(302, "text/plain", ""); });
  server->begin();
  setupOn = true;
  say(U_SETUP, apName, "", -1);
}

// ---------------------------------------------------------------- the sketch's side
void platformUpdate(int action) { pending = action; }
void otaBegin(void (*redraw)()) { redrawFn = redraw; }
void otaStop() { pending = -1; wifiOff(); }
void otaPoll() {
  if (setupOn) {
    dns->processNextRequest();
    server->handleClient();
    if (savedAt && millis() - savedAt > 1500) { stopSetup(); WiFi.mode(WIFI_OFF); wifiOn = false; pending = UA_CHECK; }
  }
  if (pending < 0) return;
  int a = pending; pending = -1;
  switch (a) {
    case UA_CHECK: stopSetup(); check(); break;
    case UA_INSTALL: install(); break;
    case UA_SETUP: startSetup(); break;
    case UA_STOP: wifiOff(); break;
  }
}
