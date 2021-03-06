#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Bounce2.h>

/* HW history
 * 0.1 - Initial hw for testing
 * 1.0 - Device installed in white box housing. Can be mounted on the wall like domofon terminal
 * 1.1 - Added red reject button and red reject LED
 * 1.2 - Diode added
 */

/*
 * SW history
 * 0.1 - Initial code. Has only one feature: auto open the door after incoming call
 * 0.3 - Added MQTT over Wi-Fi support
 * 0.4 - State actions almost finished. MQTT part almost ready
 * 0.5 - Hangup detect delay added
 * 0.6 - Added support for new reject button and red reject led. Relay delays tuned
 * 0.7 - "Last will" message added. Ping request added
 * 1.0 - Recovery feature added. Device has been tested in real life for 1 year
 */

#define DEVICE_NAME "MQTT Domofon controller by Artem Pinchuk"
#define DEVICE_HW_VERSION "1.2"
#define DEVICE_SW_VERSION "1.0"

// ***** CONFIG *****
// Hardware configuration
#define LED_ON HIGH
#define LED_OFF LOW
#define RELAY_ON LOW
#define RELAY_OFF HIGH

const int PIN_BUTTON_GREEN = 5; //D1
const int PIN_BUTTON_RED = 0; //D3
const int PIN_LED_GREEN = 4; //D2
const int PIN_LED_RED = 2; //D4
const int PIN_LED_STATUS = 14; //D5
const int PIN_CALL_DETECT = 12; //D6
const int PIN_RELAY_ANSWER = 13; //D7
const int PIN_RELAY_DOOR_OPEN = 16; //D0

// Software configuration
const char* WIFI_SSID = "WIFI_SSID";
const char* WIFI_PASSWORD = "WIFI_PASSWORD";
const char* MQTT_SERVER_ADDR = "1.2.3.4";
const uint16_t MQTT_SERVER_PORT = 1883;
const char* MQTT_USER_NAME = "MQTT_USER_NAME";
const char* MQTT_USER_PASSWORD = "MQTT_USER_PASSWORD";
const char* MQTT_CLIENT_ID = "domofon";
const char* MQTT_TOPIC_IN = "domofon/in";
const char* MQTT_TOPIC_OUT = "domofon/out";

unsigned long CALL_HANGUP_DETECT_DELAY = 2000;
unsigned long CALL_OPEN_RECOVERY_WINDOW = 5000;
unsigned int CALL_OPEN_RECOVERY_MAX_ATTEMPTS = 3;
unsigned int RELAY_ANSWER_ON_TIME = 1200;
unsigned int RELAY_OPEN_ON_TIME = 600;

// High level protocol messages
const uint8_t MSG_OUT_READY = 'R';
const char*   MSG_OUT_LAST_WILL = "L"; //null terminated 'L'
const uint8_t MSG_OUT_CALL = 'C';
const uint8_t MSG_OUT_HANGUP = 'H';
const uint8_t MSG_OUT_OPENED_BY_BUTTON = 'B';
const uint8_t MSG_OUT_OPENED_BY_RECOVERY = 'V';
const uint8_t MSG_OUT_REJECTED_BY_BUTTON = 'J';
const uint8_t MSG_OUT_SUCCESS = 'S';
const uint8_t MSG_OUT_FAIL = 'F';
const uint8_t MSG_IN_OPEN = 'O';
const uint8_t MSG_IN_REJECT = 'N';
const uint8_t MSG_IN_PING = 'P';
// ***** END OF CONFIG *****

typedef enum {
  IDLE,
  CALL
} EState;

typedef enum {
  NO_ACTION,
  OPEN,
  OPEN_BY_BUTTON,
  OPEN_BY_RECOVERY,
  REJECT,
  REJECT_BY_BUTTON
} EAction;

EState state = IDLE;
EAction action = NO_ACTION;

Bounce debouncerBtnGreen = Bounce();
Bounce debouncerBtnRed = Bounce();
unsigned long lastCallDetectedTime = 0;
unsigned long lastOpenTime = 0;
unsigned int recoveryAttempts = 0;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

