#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <ESPmDNS.h>
#include <time.h>
#include <math.h>
#include "DisplaySchedule.h"
#include "ConnectionPulse.h"
#include "CouponFlashEvents.h"
#include "ColorSettings.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#define OLED_SDA 8
#define OLED_SCL 9
#define LED_PIN 4
#define LED_COUNT 12
#define UPDATE_MS 3000UL
#define MAX_AGE_SEC 30
const char* AP_NAME = "FICSIT-POWER-SETUP";
const char* AP_PASSWORD = "12345678"; // Changer apres les tests
const char* ROOT_CA = R"CERT(
-----BEGIN CERTIFICATE-----
MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw
CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg
R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00
MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT
ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw
EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW
+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9
ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T
AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI
zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW
tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1
/q4AaOeMSQ+2b1tbFfLn
-----END CERTIFICATE-----
)CERT";
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
Adafruit_NeoPixel ring(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
WebServer web(80);
Preferences preferences;
String exportURL = "https://calculator.orguerra.com/export";
bool usbMode = false;
void serviceUSB();
void resetUSB();
String adminPassword = "1234"; // Changer dans la page Web
int selectedCircuit = -1;  // -1 = somme de tous les groupes independants
bool customCircuits = false;
String circuitSelection = "";
JsonDocument circuitNames;
JsonDocument circuitCatalog;
bool catalogFresh = false;
unsigned long catalogReceivedAt = 0;
const int MAX_CIRCUITS = 64;
int brightness = 40;
bool ringEnabled = true;
bool screenEnabled = true;
int oledContrast = 180; // SH1106 : contraste 0..255 (luminosite percue)
int flashBrightness = 190; // Intensite blanche du flash : 1..255
const unsigned long FLASH_DURATION_MS = 1100;
unsigned long flashStartedAt = 0;
bool flashRunning = false;
bool lastFuseState = false;
const unsigned long RING_REFRESH_MS = 65;
int currentCircuit = -1;
float consumed = 0, produced = 0, capacity = 0, maxConsumed = 0;
float batteryPct = 0, batteryCap = 0, batteryOut = 0, batteryIn = 0;
float batteryDiff = 0;
String batteryTime = "--:--:--";
String statusMessage = "Demarrage";
bool dataValid = false, batteryMode = false, fuseTriggered = false;
unsigned long lastPoll = 0;
bool debugMode = false; // Toujours ONLINE apres un redemarrage
int debugPreviewPage = -1; // -1 Auto, 0 Energie, 1 Bons, 2 Vehicule ; non sauvegarde.

bool debugCut = false;
float debugConsumed = 10125, debugProduced = 16455;
float debugBatteryPct = 68, debugBatteryOut = 1800;
int debugHours = 2, debugMinutes = 17;
bool debugFuse = false;
bool debugConnecting = false;
unsigned long lastRing = 0;
std::atomic<bool> startupConnectionPulse{false};
std::atomic<TaskHandle_t> connectionPulseTaskHandle{nullptr};
bool connectionPulseActive = false;
unsigned long connectionPulseStartedAt = 0;
bool energyPageEnabled = true, couponPageEnabled = false;
int energyPageSeconds = 10, couponPageSeconds = 10;
DisplaySchedule displaySchedule;
bool couponFlashEnabled=false;
uint32_t couponRingColor=0xff6400, couponFlashColor=0xffffff;
int couponFlashBrightness=190;
CouponFlashEvents couponEvents;
bool autoVehicle = false;
String playerName = "";
bool couponsValid = false, vehicleActive = false, fuelKnown = false;
bool showingVehicle() { return debugMode && debugPreviewPage>=0 ? debugPreviewPage==2 : vehicleActive; }
bool showingCoupons() { return debugMode && debugPreviewPage>=0 ? debugPreviewPage==1 : displaySchedule.page==1; }
unsigned long coupons = 0, couponWorldTotal = 0;
bool couponWorldTotalValid = false;
float couponProgress = 0, fuelAmount = 0;
float vehicleFuelFraction() { return fuelKnown ? constrain(fuelAmount/100.0f,0.0f,1.0f) : 0.0f; }
String vehicleFuelName = "CARBURANT", vehicleName = "", vehicleKind = "";
bool debugVehicle = false, debugFuelKnown = true, debugCouponsValid = true;
unsigned long debugCoupons = 42, debugCouponWorldTotal = 125;
bool debugCouponWorldKnown = true;
float debugProgress = 55, debugFuel = 80;
String debugVehicleName="Hélicoptère FICSIT", debugVehicleFuelName="Carburant conditionné";

void resetExtraData() {
  couponsValid = false; couponWorldTotalValid = false; vehicleActive = false; fuelKnown = false;
  vehicleKind = "";
}
String urlEncode(const String& value) {
  String result;
  const char* hex = "0123456789ABCDEF";
  for (size_t i=0; i<value.length(); i++) {
    uint8_t c = (uint8_t)value[i];
    if ((c>='a' && c<='z') || (c>='A' && c<='Z') ||
        (c>='0' && c<='9') || c=='-' || c=='_' || c=='.' || c=='~') result += (char)c;
    else { result += '%'; result += hex[c>>4]; result += hex[c&15]; }
  }
  return result;
}
// Strict IDs and unique membership, independent of JSON row order.
bool parseCircuitIDs(const String& text, int* ids, int& count) {
  count=0;
  if(text.isEmpty()) return true;
  int start=0;
  while(start<(int)text.length()) {
    int end=text.indexOf(',',start); if(end<0) end=text.length();
    if(end==start || count>=MAX_CIRCUITS || (end-start>1 && text[start]=='0')) return false;
    unsigned long id=0;
    for(int i=start;i<end;i++) {
      char c=text[i];
      if(c<'0' || c>'9' || id>(2147483647UL-(c-'0'))/10UL) return false;
      id=id*10UL+(c-'0');
    }
    for(int i=0;i<count;i++) if(ids[i]==(int)id) return false;
    ids[count++]=(int)id;
    start=end+1;
    if(start==(int)text.length()+1) break;
    if(start==(int)text.length()) return false;
  }
  return true;
}
bool circuitChosen(int id) {
  if(!customCircuits) return selectedCircuit==-1 || id==selectedCircuit;
  return (","+circuitSelection+",").indexOf(","+String(id)+",")>=0;
}
void applyCircuitTotals(JsonArray circuits);
void buildDebugCircuits(JsonDocument& doc) {
  JsonArray rows=doc.to<JsonArray>();
  JsonArray known=circuitCatalog.as<JsonArray>();
  int count=known.size();
  int fallback[]={0,2};
  if(count==0) count=2;
  char t[20]; snprintf(t,sizeof(t),"%02d:%02d:00",debugHours,debugMinutes);
  for(int i=0;i<count;i++) {
    JsonObject c=rows.add<JsonObject>();
    c["id"]=known.isNull() || known.size()==0 ? fallback[i] : (int)(known[i]["id"]|-999);
    c["consumption_mw"]=debugConsumed/count;
    c["production_mw"]=debugProduced/count;
    c["capacity_mw"]=debugProduced/count;
    c["battery_capacity_mwh"]=7200.0f/count;
    c["battery_percent"]=debugBatteryPct;
    c["battery_output_mw"]=debugCut ? debugBatteryOut/count : 0;
    c["battery_differential_mw"]=debugCut ? -debugBatteryOut/count : 0;
    c["battery_time_empty"]=t;
    c["fuse_triggered"]=debugFuse;
  }
}
void applyDebug() {
  couponsValid = debugCouponsValid; coupons = debugCoupons;
  couponProgress = debugProgress;
  couponWorldTotal=max(debugCoupons,debugCouponWorldTotal);
  couponWorldTotalValid=debugCouponsValid && debugCouponWorldKnown;
  vehicleActive = autoVehicle && debugVehicle;
  fuelKnown = debugFuelKnown; fuelAmount = debugFuel;
  vehicleName=debugVehicleName; vehicleKind=debugVehicleName;
  vehicleFuelName=debugVehicleFuelName;
  JsonDocument simulated;
  buildDebugCircuits(simulated);
  applyCircuitTotals(simulated.as<JsonArray>());
  batteryMode = dataValid && debugCut && batteryCap>0;
  char t[20];
  snprintf(t, sizeof(t), "%02d:%02d:00", debugHours, debugMinutes);
  batteryTime = String(t);
  if(dataValid) statusMessage = "Simulation DEBUG";
}
void loadSettings() {
  preferences.begin("ficsit", false);
  exportURL = preferences.getString("url", exportURL);
  usbMode = preferences.getBool("usbMode", false);
  adminPassword = preferences.getString("password", adminPassword);
  int legacyDisplay = constrain(preferences.getInt("displayMode",0),0,1);
  energyPageEnabled=preferences.getBool("pageEnergy",legacyDisplay==0);
  couponPageEnabled=preferences.getBool("pageCoupons",legacyDisplay==1);
  if(!energyPageEnabled && !couponPageEnabled) energyPageEnabled=true;
  energyPageSeconds=constrain(preferences.getInt("energySeconds",10),1,600);
  couponPageSeconds=constrain(preferences.getInt("couponSeconds",10),1,600);
  displaySchedule.reset(energyPageEnabled,millis());
  couponFlashEnabled=preferences.getBool("couponFlashOn",false);
  couponRingColor=preferences.getUInt("couponColor",0xff6400)&0xffffff;
  couponFlashColor=preferences.getUInt("couponFlashClr",0xffffff)&0xffffff;
  couponFlashBrightness=constrain(preferences.getInt("couponFlashBr",190),1,255);
  autoVehicle = preferences.getBool("autoVehicle",false);
  playerName = preferences.getString("playerName","");
  selectedCircuit = preferences.getInt("circuit", -1);
  customCircuits = preferences.getBool("customCircuits",false);
  circuitSelection = preferences.getString("circuitIds","");
  int ids[MAX_CIRCUITS], count;
  if(!parseCircuitIDs(circuitSelection,ids,count)) {customCircuits=false; circuitSelection="";}
  deserializeJson(circuitNames,preferences.getString("circuitNames","{}"));
  if(!circuitNames.is<JsonObject>()) circuitNames.to<JsonObject>();
  brightness = constrain(preferences.getInt("brightness", 40), 1, 255);
  flashBrightness = constrain(preferences.getInt("flashBright", 190), 1, 255);
  ringEnabled = preferences.getBool("ringOn", true);
  screenEnabled = preferences.getBool("screenOn", true);
  oledContrast = constrain(preferences.getInt("oledContrast", 180), 0, 255);
}
void saveSettings() {
  preferences.putBool("usbMode",usbMode);
  preferences.putInt("displayMode",energyPageEnabled ? 0 : 1);
  preferences.putBool("pageEnergy",energyPageEnabled);
  preferences.putBool("pageCoupons",couponPageEnabled);
  preferences.putInt("energySeconds",energyPageSeconds);
  preferences.putInt("couponSeconds",couponPageSeconds);
  preferences.putBool("couponFlashOn",couponFlashEnabled);
  preferences.putUInt("couponColor",couponRingColor);
  preferences.putUInt("couponFlashClr",couponFlashColor);
  preferences.putInt("couponFlashBr",couponFlashBrightness);
  preferences.putBool("autoVehicle",autoVehicle);
  preferences.putString("playerName",playerName);
  preferences.putString("url", exportURL);
  preferences.putString("password", adminPassword);
  preferences.putInt("circuit", selectedCircuit);
  preferences.putBool("customCircuits",customCircuits);
  preferences.putString("circuitIds",circuitSelection);
  String names; serializeJson(circuitNames,names);
  preferences.putString("circuitNames",names);
  preferences.putInt("brightness", brightness);
  preferences.putInt("flashBright", flashBrightness);
  preferences.putBool("ringOn", ringEnabled);
  preferences.putBool("screenOn", screenEnabled);
  preferences.putInt("oledContrast", oledContrast);
}
bool authenticated() {
  if (web.authenticate("admin", adminPassword.c_str())) return true;
  web.requestAuthentication();
  return false;
}
String htmlEscape(String s) {
  s.replace("&", "&amp;"); s.replace("<", "&lt;"); s.replace(">", "&gt;");
  s.replace("\"", "&quot;"); s.replace("'", "&#39;"); return s;
}
void oledDisplay();
void updateRing();
void checkFuseTransition();
String colorHex(uint32_t color){
  char value[8];snprintf(value,sizeof(value),"#%06lx",(unsigned long)(color&0xffffff));return String(value);
}
void page() {
  if (!authenticated()) return;
  String h = R"HTML(<!doctype html><html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FICSIT Power Monitor</title><link rel="icon" type="image/svg+xml" href="/favicon.svg"><style>
body{background:#151b22;color:#eee;font-family:Arial,sans-serif;margin:0;padding:16px}
main{max-width:650px;margin:auto}h1{color:#ff992e;font-size:25px}
.card{background:#27313c;border-radius:12px;padding:18px;margin:14px 0}
label{display:block;margin:14px 0 6px}input:not([type=checkbox]),select{box-sizing:border-box;width:100%;padding:10px;background:#111923;color:white;border:1px solid #687583;border-radius:6px}
input[type=range]{padding:0!important;accent-color:#ff982b}input[type=checkbox]{width:19px;height:19px;accent-color:#ff982b}
[hidden]{display:none!important}.tabs{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:16px}.tabs button{padding:9px}.tabs [aria-selected=true]{outline:2px solid white}input[type=color]{height:42px}
button{border:0;border-radius:7px;padding:13px;background:#ff982b;color:#111;font-weight:bold;cursor:pointer}
.btns{display:flex;gap:10px}.btns button{flex:1}.selected{outline:2px solid white}
.row{display:flex;justify-content:space-between;gap:12px;align-items:center}
.installation{border-top:1px solid #52606e;padding:12px 0}.installation label{display:flex;align-items:center;gap:10px;margin:0 0 6px}.installation input[type=text]{margin-top:6px}.installation small{display:block;margin-top:5px}small{color:#a9bac8}.val{color:#ffb45d;font-weight:bold}#debugCard{display:none}
</style></head><body><main><h1>FICSIT POWER MONITOR</h1>
<div class="card"><div class="row"><b>Mode de fonctionnement</b><span id="currentMode"></span></div>
<div class="btns" style="margin-top:15px"><button type="button" id="onlineBtn" onclick="setMode(0)">ONLINE</button><button type="button" id="debugBtn" onclick="setMode(1)">DEBUG</button></div>
<p><small>Adresse : http://monitor.local</small></p><p id="feedback"></p></div>
<div class="card"><form method="post" action="/save"><input type="hidden" name="pagesVersion" value="1"><input type="hidden" name="ledVersion" value="1"><nav class="tabs" role="tablist" aria-label="Réglages par mode"><button type="button" role="tab" id="tab-general" aria-controls="panel-general" aria-selected="true" onclick="openTab('general')">Général</button><button type="button" role="tab" id="tab-energy" aria-controls="panel-energy" aria-selected="false" onclick="openTab('energy')">Énergie</button><button type="button" role="tab" id="tab-coupons" aria-controls="panel-coupons" aria-selected="false" onclick="openTab('coupons')">Bons FICSIT</button><button type="button" role="tab" id="tab-vehicle" aria-controls="panel-vehicle" aria-selected="false" onclick="openTab('vehicle')">Véhicule</button><button type="button" role="tab" id="tab-debug" aria-controls="panel-debug" aria-selected="false" onclick="openTab('debug')">DEBUG</button></nav><section role="tabpanel" id="panel-general" aria-labelledby="tab-general"><label>Source des donnees</label><select name="transport"><option value="wifi" %WIFICHECK%>Reseau / HTTPS</option><option value="usb" %USBCHECK%>USB / COM FRM (115200 bauds)</option></select><p><small>Le Wi-Fi reste actif dans les deux modes pour acceder au portail. FRM USB : 115200 bauds, Auto_Serial desactive.</small></p><label>URL JSON (source reseau)</label><input name="url" maxlength="180" value="%URL%">
<label>Luminosite : <span class="val" id="vl"></span> / 255</label><input id="lum" name="brightness" type="range" min="1" max="255" value="%BRIGHT%" oninput="document.getElementById('vl').textContent=this.value; previewBrightness(this.value)">
<label style="display:flex;align-items:center;gap:10px"><input type="checkbox" name="ringOn" value="1" %RINGCHECK%> Activer anneau WS2812B</label>
<label style="display:flex;align-items:center;gap:10px"><input type="checkbox" name="screenOn" value="1" %SCREENCHECK%> Activer ecran OLED</label>
<label>Contraste OLED : <span class="val" id="vcontrast">%CONTRAST%</span> / 255</label><input id="contrast" name="oledContrast" type="range" min="0" max="255" value="%CONTRAST%" oninput="document.getElementById('vcontrast').textContent=this.value;previewContrast(this.value)">
<label>Nouveau mot de passe admin (facultatif)</label><input type="password" name="password" minlength="12" autocomplete="new-password">
</section><section role="tabpanel" id="panel-energy" aria-labelledby="tab-energy" hidden><label><input id="pageEnergy" type="checkbox" name="pageEnergy" value="1" %ENERGYCHECK%> Energie</label>
<label>Duree de la page Energie (secondes)</label><input type="number" name="energySeconds" min="1" max="600" required value="%ENERGYSECONDS%">
<h3>Installations electriques surveillees</h3>
<input type="hidden" name="selectionVersion" value="1">
<input type="hidden" id="circuitSelection" name="circuitSelection" value="%SELECTION%">
<input type="hidden" id="circuitNames" name="circuitNames" value="%NAMES%">
<label><input id="watchAll" type="checkbox" name="watchAll" value="1" %ALLCHECK% onchange="watchAllChanged()"> Tous les circuits actuels et futurs</label>
<p><small>Decochez cette option pour choisir les installations a additionner. Les noms sont personnalisables et sauvegardes dans le boitier. Un circuit correspond a un groupe electrique independant.</small></p>
<p id="circuitStatus">Chargement des installations...</p><div id="circuitList"></div>
<p id="selectionTotal" class="val"></p>
<label>Luminosite du flash blanc : <span class="val" id="vflash">%FLASHBRIGHT%</span> / 255</label><input id="flashlum" name="flashBrightness" type="range" min="1" max="255" value="%FLASHBRIGHT%" oninput="document.getElementById('vflash').textContent=this.value">
</section><section role="tabpanel" id="panel-coupons" aria-labelledby="tab-coupons" hidden><label><input id="pageCoupons" type="checkbox" name="pageCoupons" value="1" %COUPONCHECK%> Bons FICSIT</label>
<label>Duree de la page Bons FICSIT (secondes)</label><input type="number" name="couponSeconds" min="1" max="600" required value="%COUPONSECONDS%">
<label><input name="couponFlashOn" type="checkbox" value="1" %COUPONFLASHCHECK%> Flash à chaque nouveau bon produit</label>
<label>Couleur de l’anneau Bons FICSIT</label><input type="color" name="couponColor" value="%COUPONCOLOR%">
<label>Couleur du flash des bons</label><input type="color" name="couponFlashColor" value="%COUPONFLASHCOLOR%">
<label>Luminosité du flash des bons (1–255)</label><input type="range" name="couponFlashBrightness" min="1" max="255" value="%COUPONFLASHBRIGHT%" oninput="el('couponFlashValue').textContent=this.value"><span id="couponFlashValue">%COUPONFLASHBRIGHT%</span>
<p><small>Un flash par bon ajouté au solde du broyeur. Pour essayer en DEBUG, augmenter les bons disponibles après activation du flash.</small></p></section><section role="tabpanel" id="panel-vehicle" aria-labelledby="tab-vehicle" hidden><label><input type="checkbox" name="autoVehicle" value="1" %AUTOCHECK%> Vehicule automatique</label>
<label>Pseudo exact du conducteur</label><input name="playerName" maxlength="64" value="%PLAYER%">

<p><small>Les jauges écran et LED indiquent le stock restant sur 100 unités. Le véhicule reste prioritaire sur les pages Énergie et Bons.</small></p></section><section role="tabpanel" id="panel-debug" aria-labelledby="tab-debug" hidden><p id="debugOnlineHint">Activer DEBUG en haut de la page pour utiliser les simulations.</p><div class="card" id="debugCard"><h2>Simulation</h2>
<div class="btns"><button type="button" id="normalBtn" onclick="setCut(0)">RESEAU</button><button type="button" id="cutBtn" onclick="setCut(1)">COUPURE / BATTERIES</button></div>
<label>Consommation : <span class="val" id="vc"></span> MW</label><input id="c" type="range" min="0" max="30000" step="25" oninput="changed()">
<label>Production : <span class="val" id="vp"></span> MW</label><input id="p" type="range" min="0" max="30000" step="25" oninput="changed()">
<label>Batteries restantes : <span class="val" id="vb"></span> %</label><input id="b" type="range" min="0" max="100" oninput="changed()">
<label>Decharge : <span class="val" id="vo"></span> MW</label><input id="o" type="range" min="0" max="10000" step="50" oninput="changed()">
<div class="row"><div style="flex:1"><label>Autonomie heures</label><input id="hh" type="number" min="0" max="99" oninput="changed()"></div>
<div style="flex:1"><label>Minutes</label><input id="mm" type="number" min="0" max="59" oninput="changed()"></div></div>
<label><input id="fuse" type="checkbox" onchange="changed()"> Disjoncteur declenche</label>
<label><input id="connecting" type="checkbox" %DCONNECTING% onchange="changed()"> Connexion Wi-Fi simulee : pulse orange</label>
<label>Bons disponibles</label><input id="coupons" type="number" min="0" max="1000000" value="%DCOUPONS%" oninput="changed()">
<label>Total des bons disponibles sur la map (broyeur inclus)</label><input id="couponWorld" type="number" min="0" max="1000000" value="%DWORLD%" oninput="changed()">
<label><input id="couponWorldKnown" type="checkbox" %DWORLDKNOWN% onchange="changed()"> Total map disponible</label>
<label>Progression du prochain bon (%)</label><input id="progress" type="range" min="0" max="100" value="%DPROGRESS%" oninput="changed()">
<label><input id="couponKnown" type="checkbox" %DCOUPONKNOWN% onchange="changed()"> Donnees coupons disponibles</label>
<label><input id="vehicle" type="checkbox" %DVEHICLE% onchange="changed()"> Je conduis un vehicule (activer Vehicule automatique)</label>
<label>Carburant (unites)</label><input id="fuelAmount" type="number" min="0" max="1000000" value="%DFUEL%" oninput="changed()">
<label><input id="fuelKnown" type="checkbox" %DFUELKNOWN% onchange="changed()"> Quantite de carburant disponible</label>
<label>Nom du véhicule</label><input id="debugVehicleName" maxlength="64" value="%DVEHICLENAME%" oninput="changed()"><label>Nom du consommable</label><input id="debugFuelName" maxlength="64" value="%DFUELNAME%" oninput="changed()">
<p><small>Les valeurs sont temporaires et ne sont pas enregistrees dans la memoire flash.</small></p></div>
</section><p><small>Une page cochée reste fixe ; les deux alternent selon leur durée.</small></p><button type="submit">ENREGISTRER TOUS LES RÉGLAGES</button></form></div>
<script>
let state=%STATE%;let timer=null;let previewPage="auto";
const el=id=>document.getElementById(id);
function openTab(name){
if(name!=="debug"){previewPage=({energy:"energy",coupons:"coupons",vehicle:"vehicle"})[name] || "auto";if(state.debug)send("/debug-view",{page:previewPage});}
for(const key of ['general','energy','coupons','vehicle','debug']){
el('panel-'+key).hidden=key!==name;
el('tab-'+key).setAttribute('aria-selected',String(key===name));
}
}
function show(){
el('currentMode').textContent=state.debug?'DEBUG':'ONLINE';
el('onlineBtn').classList.toggle('selected',!state.debug);el('debugBtn').classList.toggle('selected',state.debug);
el('debugCard').style.display=state.debug?'block':'none';el('debugOnlineHint').hidden=state.debug;
el('normalBtn').classList.toggle('selected',!state.cut);el('cutBtn').classList.toggle('selected',state.cut);
for(const k of ['c','p','b','o','hh','mm'])el(k).value=state[k];
el('fuse').checked=state.fuse;
labels();el('vl').textContent=el('lum').value;
}
function labels(){for(const [k,id] of [['c','vc'],['p','vp'],['b','vb'],['o','vo']])el(id).textContent=el(k).value;}
async function send(path,data){try{const body=new URLSearchParams(data);const r=await fetch(path,{method:'POST',body});if(!r.ok)throw Error('HTTP '+r.status);el('feedback').textContent='Parametres appliques';}catch(e){el('feedback').textContent='Erreur : '+e.message;}}
async function setMode(v){state.debug=!!v;show();await send('/mode',{debug:v,page:previewPage});}
function setCut(v){state.cut=!!v;show();changed();}
function changed(){labels();if(!state.debug)return;clearTimeout(timer);timer=setTimeout(()=>send('/debug',{cut:state.cut?1:0,c:el('c').value,p:el('p').value,b:el('b').value,o:el('o').value,hh:el('hh').value,mm:el('mm').value,fuse:el('fuse').checked?1:0,connecting:el('connecting').checked?1:0,coupons:el('coupons').value,couponWorld:el('couponWorld').value,couponWorldKnown:el('couponWorldKnown').checked?1:0,progress:el('progress').value,couponKnown:el('couponKnown').checked?1:0,vehicle:el('vehicle').checked?1:0,fuelAmount:el('fuelAmount').value,fuelKnown:el('fuelKnown').checked?1:0,vehicleName:el('debugVehicleName').value,fuelName:el('debugFuelName').value}),160);}
let contrastTimer;function previewContrast(v){clearTimeout(contrastTimer);contrastTimer=setTimeout(()=>send('/oled-contrast',{value:v}),160);}
let brightnessTimer;function previewBrightness(v){clearTimeout(brightnessTimer);brightnessTimer=setTimeout(()=>send('/brightness',{value:v}),160);}
let circuitRows=[], circuitInputs=new Map(), circuitNames=JSON.parse(el('circuitNames').value || '{}');
let chosen=new Set(el('circuitSelection').value.split(',').filter(Boolean));
let circuitsBusy=false;
function watchAllChanged(){
if(!el('watchAll').checked) chosen=new Set([...circuitInputs].filter(([id,pair])=>pair.check.checked).map(([id])=>id));
selectionChanged();
}
function selectionChanged(){
const all=el('watchAll').checked;
for(const [id,pair] of circuitInputs) {
pair.check.checked=all || chosen.has(id);
pair.check.disabled=all;
}
el('circuitSelection').value=[...chosen].join(',');
el('circuitNames').value=JSON.stringify(circuitNames);
let production=0,consumption=0,count=0,missing=false;
for(const row of circuitRows) {
if(all || chosen.has(String(row.id))) {
if(row.missing){missing=true;continue;}
production+=row.production_mw;consumption+=row.consumption_mw;count++;
}
}
el('selectionTotal').textContent=missing ? 'Selection incomplete : une installation est absente.' : `${count} installation(s) : production ${production.toFixed(1)} MW / consommation ${consumption.toFixed(1)} MW`;
}
async function refreshCircuits(){
if(circuitsBusy)return;circuitsBusy=true;
try {
const response=await fetch('/circuits',{cache:'no-store'});
if(!response.ok)throw Error('HTTP '+response.status);
const data=await response.json();
circuitRows=data.circuits;
const available=new Set(circuitRows.map(row=>String(row.id)));
for(const id of chosen) if(!available.has(id)) circuitRows.push({id:Number(id),missing:true});
for(const row of circuitRows){
const id=String(row.id);
let pair=circuitInputs.get(id);
if(!pair){
const box=document.createElement('div');box.className='installation';
const label=document.createElement('label'),check=document.createElement('input'),title=document.createElement('b');
check.type='checkbox';check.setAttribute('aria-label','Surveiller le circuit '+id);
check.addEventListener('change',()=>{if(check.checked)chosen.add(id);else chosen.delete(id);selectionChanged();});
label.append(check,title);
const metrics=document.createElement('small'),name=document.createElement('input');
name.type='text';name.maxLength=48;name.placeholder='Nom de cette installation';name.value=circuitNames[id] || '';
name.setAttribute('aria-label','Nom du circuit '+id);
name.addEventListener('input',()=>{if(name.value.trim())circuitNames[id]=name.value.trim();else delete circuitNames[id];title.textContent=(circuitNames[id] || 'Circuit '+id)+' (#'+id+')';selectionChanged();});
box.append(label,metrics,name);el('circuitList').append(box);
pair={box,check,title,metrics,name};circuitInputs.set(id,pair);
}
pair.title.textContent=(circuitNames[id] || 'Circuit '+id)+' (#'+id+')';
pair.metrics.textContent=row.missing ? 'Installation absente des donnees actuelles' : `Production ${row.production_mw.toFixed(1)} MW · consommation ${row.consumption_mw.toFixed(1)} MW · capacite ${row.capacity_mw.toFixed(1)} MW · batteries ${row.battery_percent.toFixed(0)} %${row.fuse_triggered?' · DISJONCTEUR':''}`;
}
for(const [id,pair] of circuitInputs) if(!circuitRows.some(row=>String(row.id)===id)){pair.box.remove();circuitInputs.delete(id);}
el('circuitStatus').textContent=data.debug ? 'Valeurs simulees DEBUG : les curseurs representent le total de toutes les installations.' : data.fresh ? 'Donnees en direct' : 'Dernieres valeurs connues : donnees indisponibles ou perimees.';
selectionChanged();
}catch(e){el('circuitStatus').textContent='Impossible de charger les installations : '+e.message;}
finally{circuitsBusy=false;}
}
document.querySelector('form[action="/save"]').addEventListener('invalid',event=>{const panel=event.target.closest('[role=tabpanel]');if(panel)openTab(panel.id.substring(6));},true);
document.querySelector('form[action="/save"]').addEventListener('submit',event=>{
selectionChanged();
if(!el('pageEnergy').checked && !el('pageCoupons').checked){event.preventDefault();el('feedback').textContent='Cochez au moins une page : Energie ou Bons FICSIT.';}
});
show();refreshCircuits();setInterval(refreshCircuits,3000);</script></main></body></html>)HTML";
  h.replace("%ENERGYCHECK%",energyPageEnabled ? "checked":"");
  h.replace("%COUPONCHECK%",couponPageEnabled ? "checked":"");
  h.replace("%ENERGYSECONDS%",String(energyPageSeconds));
  h.replace("%COUPONSECONDS%",String(couponPageSeconds));
  h.replace("%AUTOCHECK%",autoVehicle?"checked":"");
  h.replace("%PLAYER%",htmlEscape(playerName));
  h.replace("%DCONNECTING%",debugConnecting ? "checked":"");
  h.replace("%DCOUPONS%",String(debugCoupons));
  h.replace("%DWORLD%",String(debugCouponWorldTotal));
  h.replace("%DWORLDKNOWN%",debugCouponWorldKnown ? "checked":"");
  h.replace("%DPROGRESS%",String(debugProgress,0));
  h.replace("%DFUEL%",String(debugFuel,0));
  h.replace("%DVEHICLENAME%",htmlEscape(debugVehicleName));
  h.replace("%DFUELNAME%",htmlEscape(debugVehicleFuelName));
  h.replace("%COUPONFLASHCHECK%",couponFlashEnabled ? "checked":"");
  h.replace("%COUPONCOLOR%",colorHex(couponRingColor));
  h.replace("%COUPONFLASHCOLOR%",colorHex(couponFlashColor));
  h.replace("%COUPONFLASHBRIGHT%",String(couponFlashBrightness));
  h.replace("%DCOUPONKNOWN%",debugCouponsValid?"checked":"");
  h.replace("%DVEHICLE%",debugVehicle?"checked":"");
  h.replace("%DFUELKNOWN%",debugFuelKnown?"checked":"");
  h.replace("%URL%", htmlEscape(exportURL));
  h.replace("%USBCHECK%", usbMode ? "selected" : "");
  h.replace("%WIFICHECK%", usbMode ? "" : "selected");
  String ids=customCircuits ? circuitSelection : (selectedCircuit>=0 ? String(selectedCircuit) : "");
  String names; serializeJson(circuitNames,names);
  h.replace("%SELECTION%",htmlEscape(ids));
  h.replace("%NAMES%",htmlEscape(names));
  h.replace("%ALLCHECK%",!customCircuits && selectedCircuit==-1 ? "checked":"");
  h.replace("%BRIGHT%", String(brightness));
  h.replace("%FLASHBRIGHT%", String(flashBrightness));
  h.replace("%RINGCHECK%", ringEnabled ? "checked" : "");
  h.replace("%SCREENCHECK%", screenEnabled ? "checked" : "");
  h.replace("%CONTRAST%", String(oledContrast));
  String state = "{debug:" + String(debugMode ? "true":"false") + ",cut:" + String(debugCut ? "true":"false") +
    ",c:" + String(debugConsumed,0) + ",p:" + String(debugProduced,0) + ",b:" + String(debugBatteryPct,0) +
    ",o:" + String(debugBatteryOut,0) + ",hh:" + String(debugHours) + ",mm:" + String(debugMinutes) +
    ",fuse:" + String(debugFuse ? "true":"false") + "}";
  h.replace("%STATE%",state);
  web.send(200,"text/html; charset=utf-8",h);
}
void savePage() {
  if(!authenticated()) return;
  String transport=web.hasArg("transport") ? web.arg("transport") : (usbMode ? "usb" : "wifi");
  if(transport!="usb" && transport!="wifi") {web.send(400,"text/plain","Connexion invalide");return;}
  String url=web.arg("url"); url.trim();
  int level=web.arg("brightness").toInt(), mode=web.hasArg("circuit") ? web.arg("circuit").toInt() : selectedCircuit;
  int flashLevel=web.arg("flashBrightness").toInt();
  int newContrast=web.arg("oledContrast").toInt();
  bool newRingEnabled=web.hasArg("ringOn");
  bool newScreenEnabled=web.hasArg("screenOn");
  String password=web.arg("password");
  if(!url.startsWith("https://") || url.length()>180 || level<1 || level>255 || flashLevel<1 || flashLevel>255 || newContrast<0 || newContrast>255 || mode< -1 || (password.length()>0 && password.length()<12)) {
    web.send(400,"text/plain","Parametres invalides"); return;
  }
  bool newEnergy=energyPageEnabled, newCoupons=couponPageEnabled;
  int newEnergySeconds=energyPageSeconds, newCouponSeconds=couponPageSeconds;
  if(web.arg("pagesVersion")=="1") {
    newEnergy=web.hasArg("pageEnergy"); newCoupons=web.hasArg("pageCoupons");
    newEnergySeconds=web.arg("energySeconds").toInt();
    newCouponSeconds=web.arg("couponSeconds").toInt();
    if((!newEnergy && !newCoupons) || newEnergySeconds<1 || newEnergySeconds>600 ||
       newCouponSeconds<1 || newCouponSeconds>600) {
      web.send(400,"text/plain","Cochez une page et choisissez des durees de 1 a 600 secondes");return;
    }
  } else if(web.hasArg("displayMode")) {
    int legacy=web.arg("displayMode").toInt();
    if(legacy<0 || legacy>1) {web.send(400,"text/plain","Mode invalide");return;}
    newEnergy=legacy==0;newCoupons=legacy==1;
  }
  String newPlayer=web.arg("playerName"); newPlayer.trim();
  if(newPlayer.length()>64 ||
     (web.hasArg("autoVehicle") && newPlayer.isEmpty())) {
    web.send(400,"text/plain","Mode ou pseudo invalide"); return;
  }
  bool newCouponFlash=couponFlashEnabled;
  uint32_t newCouponColor=couponRingColor,newCouponFlashColor=couponFlashColor;
  int newCouponBrightness=couponFlashBrightness;
  if(web.arg("ledVersion")=="1"){
    newCouponFlash=web.hasArg("couponFlashOn");
    newCouponBrightness=web.arg("couponFlashBrightness").toInt();
    if(!parseRGBColor(web.arg("couponColor").c_str(),newCouponColor) ||
       !parseRGBColor(web.arg("couponFlashColor").c_str(),newCouponFlashColor) ||
       newCouponBrightness<1 || newCouponBrightness>255){
      web.send(400,"text/plain","Couleur ou luminosite invalide");return;
    }
  }
  bool newCustom=customCircuits;
  String newSelection=circuitSelection;
  JsonDocument newNames;
  bool newSelectionForm=web.arg("selectionVersion")=="1";
  if(newSelectionForm) {
    newCustom=!web.hasArg("watchAll");
    newSelection=web.arg("circuitSelection");
    int ids[MAX_CIRCUITS],count;
    String names=web.arg("circuitNames");
    if(newSelection.length()>704 || !parseCircuitIDs(newSelection,ids,count) || names.length()>6144 ||
       deserializeJson(newNames,names) || !newNames.is<JsonObject>() || newNames.size()>MAX_CIRCUITS) {
      web.send(400,"text/plain","Selection invalide"); return;
    }
    for(JsonPair pair:newNames.as<JsonObject>()) {
      int nameIDs[MAX_CIRCUITS],nameCount;
      if(!parseCircuitIDs(String(pair.key().c_str()),nameIDs,nameCount) || nameCount!=1 || !pair.value().is<const char*>() ||
         String(pair.value().as<const char*>()).length()>192) {
        web.send(400,"text/plain","Nom de circuit invalide"); return;
      }
    }
    customCircuits=newCustom; circuitSelection=newSelection;
    circuitNames.set(newNames); mode=-1;
  } else if(web.hasArg("circuit")) customCircuits=false;
  couponFlashEnabled=newCouponFlash;couponRingColor=newCouponColor;couponFlashColor=newCouponFlashColor;
  couponFlashBrightness=newCouponBrightness;couponEvents.reset();
  energyPageEnabled=newEnergy; couponPageEnabled=newCoupons;
  energyPageSeconds=newEnergySeconds; couponPageSeconds=newCouponSeconds;
  displaySchedule.reset(energyPageEnabled,millis());
  playerName=newPlayer; autoVehicle=web.hasArg("autoVehicle");
  resetExtraData();
  exportURL=url; selectedCircuit=mode; brightness=level; flashBrightness=flashLevel;
  ringEnabled=newRingEnabled; screenEnabled=newScreenEnabled; oledContrast=newContrast;
  if(password.length()) adminPassword=password;
  usbMode=transport=="usb";
  resetUSB();
  saveSettings();
  oled.setContrast((uint8_t)oledContrast);
  oled.setPowerSave(screenEnabled ? 0 : 1);
  if(debugMode) applyDebug(); else dataValid=false;
  oledDisplay();
  updateRing();
  lastPoll=millis()-UPDATE_MS;
  web.sendHeader("Location","/");web.send(303,"text/plain","OK");
}
void circuitsPage() {
  if(!authenticated()) return;
  JsonDocument response;
  response["debug"]=debugMode;
  response["fresh"]=debugMode || (catalogFresh && millis()-catalogReceivedAt<=MAX_AGE_SEC*1000UL);
  JsonDocument simulated;
  if(debugMode) buildDebugCircuits(simulated);
  JsonArray rows=debugMode ? simulated.as<JsonArray>() : circuitCatalog.as<JsonArray>();
  JsonArray output=response["circuits"].to<JsonArray>();
  for(JsonObject row:rows) {
    JsonObject item=output.add<JsonObject>();
    item["id"]=row["id"];
    item["production_mw"]=row["production_mw"]|0.0f;
    item["consumption_mw"]=row["consumption_mw"]|0.0f;
    item["capacity_mw"]=row["capacity_mw"]|0.0f;
    item["battery_percent"]=row["battery_percent"]|0.0f;
    item["fuse_triggered"]=row["fuse_triggered"]|false;
  }
  String json; serializeJson(response,json);
  web.sendHeader("Cache-Control","no-store");
  web.send(200,"application/json; charset=utf-8",json);
}
void setDebugPreview() {
  String page=web.arg("page");
  debugPreviewPage=page=="energy" ? 0 : page=="coupons" ? 1 : page=="vehicle" ? 2 : -1;
  displaySchedule.reset(energyPageEnabled,millis());
}
void debugViewPage() {
  if(!authenticated()) return;
  if(!debugMode){web.send(409,"text/plain","Mode ONLINE");return;}
  setDebugPreview();oledDisplay();updateRing();
  web.send(200,"text/plain","OK");
}
void modePage() {
  if(!authenticated()) return;
  resetExtraData();
  couponEvents.reset();
  debugMode=(web.arg("debug")=="1");
  setDebugPreview();
  if(!debugMode) debugPreviewPage=-1;
  if(debugMode) applyDebug();
  else {dataValid=false;statusMessage="Reconnexion FRM";lastPoll=millis()-UPDATE_MS;}
  checkFuseTransition(); oledDisplay(); updateRing();
  web.send(200,"text/plain","OK");
}
void debugPage() {
  if(!authenticated()) return;
  if(!debugMode){web.send(409,"text/plain","Mode ONLINE");return;}
  debugCoupons=constrain(web.arg("coupons").toInt(),0L,1000000L);
  debugCouponWorldTotal=constrain(web.arg("couponWorld").toInt(),0L,1000000L);
  debugCouponWorldKnown=web.arg("couponWorldKnown")=="1";
  debugProgress=constrain(web.arg("progress").toFloat(),0.0f,100.0f);
  debugCouponsValid=web.arg("couponKnown")=="1";
  debugVehicle=web.arg("vehicle")=="1";
  debugFuel=constrain(web.arg("fuelAmount").toFloat(),0.0f,1000000.0f);
  debugFuelKnown=web.arg("fuelKnown")=="1";
  if(web.hasArg("vehicleName") && web.arg("vehicleName").length()<=192)debugVehicleName=web.arg("vehicleName");
  if(web.hasArg("fuelName") && web.arg("fuelName").length()<=192)debugVehicleFuelName=web.arg("fuelName");
  debugCut=web.arg("cut")=="1";
  debugConsumed=constrain(web.arg("c").toFloat(),0.0f,30000.0f);
  debugProduced=constrain(web.arg("p").toFloat(),0.0f,30000.0f);
  debugBatteryPct=constrain(web.arg("b").toFloat(),0.0f,100.0f);
  debugBatteryOut=constrain(web.arg("o").toFloat(),0.0f,10000.0f);
  debugHours=constrain(web.arg("hh").toInt(),0,99);
  debugMinutes=constrain(web.arg("mm").toInt(),0,59);
  debugFuse=web.arg("fuse")=="1";
  debugConnecting=web.arg("connecting")=="1";
  applyDebug();checkFuseTransition();oledDisplay();updateRing();
  web.send(200,"text/plain","OK");
}
void oledContrastPage() {
  if(!authenticated()) return;
  oledContrast=constrain(web.arg("value").toInt(),0,255);
  oled.setContrast((uint8_t)oledContrast);
  // Apercu temporaire ; sauvegarde via Enregistrer.
  web.send(200,"text/plain","OK");
}
void brightnessPage() {
  if(!authenticated())return;
  brightness=constrain(web.arg("value").toInt(),1,255);
  // Apercu seulement; enregistrer avec le formulaire principal.
  updateRing();
  web.send(200,"text/plain","OK");
}
bool fresh(const char* iso) {
  if (!iso || strlen(iso) < 20) return false;
  int y,mo,d,h,mi,s;
  if (sscanf(iso, "%d-%d-%dT%d:%d:%dZ", &y,&mo,&d,&h,&mi,&s) != 6) return false;
  struct tm t = {};
  t.tm_year=y-1900; t.tm_mon=mo-1; t.tm_mday=d; t.tm_hour=h; t.tm_min=mi; t.tm_sec=s;
  time_t parsed = mktime(&t);  // configTime(0,0,...) = UTC
  time_t now=time(nullptr);
  return now > 1700000000 && difftime(now, parsed) >= -10 && difftime(now, parsed) <= MAX_AGE_SEC;
}
String durationFromHours(float hours) {
  if (!isfinite(hours) || hours < 0) return "--:--:--";
  unsigned long seconds = (unsigned long)min(hours * 3600.0f, 3599999.0f);
  char txt[20]; snprintf(txt, sizeof(txt), "%02lu:%02lu:%02lu", seconds/3600, (seconds/60)%60, seconds%60);
  return String(txt);
}
void collect() {
  resetExtraData();
  catalogFresh=false;
  if (WiFi.status()!=WL_CONNECTED) { dataValid=false;statusMessage="WiFi absent";return; }
  if (time(nullptr)<1700000000) { dataValid=false;statusMessage="Attente NTP";return; }
  WiFiClientSecure client; client.setCACert(ROOT_CA);
  HTTPClient http; http.setConnectTimeout(3500); http.setTimeout((couponPageEnabled || couponFlashEnabled || autoVehicle) ? 9000 : 4500);
  String requestURL=exportURL;
  if(couponPageEnabled || couponFlashEnabled || autoVehicle) {
    requestURL += exportURL.indexOf('?')>=0 ? "&monitor=1" : "?monitor=1";
    if(autoVehicle) requestURL += "&player=" + urlEncode(playerName);
  }
  if (!http.begin(client, requestURL)) { dataValid=false;statusMessage="HTTPS indisponible";return; }
  http.addHeader("Accept", "application/json");
  int status=http.GET();
  if (status!=200) { dataValid=false;statusMessage="Erreur HTTP " + String(status);http.end();return; }
  int length=http.getSize();
  if(length>32768) {dataValid=false;statusMessage="JSON trop grand";http.end();return;}
  JsonDocument doc;
  DeserializationError err=deserializeJson(doc,http.getStream(),DeserializationOption::NestingLimit(8));
  http.end();
  if(err || !doc.is<JsonObject>()) {dataValid=false;statusMessage="JSON invalide";return;}
  if((doc["version"]|0)!=1 || !(doc["online"]|false)) {dataValid=false;statusMessage="Serveur hors ligne";return;}
  if(!fresh(doc["updated_at"]|"")) {dataValid=false;statusMessage="Donnees perimees";return;}
  JsonObject extra=doc["monitor"].as<JsonObject>();
  if(!extra.isNull() && fresh(extra["updated_at"]|"")) {
    JsonObject sink=extra["coupons"].as<JsonObject>();
    if(sink["available"].is<unsigned long>() && sink["progress_percent"].is<float>()) {
      float progress=sink["progress_percent"].as<float>();
      couponsValid=isfinite(progress) && progress>=0 && progress<=100;
      coupons=sink["available"].as<unsigned long>(); couponProgress=progress;
      couponWorldTotalValid=couponsValid && (sink["world_total_complete"]|false) && sink["world_total"].is<unsigned long>();
      if(couponWorldTotalValid) {
        couponWorldTotal=sink["world_total"].as<unsigned long>();
        couponWorldTotalValid=couponWorldTotal>=coupons;
      }
    }
    JsonObject v=extra["vehicle"].as<JsonObject>();
    vehicleActive=autoVehicle && (v["active"]|false);
    vehicleName=String(v["name"]|"VEHICULE");
    vehicleKind=String(v["kind"]|"");
    if(vehicleKind.isEmpty())vehicleKind=vehicleName;
    vehicleFuelName=String(v["fuel_name"]|"CARBURANT");
    fuelKnown=v["fuel_amount"].is<float>() && isfinite(v["fuel_amount"].as<float>()) && v["fuel_amount"].as<float>()>=0;
    fuelAmount=fuelKnown?v["fuel_amount"].as<float>():0;
    // FRM ne fournit pas l'energie restante du combustible en cours : pas d'autonomie inventee.
  }
  JsonArray circuits=doc["circuits"].as<JsonArray>();
  if(circuits.isNull()) {dataValid=false;statusMessage="Circuits absents";return;}
  if(circuits.size()>MAX_CIRCUITS) {dataValid=false;statusMessage="Trop de circuits";return;}
  circuitCatalog.set(circuits);
  catalogReceivedAt=millis();catalogFresh=true;
  applyCircuitTotals(circuits);
}
void applyCircuitTotals(JsonArray circuits) {
  float cons=0,prod=0,cap=0,maxCon=0,batEnergy=0,batStorage=0,batOut=0,batIn=0,batDif=0;
  bool blown=false;int count=0, lastID=-1;
  String singleTime="--:--:--";
  int batteryGroupCount=0;
  // Chaque CircuitGroupID de FRM est un groupe electrique distinct.
  for(JsonObject c:circuits) {
    int id=c["id"]|-999;
    if(!circuitChosen(id)) continue;
    float bCap=max(0.0f,(float)(c["battery_capacity_mwh"]|0.0f));
    float bPct=constrain((float)(c["battery_percent"]|0.0f),0.0f,100.0f);
    batStorage+=bCap; batEnergy+=bCap*bPct/100.0f;
    if(bCap>0.0f) batteryGroupCount++;
    cons+=c["consumption_mw"]|0.0f; prod+=c["production_mw"]|0.0f;
    cap+=c["capacity_mw"]|0.0f; maxCon+=c["max_consumption_mw"]|0.0f;
    batOut+=c["battery_output_mw"]|0.0f; batIn+=c["battery_input_mw"]|0.0f;
    batDif+=c["battery_differential_mw"]|0.0f;
    blown=blown || (bool)(c["fuse_triggered"]|false);
    if (bCap>0.0f) singleTime=String((const char*)(c["battery_time_empty"]|"--:--:--"));
    lastID=id; count++;
  }
  int selectedIDs[MAX_CIRCUITS],expected=0;
  if(customCircuits) parseCircuitIDs(circuitSelection,selectedIDs,expected);
  if((customCircuits && count<expected) || (!customCircuits && count==0)) {
    dataValid=false; statusMessage="Circuit absent"; return;
  }
  consumed=cons;produced=prod;capacity=cap;maxConsumed=maxCon;
  batteryCap=batStorage;batteryPct=batStorage>0?100.0f*batEnergy/batStorage:0;
  batteryOut=batOut;batteryIn=batIn;batteryDiff=batDif;
  fuseTriggered=blown;currentCircuit=count==1 ? lastID : -1;
  batteryMode=(batOut>0.01f || batDif < -0.01f) && batStorage>0;
  // Mode global : autonomie theorique de l'ensemble des batteries, sans additionner les durées.
  float discharge=max(batOut,-batDif);
  batteryTime=(batteryGroupCount>1 && discharge>0.01f)
    ?durationFromHours(batEnergy/discharge):singleTime;
  if(!batteryMode && count!=1) batteryTime="--:--:--";
  dataValid=true;statusMessage=count==0 ? "Aucun circuit choisi" : "Connecte";
}
// Une seule animation au front montant du disjoncteur.
void checkFuseTransition() {
  bool current = dataValid && fuseTriggered;
  if (current && !lastFuseState) {
    flashRunning = true;
    flashStartedAt = millis();
  }
  lastFuseState = current;
}

void drawConnectionPulse() {
  ring.clear();
  if(!ringEnabled) {
    ring.setBrightness(0);ring.show();return;
  }
  if(!connectionPulseActive) {
    connectionPulseActive=true;
    connectionPulseStartedAt=millis();
  }
  ring.setBrightness(connectionPulseLevel((uint32_t)(millis()-connectionPulseStartedAt),(uint8_t)brightness));
  for(int i=0;i<LED_COUNT;i++) ring.setPixelColor(i,ring.Color(255,100,0));
  ring.show();
}
void connectionPulseTask(void*) {
  while(startupConnectionPulse) {
    drawConnectionPulse();
    vTaskDelay(pdMS_TO_TICKS(RING_REFRESH_MS));
  }
  // Hand ownership of the NeoPixel ring back to setup/loop before self-deletion.
  connectionPulseTaskHandle=nullptr;
  vTaskDelete(nullptr);
}
void startConnectionPulse() {
  startupConnectionPulse=true;
  connectionPulseActive=false;
  TaskHandle_t handle=nullptr;
  if(xTaskCreate(connectionPulseTask,"wifi-ring",2048,nullptr,1,&handle)==pdPASS)
    connectionPulseTaskHandle=handle;
  else {
    startupConnectionPulse=false;
    ring.clear();ring.setBrightness(ringEnabled ? brightness : 0);
    for(int i=0;i<LED_COUNT;i++) ring.setPixelColor(i,ring.Color(255,100,0));
    ring.show();
  }
}
void stopConnectionPulse() {
  startupConnectionPulse=false;
  // Never delete a task while ring.show() may still own the ESP32 RMT driver.
  while(connectionPulseTaskHandle!=nullptr) delay(2);
  connectionPulseActive=false;
  ring.clear();ring.setBrightness(ringEnabled ? brightness : 0);ring.show();
}
void updateRing() {
  ring.clear();
  if (!ringEnabled) {
    ring.setBrightness(0);
    ring.show();
    return;
  }
  const unsigned long now = millis();
  const bool connecting=debugMode ? debugConnecting : (!usbMode && WiFi.status()!=WL_CONNECTED);
  if(!connecting) connectionPulseActive=false;

  if (flashRunning && now - flashStartedAt >= FLASH_DURATION_MS) {
    flashRunning = false;
  }

  bool couponFlashLit=couponEvents.lit((uint32_t)now,flashRunning || connecting || !ringEnabled);
  if (flashRunning) {
    // Orage : trois impulsions tres courtes, blanc sur les 12 LED.
    // Aucune boucle bloquante ; le portail continue de repondre.
    unsigned long t = now - flashStartedAt;
    bool white = (t < 80) || (t >= 170 && t < 240) ||
                 (t >= 340 && t < 490) || (t >= 750 && t < 800);
    ring.setBrightness(flashBrightness);
    if (white) {
      for (int i=0; i<LED_COUNT; ++i) {
        ring.setPixelColor(i, ring.Color(255,255,255));
      }
    }
  } else {
    // Retour exactement a la luminosite initiale apres l'orage.
    ring.setBrightness(brightness);
    if(connecting) {drawConnectionPulse();return;}
    if(couponFlashLit){
      ring.setBrightness(couponFlashBrightness);
      for(int i=0;i<LED_COUNT;i++)ring.setPixelColor(i,couponFlashColor);
      ring.show();return;
    }
    if (!dataValid) {
      ring.setPixelColor(0, ring.Color(255,90,0));
    } else if (showingVehicle()) {
      // Meme fraction que la jauge OLED, sur un stack de 100 unites.
      if(!fuelKnown) ring.setPixelColor(0,ring.Color(255,100,0));
      else {
        int lit=constrain((int)ceilf(vehicleFuelFraction()*LED_COUNT),0,LED_COUNT);
        for(int i=0;i<lit;i++) ring.setPixelColor(i,ring.Color(0,65,255));
      }
    } else if (showingCoupons()) {
      if(!couponsValid) ring.setPixelColor(0,ring.Color(255,90,0));
      else {
        float steps=couponProgress/100.0f*LED_COUNT;
        int full=(int)floorf(steps);
        for(int i=0;i<full;i++) ring.setPixelColor(i,couponRingColor);
        if(full<LED_COUNT) {
          float part=steps-full;
          ring.setPixelColor(full,ring.Color((uint8_t)(((couponRingColor>>16)&255)*part),(uint8_t)(((couponRingColor>>8)&255)*part),(uint8_t)((couponRingColor&255)*part)));
        }
      }
    } else if (batteryMode) {
      if (batteryPct <= 10.0f) {
        // Alerte batterie faible : tout l'anneau rouge clignotant.
        if ((now / 400) % 2 == 0) {
          for (int i=0;i<LED_COUNT;i++) ring.setPixelColor(i,ring.Color(255,0,0));
        }
      } else if (batteryPct <= 20.0f) {
        // Avertissement : jauge orange fixe, proportionnelle a la charge.
        int n=constrain((int)ceilf(batteryPct/100.0f*LED_COUNT),0,LED_COUNT);
        for(int i=0;i<n;i++) ring.setPixelColor(i,ring.Color(255,100,0));
      } else {
        // Batterie : bleu fixe, remplissage selon pourcentage restant.
        int n=constrain((int)ceilf(batteryPct/100.0f*LED_COUNT),0,LED_COUNT);
        for(int i=0;i<n;i++) ring.setPixelColor(i,ring.Color(0,65,255));
      }
    } else if (fuseTriggered) {
      // Disjoncteur sans sortie batterie : rouge clignotant.
      if ((now / 500) % 2 == 0) {
        for(int i=0;i<LED_COUNT;i++) ring.setPixelColor(i,ring.Color(255,0,0));
      }
    } else {
      // FRM PowerCapacity inclut le boost ; PowerProduction est la production de base.
      float fraction=capacity>0?consumed/capacity:(consumed>0?1.0f:0.0f);
      int n=constrain((int)ceilf(fraction*LED_COUNT),0,LED_COUNT);
      for(int i=0;i<n;i++) {
        float p=(float)(i+1)/LED_COUNT;
        uint8_t r=(uint8_t)constrain((int)(p<0.6f?255*p/0.6f:255),0,255);
        uint8_t g=(uint8_t)constrain((int)(p<0.6f?255:255*(1-p)/0.4f),0,255);
        ring.setPixelColor(i,ring.Color(r,g,0));
      }
    }
  }
  ring.show();
}

void filledBolt() {
  // Silhouette pleine, polygone simple non croise
  const int px[]={24,9,20,15,34,23,27};
  const int py[]={3,26,26,46,19,19,3};
  const int n=7;
  for(int y=3;y<=46;y++) {
    int xs[7],count=0;
    for(int i=0;i<n;i++) {
      int j=(i+1)%n;
      if((py[i]<=y && y<py[j])||(py[j]<=y && y<py[i]))
        xs[count++]=px[i]+(long)(y-py[i])*(px[j]-px[i])/(py[j]-py[i]);
    }
    for(int i=0;i<count;i++)for(int j=i+1;j<count;j++)if(xs[j]<xs[i]){int t=xs[i];xs[i]=xs[j];xs[j]=t;}
    for(int i=0;i+1<count;i+=2)oled.drawHLine(xs[i],y,xs[i+1]-xs[i]+1);
  }
}
// Transforme HH:MM:SS en HHhMM, par exemple 02H27.
// Conserve HH:MM:SS si fourni, sinon affiche --:--:--.
String fullBatteryTime(const String& raw) {
  if (raw.length() < 8 || raw.charAt(2) != ':' || raw.charAt(5) != ':')
    return "--:--:--";
  for (int i=0; i<8; i++) {
    if (i==2 || i==5) continue;
    if (!isDigit(raw.charAt(i))) return "--:--:--";
  }
  return raw.substring(0,8);
}

void smallBatteryIcon(int x, int y, int percent) {
  oled.drawFrame(x,y,13,8);
  oled.drawBox(x+13,y+2,2,4);
  int fill=constrain((int)roundf(percent/100.0f*9.0f),0,9);
  if(fill>0) oled.drawBox(x+2,y+2,fill,4);
}

// Batterie quasiment pleine hauteur : corps 29x56 pixels.
void bigBatteryIcon(int x, int y, int percent) {
  oled.drawRFrame(x, y+4, 29, 56, 3);
  oled.drawBox(x+9,y,11,4);
  int fill = constrain((int)roundf(percent/100.0f*50.0f),0,50);
  if(fill>0) oled.drawBox(x+4,y+8+(50-fill),21,fill);
}

// 16 barrettes dans un conteneur arrondi, zone droite de la batterie.
void batterySegmentGauge(int x, int y, int width, int percent) {
  oled.drawRFrame(x,y,width,12,3);
  const int count=16;
  const int gap=1;
  const int inner=width-6;
  const int segmentWidth=(inner-(count-1)*gap)/count;
  const int active=constrain((int)ceilf(percent/100.0f*count),0,count);
  for (int i=0;i<active;i++)
    oled.drawBox(x+3+i*(segmentWidth+gap),y+3,segmentWidth,6);
}

void couponIcon() {
  // Ticket occupies the same left-hand area and height as the original lightning bolt.
  oled.drawRBox(6,3,28,44,3);
  oled.setDrawColor(0);
  oled.drawDisc(6,25,3); oled.drawDisc(33,25,3);
  oled.drawRFrame(9,6,22,38,1);
  // FICSIT F and perforation, cut into the white ticket silhouette.
  oled.drawBox(13,12,4,18); oled.drawBox(13,12,14,4); oled.drawBox(13,20,11,4);
  for(int x=11;x<30;x+=4) oled.drawHLine(x,35,2);
  oled.setDrawColor(1);
}
void couponDisplay() {
  couponIcon();
  oled.setFont(u8g2_font_helvB24_tn);
  String number=couponsValid ? String(coupons) : "--";
  if(oled.getStrWidth(number.c_str())>91) oled.setFont(u8g2_font_helvB18_tn);
  if(oled.getStrWidth(number.c_str())>91) oled.setFont(u8g2_font_9x15B_tf);
  int topBaseline=oled.getAscent()+1;
  oled.drawStr(max(35,126-oled.getStrWidth(number.c_str())),topBaseline,number.c_str());
  String map="MAP " + (couponWorldTotalValid ? String(couponWorldTotal) : String("--"));
  oled.setFont(u8g2_font_7x13B_tf);
  if(oled.getStrWidth(map.c_str())>91) oled.setFont(u8g2_font_5x7_tf);
  oled.drawStr(max(35,126-oled.getStrWidth(map.c_str())),41,map.c_str());
  oled.setFont(u8g2_font_5x7_tf);
  String progress=couponsValid ? "PROCHAIN " + String(couponProgress,1) + "%" : "PROCHAIN --";
  oled.drawStr(max(35,126-oled.getStrWidth(progress.c_str())),50,progress.c_str());
  oled.drawRFrame(2,53,124,11,3);
  int filled=couponsValid ? constrain((int)ceilf(couponProgress/100.0f*20.0f),0,20) : 0;
  for(int i=0;i<filled;i++) oled.drawBox(5+i*6,56,4,5);
}
String removeLastUTF8(String text){
  int end=text.length()-1;
  while(end>0 && (((uint8_t)text[end]&0xc0)==0x80))end--;
  return end>=0 ? text.substring(0,end) : String("");
}
void drawVehicleText(String text,int baseline,const uint8_t* font){
  oled.setFont(font);
  if(oled.getUTF8Width(text.c_str())>91)oled.setFont(u8g2_font_5x7_tf);
  bool clipped=oled.getUTF8Width(text.c_str())>91;
  if(clipped){
    while(text.length() && oled.getUTF8Width((text+"...").c_str())>91)text=removeLastUTF8(text);
    text+="...";
  }
  oled.drawUTF8(max(35,126-oled.getUTF8Width(text.c_str())),baseline,text.c_str());
}
void vehicleIcon(){
  // Jerrican au trait, adapte au modele fourni et a la grille OLED.
  oled.drawRFrame(4,14,29,36,5);
  oled.drawRFrame(5,15,27,34,4);
  // Epaule inclinee : decoupe le coin puis dessine son contour.
  oled.setDrawColor(0);oled.drawBox(23,14,11,10);oled.setDrawColor(1);
  oled.drawLine(22,14,32,24);oled.drawLine(22,15,31,24);
  // Poignee.
  oled.drawRFrame(10,7,13,9,2);oled.drawRFrame(11,8,11,7,1);
  // Col et bouchon inclines.
  oled.drawLine(25,16,27,13);oled.drawLine(28,19,30,16);
  oled.drawLine(23,9,27,5);oled.drawLine(27,5,34,12);
  oled.drawLine(34,12,30,16);oled.drawLine(30,16,23,9);
  oled.drawLine(24,9,27,6);oled.drawLine(27,6,33,12);
  oled.drawLine(33,12,30,15);oled.drawLine(30,15,24,9);
  // Renfort central et quatre branches.
  oled.drawFrame(12,25,13,16);
  oled.drawLine(9,22,12,25);oled.drawLine(24,25,27,22);
  oled.drawLine(9,44,12,40);oled.drawLine(24,40,27,44);
}
void vehicleDisplay(){
  vehicleIcon();
  oled.setFont(u8g2_font_logisoso38_tn);
  String number=fuelKnown ? String(fuelAmount,0) : "--";
  if(oled.getStrWidth(number.c_str())>91)oled.setFont(u8g2_font_helvB24_tn);
  if(oled.getStrWidth(number.c_str())>91)oled.setFont(u8g2_font_helvB18_tn);
  if(oled.getStrWidth(number.c_str())>91)oled.setFont(u8g2_font_9x15B_tf);
  oled.drawStr(max(35,126-oled.getStrWidth(number.c_str())),39,number.c_str());
  drawVehicleText(vehicleFuelName,50,u8g2_font_7x13B_tf);
  oled.drawRFrame(2,53,124,11,3);
  float fraction=vehicleFuelFraction();
  int filled=constrain((int)ceilf(fraction*20.0f),0,20);
  for(int i=0;i<filled;i++)oled.drawBox(5+i*6,56,4,5);
}
void oledDisplay() {
  oled.clearBuffer();
  if(!dataValid) {
    oled.setFont(u8g2_font_6x10_tf);
    oled.drawStr(0,12,"FICSIT MONITOR");
    oled.drawStr(0,30,"DONNEES INDISPONIBLES");
    oled.drawStr(0,45,statusMessage.substring(0,21).c_str());
    oled.drawStr(0,60,usbMode ? "USB COM / 115200" : WiFi.localIP().toString().c_str());
    oled.sendBuffer(); return;
  }
  if(showingVehicle()) {vehicleDisplay();oled.sendBuffer();return;}
  if(showingCoupons()) {
    couponDisplay(); oled.sendBuffer(); return;
  }
  if(batteryMode) {
    bigBatteryIcon(2,2,(int)roundf(batteryPct));
    String remaining=fullBatteryTime(batteryTime);
    // Police plus lisible : la plus grande qui rentre dans les 92 pixels libres.
    oled.setFont(u8g2_font_helvB18_tn);
    if(oled.getStrWidth(remaining.c_str())>92)
      oled.setFont(u8g2_font_9x15B_tf);
    if(oled.getStrWidth(remaining.c_str())>92)
      oled.setFont(u8g2_font_7x13B_tf);
    int w=oled.getStrWidth(remaining.c_str());
    int x=35+max(0,(92-w)/2);
    oled.drawStr(x,34,remaining.c_str());
    batterySegmentGauge(35,45,92,(int)roundf(batteryPct));
  } else {
    // Eclair original conserve.
    filledBolt();
    // Deux lignes alignees sur le bord droit de l'ecran.
    oled.setFont(u8g2_font_helvB24_tn);
    String number=String((int)roundf(consumed));
    if(oled.getStrWidth(number.c_str())>91)
      oled.setFont(u8g2_font_helvB18_tn);
    if(oled.getStrWidth(number.c_str())>91)
      oled.setFont(u8g2_font_9x15B_tf);
    // Aligne a droite, aussi haut que la police le permet sans coupure.
    int topBaseline=oled.getAscent()+1;
    oled.drawStr(max(35,126-oled.getStrWidth(number.c_str())),topBaseline,number.c_str());
    oled.setFont(u8g2_font_7x13B_tf);
    String line="/ " + String((int)roundf(capacity)) + " MW";
    oled.drawStr(max(35,126-oled.getStrWidth(line.c_str())),41,line.c_str());
    // Capacite totale de batteries en MWh + pourcentage sur la meme ligne.
    smallBatteryIcon(38,43,(int)roundf(batteryPct));
    oled.setFont(u8g2_font_5x7_tf);
    String batteries=String((int)roundf(batteryCap)) + "MWh " +
                     String((int)roundf(batteryPct)) + "%";
    oled.drawStr(56,50,batteries.c_str());
    oled.drawRFrame(2,53,124,11,3);
    float ratio=capacity>0 ? consumed/capacity : (consumed>0?1.0f:0.0f);
    int filled=constrain((int)ceilf(ratio*20.0f),0,20);
    for(int i=0;i<filled;i++) oled.drawBox(5+i*6,56,4,5);
  }
  oled.sendBuffer();
}

#include "USBMonitor.h"

void setup() {
  loadSettings();
  Serial.setRxBufferSize(8192);
  Serial.begin(115200);
  Wire.begin(OLED_SDA,OLED_SCL);oled.setI2CAddress(0x3C<<1);oled.begin();
  oled.setContrast((uint8_t)oledContrast);
  oled.setPowerSave(screenEnabled ? 0 : 1);
  ring.begin();ring.clear();ring.setBrightness(brightness);ring.show();
  oledDisplay();
  WiFi.mode(WIFI_STA);
  WiFiManager wm;wm.setDebugOutput(false);wm.setConnectTimeout(15);
  startConnectionPulse();
  wm.autoConnect(AP_NAME,AP_PASSWORD);
  stopConnectionPulse();
  if(WiFi.status()==WL_CONNECTED) {
    if(MDNS.begin("monitor")) MDNS.addService("http","tcp",80);
    configTime(0,0,"pool.ntp.org","time.google.com");
  }
  web.on("/",HTTP_GET,page);web.on("/save",HTTP_POST,savePage);
  web.on("/debug-view",HTTP_POST,debugViewPage);web.on("/mode",HTTP_POST,modePage);web.on("/debug",HTTP_POST,debugPage);
  web.on("/circuits",HTTP_GET,circuitsPage);
  web.on("/brightness",HTTP_POST,brightnessPage);
  web.on("/oled-contrast",HTTP_POST,oledContrastPage);
  web.on("/favicon.svg",HTTP_GET,[]() {
    web.send(200,"image/svg+xml",R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32"><rect width="32" height="32" rx="4" fill="#14181c"/><path d="M16 4.5 26.5 10.5v11L16 27.5 5.5 21.5v-11Z" fill="none" stroke="#ff8a14" stroke-width="1.8"/><path d="M17.2 8.2 11.6 16h4.4L14.2 24l8.2-10.2h-4.6l2.4-5.6z" fill="#ff8a14"/></svg>)SVG");
  });
  web.begin();
  lastPoll=millis()-UPDATE_MS;
}
void loop() {
  web.handleClient();
  if(usbMode) serviceUSB();
  if(!usbMode && !debugMode && millis()-lastPoll>=UPDATE_MS) {
    lastPoll=millis();
    collect();
    checkFuseTransition();
    oledDisplay();
  }
  couponEvents.observe(couponFlashEnabled && ringEnabled,dataValid && couponsValid && !(debugMode && debugConnecting),coupons);
  if(!(debugMode && debugPreviewPage>=0) && displaySchedule.update(energyPageEnabled,couponPageEnabled,
      (uint32_t)energyPageSeconds*1000UL,(uint32_t)couponPageSeconds*1000UL,
      vehicleActive,dataValid,millis())) {
    oledDisplay();updateRing();
  }
  if(millis()-lastRing>=RING_REFRESH_MS) {
    lastRing=millis();
    updateRing();
  }
  delay(2);
}
