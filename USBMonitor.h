#pragma once
#include "SerialJSONFrame.h"

// FRM serial protocol: one endpoint per request, JSON array in response.
// No diagnostic output may be written to this port.
static const char* usbEndpoints[]={"getPower","getResourceSink","getCloudInv",
  "getStorageInv","getPlayer","getCrateInv","getVehicles"};
static char usbBuffer[131072];
static SerialJSONFrame usbFrame;
static int usbStep=0;
static bool usbWaiting=false, usbCycle=false, usbWorldOK=false;
static unsigned long usbSentAt=0, usbLastByte=0, usbPowerAt=0, usbCycleAt=0;
static uint64_t usbWorld=0;

void resetUSB() {
  usbWaiting=false;usbCycle=false;usbStep=0;usbFrame.reset();
  usbPowerAt=0;usbCycleAt=millis()-UPDATE_MS;usbLastByte=millis();
  dataValid=false;catalogFresh=false;resetExtraData();couponEvents.reset();
  statusMessage=usbMode ? "Attente USB FRM" : "Attente reseau";
}
static bool usbNumber(JsonVariantConst value, double& number) {
  if(!value.is<double>() && !value.is<const char*>()) return false;
  if(value.is<const char*>()) {
    const char* text=value.as<const char*>();char* end=nullptr;
    number=strtod(text,&end);
    if(end==text || *end) return false;
  } else number=value.as<double>();
  return isfinite(number);
}
static float usbFloat(JsonVariantConst value) {
  double n;return usbNumber(value,n) ? (float)n : 0.0f;
}
static bool usbBool(JsonVariantConst value) {
  if(value.is<bool>()) return value.as<bool>();
  if(value.is<const char*>()) {
    String s=value.as<const char*>();s.toLowerCase();
    return s=="true" || s=="1" || s=="yes";
  }
  return usbFloat(value)!=0;
}
static bool usbInventory(JsonArrayConst slots, uint64_t& total) {
  if(slots.isNull()) return false;
  for(JsonVariantConst slot:slots) {
    if(!slot.is<JsonObjectConst>()) return false;
    String kind=slot["ClassName"] | (slot["className"] | "");
    if(kind!="Desc_ResourceSinkCoupon_C") continue;
    double amount;
    JsonVariantConst v=slot["Amount"].isNull() ? slot["amount"] : slot["Amount"];
    if(!usbNumber(v,amount) || amount<0 || amount>UINT32_MAX || floor(amount)!=amount) return false;
    total+=(uint64_t)amount;
    if(total>UINT32_MAX) return false;
  }
  return true;
}
static bool usbPower(JsonArrayConst rows) {
  if(rows.isNull() || rows.size()==0 || rows.size()>MAX_CIRCUITS) return false;
  JsonDocument converted;JsonArray circuits=converted.to<JsonArray>();
  for(JsonObjectConst row:rows) {
    double id;
    if(!usbNumber(row["CircuitGroupID"],id) || id<0 || id>2147483647 || floor(id)!=id) return false;
    for(JsonObject c:circuits) if(c["id"].as<int>()==(int)id) return false;
    double used,cap;
    if(!usbNumber(row["PowerConsumed"],used) || !usbNumber(row["PowerCapacity"],cap)) return false;
    JsonObject c=circuits.add<JsonObject>();c["id"]=(int)id;
    c["consumption_mw"]=used;c["capacity_mw"]=cap;
    c["production_mw"]=usbFloat(row["PowerProduction"]);
    c["max_consumption_mw"]=usbFloat(row["PowerMaxConsumed"]);
    c["battery_percent"]=usbFloat(row["BatteryPercent"]);
    c["battery_capacity_mwh"]=usbFloat(row["BatteryCapacity"]);
    c["battery_input_mw"]=usbFloat(row["BatteryInput"]);
    c["battery_output_mw"]=usbFloat(row["BatteryOutput"]);
    c["battery_differential_mw"]=usbFloat(row["BatteryDifferential"]);
    // Copy strings: the zero-copy input buffer is reused for the next endpoint.
    c["battery_time_empty"]=String(row["BatteryTimeEmpty"] | "--:--:--");
    c["battery_time_full"]=String(row["BatteryTimeFull"] | "--:--:--");
    c["fuse_triggered"]=usbBool(row["FuseTriggered"]);
  }
  circuitCatalog.set(circuits);catalogReceivedAt=millis();catalogFresh=true;
  applyCircuitTotals(circuits);usbPowerAt=millis();checkFuseTransition();oledDisplay();
  return true;
}
static bool usbResponse(JsonArrayConst rows) {
  if(rows.isNull()) return false;
  if(usbStep==0) return usbPower(rows);
  if(usbStep==1) {
    bool found=false;unsigned long balance=0;float progress=0;
    for(JsonObjectConst row:rows) {
      double n,p;
      if(!usbNumber(row["NumCoupon"],n) || n<0 || n>UINT32_MAX || floor(n)!=n ||
         !usbNumber(row["Percent"],p) || p<0 || p>1) return false;
      if(!found) progress=p*100;
      balance=max(balance,(unsigned long)n);found=true;
    }
    couponsValid=found;coupons=balance;couponProgress=progress;
    usbWorld=balance;usbWorldOK=found;couponWorldTotalValid=false;
    return true;
  }
  if(usbStep>=2 && usbStep<=5) {
    bool ok=true;
    if(usbStep==2) ok=usbInventory(rows,usbWorld);
    else for(JsonVariantConst row:rows) {
      JsonArrayConst inv=row["Inventory"].isNull() ? row["inventory"].as<JsonArrayConst>() : row["Inventory"].as<JsonArrayConst>();
      if(!usbInventory(inv,usbWorld)) {ok=false;break;}
    }
    usbWorldOK=usbWorldOK && ok;
    if(usbStep==5) {
      couponWorldTotalValid=usbWorldOK;
      if(usbWorldOK) couponWorldTotal=(unsigned long)usbWorld;
    }
    return ok;
  }
  vehicleActive=false;fuelKnown=false;
  JsonObjectConst match;int matches=0;
  for(JsonObjectConst row:rows) {
    String driver=row["Driver"] | "";driver.trim();
    if(driver==playerName) {match=row;++matches;}
  }
  if(matches!=1) return true;
  vehicleActive=true;
  vehicleName=match["Name"] | (match["VehicleType"] | "VEHICULE");
  vehicleKind=match["VehicleType"] | (match["ClassName"] | vehicleName.c_str());
  JsonArrayConst fuel=match["FuelInventory"].isNull() ? match["Fuel"].as<JsonArrayConst>() : match["FuelInventory"].as<JsonArrayConst>();
  fuelKnown=!fuel.isNull();fuelAmount=0;vehicleFuelName="CARBURANT";
  String firstName;bool mixed=false;
  for(JsonVariantConst slot:fuel) {
    double amount;
    if(!usbNumber(slot["Amount"],amount) || amount<0) {fuelKnown=false;break;}
    fuelAmount+=amount;
    String name=slot["Name"] | "";
    if(amount>0 && name.length()) {
      if(firstName.isEmpty()) firstName=name;
      else if(firstName!=name) mixed=true;
    }
  }
  if(!isfinite(fuelAmount)) fuelKnown=false;
  if(firstName.length()) vehicleFuelName=mixed ? "CARBURANTS MIXTES" : firstName;
  return true;
}
static void usbAdvance() {
  usbWaiting=false;usbFrame.reset();
  do {++usbStep;} while(usbStep<7 &&
    ((usbStep>=1 && usbStep<=5 && !couponPageEnabled && !couponFlashEnabled) ||
     (usbStep==6 && (!autoVehicle || playerName.isEmpty()))));
  if(usbStep>=7) {usbCycle=false;oledDisplay();}
}
static void usbFailure(const char* message) {
  if(usbStep==0) {
    dataValid=false;catalogFresh=false;resetExtraData();statusMessage=message;oledDisplay();
    usbWaiting=false;usbCycle=false;usbFrame.reset();usbCycleAt=millis();return;
  }
  else if(usbStep<=5) {
    usbWorldOK=false;couponWorldTotalValid=false;
    if(usbStep==1) couponsValid=false;
  } else {vehicleActive=false;fuelKnown=false;}
  usbAdvance();
}
void serviceUSB() {
  if(debugMode) {
    usbCycle=false;usbWaiting=false;usbFrame.reset();usbPowerAt=0;
    for(int i=0;i<2048 && Serial.available();++i) Serial.read();
    return;
  }
  if(usbPowerAt && millis()-usbPowerAt>MAX_AGE_SEC*1000UL && dataValid) {
    dataValid=false;catalogFresh=false;resetExtraData();statusMessage="USB sans donnees";oledDisplay();
  }
  // Limit work per pass so WebServer and LED animations continue to run.
  for(int i=0;i<2048 && Serial.available();++i) {
    char c=(char)Serial.read();usbLastByte=millis();
    if(!usbWaiting) continue;
    if(usbFrame.push(c,usbBuffer,sizeof(usbBuffer))) {
      bool ok=false;
      if(!usbFrame.overflow) {
        JsonDocument doc;
        auto error=deserializeJson(doc,usbBuffer,DeserializationOption::NestingLimit(16));
        if(!error) ok=usbResponse(doc.as<JsonArrayConst>());
      }
      if(ok) usbAdvance(); else usbFailure("JSON USB invalide");
    }
  }
  // A large reply at 115200 baud may take several seconds. Also impose an
  // absolute deadline so a broken sender cannot hold a request forever.
  if(usbWaiting && (millis()-usbSentAt>20000UL || millis()-usbLastByte>4000UL)) {
    usbFailure("FRM USB absent");
    // A timed-out reply can arrive late: start again with a recognisable power
    // response instead of assigning that reply to the next optional endpoint.
    usbWaiting=false;usbCycle=false;usbCycleAt=millis();usbFrame.reset();
  }
  if(!usbCycle && millis()-usbCycleAt>=UPDATE_MS) {
    usbCycle=true;usbStep=0;usbCycleAt=millis();
  }
  // Drain any trailing/late bytes before the next request (FRM has no request IDs).
  if(usbCycle && !usbWaiting && !Serial.available() && millis()-usbLastByte>=200UL &&
     Serial.availableForWrite()>=32) {
    usbFrame.reset();Serial.println(usbEndpoints[usbStep]);
    usbSentAt=millis();usbLastByte=usbSentAt;usbWaiting=true;
  }
}