void gpioInit() {
  pinMode(PIN_BUTTON_GREEN, INPUT_PULLUP);
  pinMode(PIN_BUTTON_RED, INPUT_PULLUP);
  pinMode(PIN_CALL_DETECT, INPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_RELAY_ANSWER, OUTPUT);
  pinMode(PIN_RELAY_DOOR_OPEN, OUTPUT);

  digitalWrite(PIN_LED_STATUS, LED_OFF);
  digitalWrite(PIN_LED_GREEN, LED_OFF);
  digitalWrite(PIN_LED_RED, LED_OFF);
  digitalWrite(PIN_RELAY_ANSWER, RELAY_OFF);
  digitalWrite(PIN_RELAY_DOOR_OPEN, RELAY_OFF);
}

void ledBlink(int* pins, int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(pins[i], LED_ON);
  }
  delay(125);
  for (int i = 0; i < count; i++) {
    digitalWrite(pins[i], LED_OFF);
  }
}

void ledBlink(int pin) {
  ledBlink(&pin, 1);
}

void msgSend(uint8_t msg) {
  mqttClient.publish(MQTT_TOPIC_OUT, &msg, 1);
}

bool mqttReconnect() {
  Serial.println();
  Serial.println();
  Serial.print("(Re)connecting to MQTT server on ");
  Serial.print(MQTT_SERVER_ADDR);
  Serial.print("...");

  for (int i = 0; !mqttClient.connected(); i++) {
    if (mqttClient.connect(MQTT_CLIENT_ID,
                           MQTT_USER_NAME,
                           MQTT_USER_PASSWORD,
                           MQTT_TOPIC_OUT,
                           0,
                           0,
                           MSG_OUT_LAST_WILL)) {
      msgSend(MSG_OUT_READY);
      mqttClient.subscribe(MQTT_TOPIC_IN);
    } else {
      //wait 5 seconds
      for (int j = 0; j < 20; j++) {
        ledBlink(PIN_LED_STATUS);
        delay(125);
      }
      Serial.print(".");
      if (i >= 5) return false;
    }
  }
  Serial.println(" Done");

  state = IDLE;
  Serial.println("Current state: IDLE");
  digitalWrite(PIN_LED_STATUS, LED_ON);
  return true;
}

void wifiDisconnect() {
  WiFi.disconnect();
  digitalWrite(PIN_LED_STATUS, LED_OFF);
}

void wifiConnect() {
  Serial.println();
  Serial.println();
  Serial.print("(Re)connecting to Wi-Fi \"");
  Serial.print(WIFI_SSID);
  Serial.print("\"...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(PIN_LED_STATUS, LED_ON);
    delay(250);
    digitalWrite(PIN_LED_STATUS, LED_OFF);
    delay(250);
    Serial.print(".");
  }
  Serial.println(" Done");
  Serial.println("IP address: ");
  Serial.print(WiFi.localIP());
}

void wifiReconnect() {
  wifiDisconnect();
  wifiConnect();
}

void allReconnect() {
  do {
    if (WiFi.status() != WL_CONNECTED) {
      wifiReconnect();
    }
  } while(!mqttReconnect());
}

void callAnswer() {
  Serial.print("Call answer... ");
  digitalWrite(PIN_RELAY_DOOR_OPEN, RELAY_OFF);
  digitalWrite(PIN_RELAY_ANSWER, RELAY_ON);
  Serial.println("Done");
}

void callHangUp() {
  Serial.print("Hang up... ");
  digitalWrite(PIN_RELAY_ANSWER, RELAY_OFF);
  digitalWrite(PIN_RELAY_DOOR_OPEN, RELAY_OFF);
  Serial.println("Done");
}

void doorOpen() {
  Serial.print("Door open... ");
  digitalWrite(PIN_RELAY_DOOR_OPEN, RELAY_ON);
  delay(RELAY_OPEN_ON_TIME);
  digitalWrite(PIN_RELAY_DOOR_OPEN, RELAY_OFF);
  Serial.println("Done");
}

void answerAndOpen() {
  callAnswer();
  delay(RELAY_ANSWER_ON_TIME);
  doorOpen();
  callHangUp();
  lastOpenTime = millis();
}

void answerAndReject() {
  callAnswer();
  delay(RELAY_ANSWER_ON_TIME);
  callHangUp();
}

