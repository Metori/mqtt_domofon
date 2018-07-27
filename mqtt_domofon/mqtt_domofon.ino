#include <ESP8266WiFi.h>
#include <PubSubClient.h>

/* HW history
 * 0.1 - Initial hw for testing
 * 1.0 - Device installed in white box housing. Can be mounted on the wall like domofon terminal.
 */

/*
 * SW history
 * 0.1 - Initial code. Has only one feature: auto open the door after incoming call.
 * 0.3 - Added MQTT over Wi-Fi support.
 */

#define DEVICE_NAME "MQTT Domofon controller by Artem Pinchuk"
#define DEVICE_HW_VERSION "1.0"
#define DEVICE_SW_VERSION "0.3"

// ***** CONFIG *****
// Hardware configuration
#define RELAY_ON LOW
#define RELAY_OFF HIGH
#define BUTTON_OFF HIGH
#define BUTTON_ON LOW

const int PIN_BUTTON_OPEN = 5; //D1
const int PIN_LED_CALL = 4; //D2
const int PIN_LED_STATUS = 14; //D5
const int PIN_CALL_DETECT = 12; //D6
const int PIN_RELAY_ANSWER = 13; //D7
const int PIN_RELAY_DOOR_OPEN = 2; //D4

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

// High level protocol messages
const uint8_t MSG_OUT_READY = 0x52; //'R'
const uint8_t MSG_OUT_CALL = 0x43; //'C'
const uint8_t MSG_OUT_HANGUP = 0x48; //'H'
const uint8_t MSG_OUT_SUCCESS = 0x53; //'S'
const uint8_t MSG_OUT_FAIL = 0x46; //'F'
const uint8_t MSG_IN_OPEN = 0x4F; //'O'
const uint8_t MSG_IN_REJECT = 0x4E; //'N'
// ***** END OF CONFIG *****

typedef enum {
  IDLE,
  CALL
} EState;

EState state;
unsigned long btnLastDebounceTime = 0;
bool btnLastDebounceState = false;
bool btnLastRealState = false;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

void gpioInit() {
  pinMode(PIN_BUTTON_OPEN, INPUT_PULLUP);
  pinMode(PIN_CALL_DETECT, INPUT);
  pinMode(PIN_LED_CALL, OUTPUT);
  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_RELAY_ANSWER, OUTPUT);
  pinMode(PIN_RELAY_DOOR_OPEN, OUTPUT);

  digitalWrite(PIN_LED_STATUS, LOW);
  digitalWrite(PIN_LED_CALL, LOW);
  digitalWrite(PIN_RELAY_ANSWER, RELAY_OFF);
  digitalWrite(PIN_RELAY_DOOR_OPEN, RELAY_OFF);
}

void ledBlink(int* pins, int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(pins[i], HIGH);
  }
  delay(125);
  for (int i = 0; i < count; i++) {
    digitalWrite(pins[i], LOW);
  }
}

void ledBlink(int pin) {
  ledBlink(&pin, 1);
}

bool readOpenButtonLoop() {
  return false;
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
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER_NAME, MQTT_USER_PASSWORD)) {
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
  digitalWrite(PIN_LED_STATUS, HIGH);
  return true;
}

void wifiDisconnect() {
  WiFi.disconnect();
  digitalWrite(PIN_LED_STATUS, LOW);
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
    digitalWrite(PIN_LED_STATUS, HIGH);
    delay(250);
    digitalWrite(PIN_LED_STATUS, LOW);
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

bool isCalling() {
  return digitalRead(PIN_CALL_DETECT) == HIGH;
}

void callAnswer() {
  digitalWrite(PIN_RELAY_DOOR_OPEN, RELAY_OFF);
  digitalWrite(PIN_RELAY_ANSWER, RELAY_ON);
}

void callHangUp() {
  digitalWrite(PIN_RELAY_ANSWER, RELAY_OFF);
  digitalWrite(PIN_RELAY_DOOR_OPEN, RELAY_OFF);
}

void doorOpen() {
  digitalWrite(PIN_RELAY_DOOR_OPEN, RELAY_ON);
  delay(500);
  digitalWrite(PIN_RELAY_DOOR_OPEN, RELAY_OFF);
}

void onMqttMsgReceived(char* topic, byte* payload, unsigned int len) {
  //TODO
}

void mqttInit() {
  mqttClient.setServer(MQTT_SERVER_ADDR, MQTT_SERVER_PORT);
  mqttClient.setCallback(onMqttMsgReceived);
}

void setup() {
  gpioInit();
  Serial.begin(115200);
  Serial.println("Boot-up success");

  int pins[2] = {PIN_LED_STATUS, PIN_LED_CALL};
  ledBlink(pins, 2);

  mqttInit();
}

void loop() {
  if ( (WiFi.status() != WL_CONNECTED) || (!mqttClient.connected()) ) {
    allReconnect();
  }

  EState oldState = state;
  state = isCalling() ? CALL : IDLE;

  switch (state) {
    case IDLE:
      if (oldState == CALL) {
        msgSend(MSG_OUT_HANGUP);
        digitalWrite(PIN_LED_CALL, LOW);
        Serial.println("Current state: IDLE");
      }
      break;

    case CALL:
      if (oldState == IDLE){
        msgSend(MSG_OUT_CALL);
        digitalWrite(PIN_LED_CALL, HIGH);
        Serial.println("Current state: CALL");
      }
      
      break;
  }

  mqttClient.loop();
}
