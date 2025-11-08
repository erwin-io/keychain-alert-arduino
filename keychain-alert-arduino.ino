#include <AltSoftSerial.h>
AltSoftSerial modem;
#define DBG Serial

const int LED_PIN = 4;
const unsigned long STROBE_ON_MS  = 60;
const unsigned long STROBE_OFF_MS = 60;

bool alarmActive = false;
bool ledState = false;
unsigned long ledT0 = 0;

const int PWRKEY = 7;

// ====== EDIT THESE ======
// +63 + 10 digit
const char* RECIPIENTS[] = { 
};
const int NUM_RECIPIENTS = sizeof(RECIPIENTS)/sizeof(RECIPIENTS[0]);
const char* REF_CODE = "SKV-20251103-78";

String buildAlertMessage(const char* refCode);

void tickEmergencyStrobe() {
  if (!alarmActive) return;
  unsigned long now = millis();
  unsigned long interval = ledState ? STROBE_ON_MS : STROBE_OFF_MS;
  if (now - ledT0 >= interval) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    ledT0 = now;
  }
}

void startEmergencyStrobe() {
  alarmActive = true;
  ledState = false;
  ledT0 = millis();
  digitalWrite(LED_PIN, LOW);
  tickEmergencyStrobe();
}

void delaySmooth(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    tickEmergencyStrobe();
    delayMicroseconds(500);
  }
}

void powerOnPulse() {
  pinMode(PWRKEY, OUTPUT);
  digitalWrite(PWRKEY, LOW);
  delaySmooth(200);
  pinMode(PWRKEY, INPUT);
  delaySmooth(200);
}

String readAll(unsigned long ms) {
  String s; unsigned long t = millis();
  while (millis() - t < ms) {
    tickEmergencyStrobe();
    while (modem.available()) {
      s += (char)modem.read();
      t = millis();
      tickEmergencyStrobe();
    }
  }
  return s;
}

String cmd(const String& c, unsigned long to=1500, bool echo=true) {
  while (modem.available()) modem.read();
  modem.print(c); modem.print("\r");
  String r = readAll(to);
  if (echo) { DBG.print(">> "); DBG.println(c); DBG.print("<< "); DBG.println(r); }
  return r;
}

bool ok(const String& c, unsigned long to=1500) {
  return cmd(c, to).indexOf("OK") >= 0;
}

bool syncBaud() {
  long BAUDS[] = {115200, 9600, 19200};
  for (long b : BAUDS) {
    modem.begin(b);
    delaySmooth(80);
    for (int i=0; i<3; i++) {
      String r = cmd("AT", 400, false);
      if (r.indexOf("OK") >= 0) {
        DBG.print("** SYNC OK @ "); DBG.println(b);
        return true;
      }
      delaySmooth(60);
    }
  }
  return false;
}

bool ensureCPIN(unsigned long maxMs=20000) {
  unsigned long t0 = millis();
  while (millis() - t0 < maxMs) {
    tickEmergencyStrobe();
    String r = cmd("AT+CPIN?", 800);
    if (r.indexOf("+CPIN: READY") >= 0) return true;
    if (r.indexOf("SIM PIN") >= 0) {
      DBG.println("SIM requires PIN.");
      return false;
    }
    delaySmooth(200);
  }
  return false;
}

bool parseReg(const String& resp, const char* tag, int& statOut) {
  int p = resp.indexOf(tag);
  if (p < 0) return false;
  int comma = resp.indexOf(',', p);
  if (comma < 0 || comma+1 >= (int)resp.length()) return false;
  statOut = resp.substring(comma+1).toInt();
  return true;
}

bool waitRegistered(unsigned long maxMs=60000) {
  unsigned long t0 = millis();
  while (millis() - t0 < maxMs) {
    tickEmergencyStrobe();
    int stat;
    String r = cmd("AT+CEREG?", 900, false);
    if (parseReg(r, "+CEREG:", stat)) {
      DBG.print("CEREG="); DBG.println(stat);
      if (stat == 1 || stat == 5) return true;
    }
    delaySmooth(600);
  }
  DBG.println(cmd("AT+CEER", 1200));
  return false;
}

bool sendSMS_Text_FAST(const char* to, const char* text) {
  while (modem.available()) modem.read();
  modem.print("AT+CMGS=\""); modem.print(to); modem.print("\"\r");

  unsigned long t0 = millis();
  bool prompt = false;
  while (millis() - t0 < 3000) {
    tickEmergencyStrobe();
    if (modem.available() && modem.read() == '>') { prompt = true; break; }
  }
  if (!prompt) return false;

  modem.print(text);
  modem.write(26);

  String r = readAll(15000);
  return (r.indexOf("+CMGS:") >= 0 && r.indexOf("OK") >= 0);
}

String buildAlertMessage(const char* refCode) {
  String s = "SAFEKEY ALERT: Immediate assistance requested. You are listed as an emergency contact. Please check the safety of the child. Thank you. ";
  s += refCode;
  return s;
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  startEmergencyStrobe();

  DBG.begin(115200);
  delaySmooth(300);

  powerOnPulse();

  if (!syncBaud()) {
    DBG.println("NO SYNC");
    return;
  }

  ok("ATE0");
  ok("AT+CMEE=1");
  ok("AT+CMGF=1");
  ok("AT+CSCS=\"GSM\"");
  ok("AT+CSMS=1");
  ok("AT+IFC=0,0");
  ok("AT+CFUN=1");

  if (!ensureCPIN(15000)) return;
  ok("AT+COPS=0");
  if (!waitRegistered(40000)) return;

  String msg = buildAlertMessage(REF_CODE);

  for (int i = 0; i < NUM_RECIPIENTS; i++) {
    sendSMS_Text_FAST(RECIPIENTS[i], msg.c_str());
    delaySmooth(300);
  }

  DBG.println("DONE.");
}

void loop() {
  tickEmergencyStrobe();
}