void onMqttMsgReceived(char* topic, byte* payload, unsigned int len) {
  if (len != 1) return;
  uint8_t cmd = (uint8_t)payload[0];
  switch (cmd) {
    case MSG_IN_OPEN:
      action = OPEN;
      break;

    case MSG_IN_REJECT:
      action = REJECT;
      break;

    case MSG_IN_PING:
      msgSend(MSG_OUT_READY);
      break;

    default:
      break;
  }
}

void mqttInit() {
  mqttClient.setServer(MQTT_SERVER_ADDR, MQTT_SERVER_PORT);
  mqttClient.setCallback(onMqttMsgReceived);
}

void bootUpTest() {
  int pins[3] = {PIN_LED_STATUS, PIN_LED_GREEN, PIN_LED_RED};
  ledBlink(pins, 3);
  callAnswer();
  delay(500);
  callHangUp();
  delay(500);
  doorOpen();
}

void setup() {
  gpioInit();
  debouncerBtnGreen.attach(PIN_BUTTON_GREEN);
  debouncerBtnGreen.interval(25);
  debouncerBtnRed.attach(PIN_BUTTON_RED);
  debouncerBtnRed.interval(25);
  Serial.begin(115200);
  Serial.println();
  Serial.println();
  Serial.println(DEVICE_NAME " HW Ver. " DEVICE_HW_VERSION " SW Ver. " DEVICE_SW_VERSION);

  bootUpTest();

  mqttInit();
}

void loop() {
  if ( (state == IDLE) && ( (WiFi.status() != WL_CONNECTED) || (!mqttClient.connected()) ) ) {
    allReconnect();
  }

  if ((recoveryAttempts > 0) && ((millis() - lastOpenTime) > CALL_OPEN_RECOVERY_WINDOW)) {
    recoveryAttempts = 0;
  }

  mqttClient.loop();
  debouncerBtnGreen.update();
  debouncerBtnRed.update();

  EState oldState = state;
  if (digitalRead(PIN_CALL_DETECT)) {
    state = CALL;
    lastCallDetectedTime = millis();
  } else if (millis() - lastCallDetectedTime > CALL_HANGUP_DETECT_DELAY) {
    state = IDLE;
  }

  switch (state) {
    case IDLE:
      if (oldState != IDLE) {
        msgSend(MSG_OUT_HANGUP);
        digitalWrite(PIN_LED_GREEN, LED_OFF);
        digitalWrite(PIN_LED_RED, LED_OFF);
        Serial.println("Current state: IDLE");
      }
      if (action != NO_ACTION) {
        msgSend(MSG_OUT_FAIL);
        action = NO_ACTION;
      }
      break;

    case CALL:
      if (oldState != CALL) {
        msgSend(MSG_OUT_CALL);
        digitalWrite(PIN_LED_GREEN, LED_ON);
        digitalWrite(PIN_LED_RED, LED_ON);
        Serial.println("Current state: CALL");

        if ((recoveryAttempts < CALL_OPEN_RECOVERY_MAX_ATTEMPTS) &&
            ((millis() - lastOpenTime) <= CALL_OPEN_RECOVERY_WINDOW)) {
          action = OPEN_BY_RECOVERY;
          recoveryAttempts++;
        } else {
          action = NO_ACTION;
        }
      }
      if (action == NO_ACTION) {
        if (debouncerBtnRed.fell()) action = REJECT_BY_BUTTON;
        else if (debouncerBtnGreen.fell()) action = OPEN_BY_BUTTON;
      }
      switch (action) {
        case OPEN_BY_RECOVERY:
          answerAndOpen();
          msgSend(MSG_OUT_OPENED_BY_RECOVERY);
          break;

        case OPEN_BY_BUTTON:
          answerAndOpen();
          msgSend(MSG_OUT_OPENED_BY_BUTTON);
          break;

        case REJECT_BY_BUTTON:
          answerAndReject();
          msgSend(MSG_OUT_REJECTED_BY_BUTTON);
          break;

        case OPEN:
          answerAndOpen();
          msgSend(MSG_OUT_SUCCESS);
          break;

        case REJECT:
          answerAndReject();
          msgSend(MSG_OUT_SUCCESS);
          break;

        default:
          break;
      }
      action = NO_ACTION;
      break;
  }
}
