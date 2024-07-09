// Main program
#include <ArduinoMqttClient.h>
#if defined(ARDUINO_SAMD_MKRWIFI1010) || defined(ARDUINO_SAMD_NANO_33_IOT) || defined(ARDUINO_AVR_UNO_WIFI_REV2)
  #include <WiFiNINA.h>
#elif defined(ARDUINO_SAMD_MKR1000)
  #include <WiFi101.h>
#elif defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ARDUINO_PORTENTA_H7_M7) || defined(ARDUINO_NICLA_VISION) || defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_GIGA) || defined(ARDUINO_OPTA)
  #include <WiFi.h>
#elif defined(ARDUINO_PORTENTA_C33)
  #include <WiFiC3.h>
#elif defined(ARDUINO_UNOR4_WIFI)
  #include <WiFiS3.h>
#endif

#include "arduino_secrets.h"

#define INTERRUPTEUR 5
#define RELAIS 3
#define PULSE_DURATION 5000

#define OPENING_TIME 8000

char ssid[] = SECRET_SSID;        // your network SSID
char pass[] = SECRET_PASS;    // your network password

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

const char broker[] = "192.168.1.64";
int        port     = 1883;
const char topic_command[]  = "homeassistant/cover/garage_door/command";
const char topic_state[]  = "homeassistant/cover/garage_door/state";
const char topic_config[]  = "homeassistant/cover/garage_door/config";

const char config[] = "{\
\"device_class\": \"garage\",\
\"command_topic\": \"homeassistant/cover/garage_door/command\",\
\"value_template\": \"{{ value_json.state }}\",\
\"state_topic\": \"homeassistant/cover/garage_door/state\",\
\"value_template\": \"{{ value_json.state }}\",\
\"position_topic\": \"homeassistant/cover/garage_door/state\",\
\"value_template\": \"{{ value_json.position }}\",\
\"unique_id\": \"garage_door\",\
\"retain\": true,\
\"payload_open\": \"OPEN\",\
\"payload_close\": \"CLOSE\",\
\"payload_stop\": \"STOP\",\
\"state_open\": \"opened\",\
\"state_opening\": \"opening\",\
\"state_closed\": \"closed\",\
\"state_closing\": \"closing\",\
\"device\": {\
    \"identifiers\": [\"garage_door\"],\
    \"name\": \"Porte de garage\",\
    \"manufacturer\": \"Genie Pro 88\",\
    \"model\": \"Genie Pro 88\"\
}\
}";


#define state_opened "opened"
#define state_opening "opening"
#define state_closed "closed"
#define state_closing "closing"

const char command_open[] = "OPEN";
const char command_stop[] = "STOP";
const char command_close[] = "CLOSE";

unsigned long mouvement_start = 0;


enum STATES {
  STATE_OPENED,
  STATE_OPENING,
  STATE_CLOSED,
  STATE_CLOSING,
  STATE_STOPPED
} state, previous_state;


void setup() {

  state = STATE_CLOSED;
  previous_state = STATE_CLOSED;

  // put your setup code here, to run once:
  Serial.begin(9600);
  while(!Serial) {
    ;
  }

  Serial.write("START\n");

  pinMode(INTERRUPTEUR, INPUT_PULLUP);
  pinMode(RELAIS, OUTPUT);
  digitalWrite(RELAIS, LOW);


  // attempt to connect to Wifi network:
  Serial.print("Attempting to connect to WPA SSID: ");
  Serial.println(ssid);
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    // failed, retry
    Serial.print(".");
    delay(5000);
  }

  Serial.println("You're connected to the network");
  Serial.println();

  Serial.print("Attempting to connect to the MQTT broker: ");
  Serial.println(broker);


  if (!mqttClient.connect(broker, port)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());

    while (1);
  }

  Serial.println("You're connected to the MQTT broker!");
  Serial.println();

  mqttClient.poll();

  mqttClient.setTxPayloadSize(768);
  mqttClient.beginMessage(topic_config);
  mqttClient.print(config);
  mqttClient.endMessage();

  Serial.print("Subscribing to topic: ");
  Serial.println(topic_command);
  Serial.println();

  // subscribe to a topic
  mqttClient.subscribe(topic_command);

  // Init state topic
  send_state();
}


void loop() {
  // mqttClient.poll();

  bool pushed = false;

  int messageSize = mqttClient.parseMessage();
  if (messageSize) {
    // we received a message, print out the topic and contents
    Serial.print("Received a message with topic '");
    Serial.print(mqttClient.messageTopic());
    Serial.print("', length ");
    Serial.print(messageSize);
    Serial.println(" bytes:");

    // use the Stream interface to print the contents
    while (mqttClient.available()) {
      Serial.print((char)mqttClient.read());
    }
    Serial.println();

    Serial.println();

    pushed = true;
  }

  if(!pushed) {
  // Si interrupteur mural a été pressé
    PinStatus value = digitalRead(INTERRUPTEUR);
    if (value == LOW) {
      pushed = true;
    }
  }
  if (pushed) {
    previous_state = state;
    switch (state) {
      case STATE_CLOSED: 
        state = STATE_OPENING;
        mouvement_start = micros();
        break;
      case STATE_OPENED:
        state = STATE_CLOSING;
        mouvement_start = micros();
        break;
      case STATE_OPENING:
      case STATE_CLOSING:
        state = STATE_STOPPED;
        break;
      case STATE_STOPPED:
        if (previous_state == STATE_OPENING) {
          state = STATE_CLOSING;
        }
        else {
          state = STATE_OPENING;
        }
    }
    Serial.println("push detected");
    Serial.print("Old state : ");
    Serial.println(previous_state);
    Serial.print("New state : ");
    Serial.println(state);
    Serial.println("");
    send_state();
    push_button();
  }

  unsigned long elapsed_time = (micros() / 1000) - mouvement_start;
  if (elapsed_time > OPENING_TIME) {
    switch (state) {
      case STATE_OPENING:
        state = STATE_OPENED;
        send_state();
        break;
      case STATE_CLOSING:
        state = STATE_CLOSED;
        send_state();
        break;
    }
  }

  delay(100);

}

void push_button() {
    Serial.write("push\n");
    digitalWrite(RELAIS, HIGH);
    delay(PULSE_DURATION);
    digitalWrite(RELAIS, LOW);
}

void send_state() {
  int position;
  char *status;
  switch(state) {
    case STATE_OPENED:
      status = state_opened;
      position = 100;
      break;
    case STATE_CLOSED:
      status = state_closed;
      position = 100;
      break;
    case STATE_OPENING:
      status = state_opening;
      position = 70;
      break;
    case STATE_CLOSING:
      status = state_closed;
      position = 0;
      break;
  }

  char buffer[64];
  sprintf(buffer, "{\"state\":\"%s\":\"position\":%d}", status, position);
  Serial.print("Sending state: ");
  Serial.println(buffer);
  mqttClient.beginMessage(topic_state);
  mqttClient.print(buffer);
  mqttClient.endMessage();
}
