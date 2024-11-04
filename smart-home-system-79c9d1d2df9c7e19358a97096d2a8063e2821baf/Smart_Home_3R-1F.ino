#include <FS.h>
#include <Arduino.h>
// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// library inclusions
// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <AceButton.h>
using namespace ace_button;
#include <BlynkSimpleEsp8266.h>
#include "AsyncPing.h"
#include "Ticker.h"
#include "hw_timer.h"
#include <ESP8266mDNS.h>
#include <arduino_homekit_server.h>
#define LOG_D(fmt, ...)   printf_P(PSTR(fmt "\n") , ##__VA_ARGS__);
#include <WiFiManager.h>
#include <ArduinoJson.h>

// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// Credential Definitions
// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
#define SIRI_FEATURES true
#define BLYNK_FEATURES true

// Variables
char mqtt_server[40] = "<MQTT_SERVER_IP_HERE>"; 
char blynk_server_ip[40] = "<BLYNK_SERVER_IP_HERE>";
char mqtt_username[20] = "<MQTT_USER_HERE>";
char blynk_auth[34] = "<BLYNK_AUTH_HERE>";
char email_id[100] = "<EMAIL_ID_HERE>";
char device_id[5] = "D1";
const char* mqtt_pass = "<MQTT_PASS_HERE>";

// Constants
char serial_number[9] = "XXXXXXXX";
int port = 8080;
char* ap_ssid = "Smart Home System";
char* ap_pass = "123456789";

String generate_serial_number(char* curr_serial_number){  
  String new_serial_number = "";
  if(strstr(curr_serial_number,"XXXXXXXX"))
    new_serial_number = String(random(0xffff), HEX) + String(random(0xffff), HEX);
  else
    new_serial_number = String(curr_serial_number);
  new_serial_number.toUpperCase();
  return new_serial_number;
}
// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// Ping functionality:
// To check if the chip is online or offline (to further avoid blocking in client.reconnect();
// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
bool internet_status = true;
Ticker timer;
AsyncPing Pings[3];
IPAddress addrs[3];
const char *ips[]={mqtt_server,NULL,NULL};

void ping(){
  if (SIRI_FEATURES) announce();
  for (int i = 0; i < 1 ; i++){   
    Pings[i].begin(addrs[i]);
  }
}

void ping_init(){
    for (int i = 0; i < 3 ; i++){
      if (ips[i]){
        if (!WiFi.hostByName(ips[i], addrs[i]))
          addrs[i].fromString(ips[i]);
      }else
        addrs[i] = WiFi.gatewayIP();

      Pings[i].on(true,[](const AsyncPingResponse& response){
        IPAddress addr(response.addr); //to prevent with no const toString() in 2.3.0               
        return false; //do not stop
      });

      Pings[i].on(false,[](const AsyncPingResponse& response){
        IPAddress addr(response.addr); //to prevent with no const toString() in 2.3.0        
        // use -> response.total_sent,response.total_recv to tell if chip is online or offline        
        if (response.total_recv > 0) internet_status = true; 
        else internet_status = false;        
        return true;
      });
    }
    ping();
    timer.attach(10,ping); 
}

// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// %              GPIO pin declarations                %
// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
#define R1 1
#define R2 2
#define R3 3
#define R4 15
#define pwmPin 4
const byte zcPin = 5;
byte fade = 0;      // Make this 1 to enable fading effect
byte state = 1;     // 0 means switch off, 1 means switch on

byte tarBrightness = 255;
byte curBrightness = 0;
byte zcState = 0; // 0 = ready; 1 = processing;

#define S1 14       // Purple-Blue
#define S2 12       // Green-Yellow
#define S3 13       // Brown-black
#define S4 10       // Red-orange

int relay_1_state = 0; 
int relay_2_state = 0; 
int relay_3_state = 0; 
int relay_4_state = 0;
int fan_state = 0;

// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// %           Dimming Functionality of Triac          %
// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
/* Triac implementation - dimTimerISR for dimming */
ICACHE_RAM_ATTR void dimTimerISR() {

 // code for disabled fading effect - Take current brightness to target brightness directly
      if (fade == 0) {
            // If fan is on, make the current brightness as target brightness
            if (state == 1) {
              curBrightness = tarBrightness;
            }
            // Fan is off, make the current brightness 0           
            else {
              curBrightness = 0;
            }
      }


    // Code for fade effect enabled - Take current brightness to target brightness in steps
    if (fade == 1) {
      
      // (target < current) or (fan is off and current brightness is not yet 0)
      if (curBrightness > tarBrightness || (state == 0 && curBrightness > 0)) {
        // decrement current brightness       
        --curBrightness;
      }
      
      // (target > current) and (fan is on and current brightness is not yet 255)
      else if (curBrightness < tarBrightness && state == 1 && curBrightness < 255) {
        // increment current brightness       
        ++curBrightness;
      }
    }

    // Current brightness became 0 after decrementing
    if (curBrightness == 0) {
      // make the state of the fan 0
      state = 0;
      // Turn off the fan
      digitalWrite(pwmPin, 0);
    }

    // Current brightness became 255 after incrementing
    else if (curBrightness == 255) {
      // make the state of the fan 1
      state = 1;
      // Turn on the fan
      digitalWrite(pwmPin, 1);
    }
    else {
      // make the state of the fan 1 for enabling dimming
      state = 1;
      // Turn on the fan to use dimming
      digitalWrite(pwmPin, 1);
    }

    // Make the zero cross state 0    
    zcState = 0;
}

/* Triac implementation - zcDetectISR for dimming */
ICACHE_RAM_ATTR void zcDetectISR() {
  // If the zero cross state was 0    
  if (zcState == 0) {
    // Make the zero cross state 1    
    zcState = 1;

    // If brightness is between 0 and 255
    if (curBrightness < 255 && curBrightness > 0) {
      // Turn off the fan
      digitalWrite(pwmPin, 0);

      // Induce delay in dimming
      int dimDelay = 30 * (255 - curBrightness) + 400;//400
      hw_timer_arm(dimDelay);
    }
  }
}


// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// %                  Homekit Config                  %
// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
bool announce(){
  MDNS.announce();
  return true;
}

extern "C" char serial_num[8];
extern "C" char bridge_name[20];
void generate_homekit_credentials(){
  String bn = "ASAP SB31-";
  String sno = String(serial_number);
  int serial_length = sno.length();
  bn += sno.substring(0,2) + sno.substring(serial_length - 2);

  sno.toCharArray(serial_num, sno.length() + 1);
  bn.toCharArray(bridge_name, bn.length() + 1);
}

extern "C" homekit_server_config_t config;
extern "C" homekit_characteristic_t cha_switch1_on;
extern "C" homekit_characteristic_t cha_switch2_on;
extern "C" homekit_characteristic_t cha_switch3_on;

extern "C" homekit_characteristic_t cha_fan_active;
extern "C" homekit_characteristic_t cha_fan_rotation_speed;

static uint32_t next_heap_millis = 0;

void cha_switch1_on_setter(const homekit_value_t value) {
  bool on = value.bool_value;
  cha_switch1_on.value.bool_value = on;  //sync the value
  LOG_D("Switch: %s", on ? "ON" : "OFF");
  if(on){
    relay_1_state = 0;
    digitalWrite(R1, LOW);
    if(internet_status) MQTT_WRITE(1, "1");
    if (BLYNK_FEATURES) Blynk.virtualWrite(V1, 1);
    
  }else{
    relay_1_state = 1;
    digitalWrite(R1, HIGH);
    if(internet_status) MQTT_WRITE(1, "0");
    if (BLYNK_FEATURES) Blynk.virtualWrite(V1, 0);
  }
}

//Called when the switch value is changed by iOS Home APP
void cha_switch2_on_setter(const homekit_value_t value) {
  bool on = value.bool_value;
  cha_switch2_on.value.bool_value = on;  //sync the value
  LOG_D("Switch: %s", on ? "ON" : "OFF");
  if(on){
    relay_2_state = 0;
    digitalWrite(R2, LOW);  
    if(internet_status) MQTT_WRITE(2, "1");
    if (BLYNK_FEATURES) Blynk.virtualWrite(V2, 1);
    
  }else{
    relay_2_state = 1;
    digitalWrite(R2, HIGH);
    if(internet_status) MQTT_WRITE(2, "0");
    if (BLYNK_FEATURES) Blynk.virtualWrite(V2, 0);
  }
}

//Called when the switch value is changed by iOS Home APP
void cha_switch3_on_setter(const homekit_value_t value) {
  bool on = value.bool_value;
  cha_switch3_on.value.bool_value = on;  //sync the value
  LOG_D("Switch: %s", on ? "ON" : "OFF");
  if(on){
    relay_3_state = 0;
    digitalWrite(R3, LOW);
    if(internet_status) MQTT_WRITE(3, "1");
    if (BLYNK_FEATURES) Blynk.virtualWrite(V3, 1);
  }else{
    relay_3_state = 1;
    digitalWrite(R3, HIGH);
    if(internet_status) MQTT_WRITE(3, "0");
    if (BLYNK_FEATURES) Blynk.virtualWrite(V3, 0);
  }
}

void set_fan_active(const homekit_value_t v) {
    int fan_active = v.int_value;
    cha_fan_active.value.int_value = fan_active; //sync the value

//    active ? is_on = true: is_on = false;
    if(fan_active){ 
      if(relay_4_state == 1){ //fan was off
        relay_4_state = 0;
        digitalWrite(R4, LOW);
        state = 1;
        tarBrightness = 255;
        if(internet_status) MQTT_WRITE(4, "100");
        cha_fan_rotation_speed.value.float_value = 100.0;
        if (BLYNK_FEATURES) Blynk.virtualWrite(V4, 1);
        if (BLYNK_FEATURES) Blynk.virtualWrite(V5, 100);
      }
    }
    else {
        relay_4_state = 1;
        digitalWrite(R4, HIGH);
        state = 0;
        tarBrightness = 0;
        if(internet_status) MQTT_WRITE(4, "0");
        cha_fan_rotation_speed.value.float_value = 0;
        if (BLYNK_FEATURES) Blynk.virtualWrite(V4, 0);
        if (BLYNK_FEATURES) Blynk.virtualWrite(V5, 0);
    }
}

void set_fan_speed(const homekit_value_t v) {
    float fan_speed = v.float_value;
    cha_fan_rotation_speed.value.float_value = fan_speed; //sync the value
    int siri_fan_speed = (int)fan_speed;
    if(siri_fan_speed == 0) {
      relay_4_state = 1;
      digitalWrite(R4, HIGH);
      state = 0;
      tarBrightness = 0;
      if(internet_status) MQTT_WRITE(4, "0");
      if (BLYNK_FEATURES) Blynk.virtualWrite(V4, 0);
      if (BLYNK_FEATURES) Blynk.virtualWrite(V5, 0);
    }else{
      if(relay_4_state == 1){
        relay_4_state = 0;
        digitalWrite(R4, LOW);        
      }
      int b = map(siri_fan_speed,0, 100,0, 255);
      state = 1;
      tarBrightness = b;
      char siri_fan_speed_str[8];
      itoa(siri_fan_speed, siri_fan_speed_str, 10);
      if(internet_status) MQTT_WRITE(4, siri_fan_speed_str);
      if (BLYNK_FEATURES) Blynk.virtualWrite(V4, 1);
      if (BLYNK_FEATURES) Blynk.virtualWrite(V5, siri_fan_speed);
    }
}

void my_homekit_setup() {
  if (SIRI_FEATURES) generate_homekit_credentials();
  cha_switch1_on.setter = cha_switch1_on_setter;
  cha_switch2_on.setter = cha_switch2_on_setter;
  cha_switch3_on.setter = cha_switch3_on_setter;
  cha_fan_active.setter = set_fan_active;
  cha_fan_rotation_speed.setter = set_fan_speed;
  arduino_homekit_setup(&config);
}

// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// %    Update relays from data coming from switches    %
// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
ButtonConfig config1;
AceButton button1(&config1);
ButtonConfig config2;
AceButton button2(&config2);
ButtonConfig config3;
AceButton button3(&config3);
ButtonConfig config4;
AceButton button4(&config4);

void button1Handler(AceButton* button, uint8_t eventType, uint8_t buttonState) {
  switch (eventType) {
    case AceButton::kEventPressed:
      relay_1_state = 0;
      digitalWrite(R1, LOW);
      if(internet_status) MQTT_WRITE(1, "1");
      if (BLYNK_FEATURES) Blynk.virtualWrite(V1, 1);
      if (SIRI_FEATURES){
        cha_switch1_on.value.bool_value = 1;
        homekit_characteristic_notify(&cha_switch1_on, cha_switch1_on.value);     
      }
      break;
    case AceButton::kEventReleased:
      relay_1_state = 1;
      digitalWrite(R1, HIGH);
      if(internet_status) MQTT_WRITE(1, "0");
      if (BLYNK_FEATURES) Blynk.virtualWrite(V1, 0);
      if (SIRI_FEATURES){
        cha_switch1_on.value.bool_value = 0;
        homekit_characteristic_notify(&cha_switch1_on, cha_switch1_on.value);
      }
      break;
    case AceButton::kEventDoubleClicked:
      delay(3000);
      ESP.reset();
      break;
  }
}

void button2Handler(AceButton* button, uint8_t eventType, uint8_t buttonState) {
  switch (eventType) {
    case AceButton::kEventPressed:
      relay_2_state = 0;
      digitalWrite(R2, LOW);  
      if(internet_status) MQTT_WRITE(2, "1");
      if (BLYNK_FEATURES) Blynk.virtualWrite(V2, 1);
      if (SIRI_FEATURES){
        cha_switch2_on.value.bool_value = 1;
        homekit_characteristic_notify(&cha_switch2_on, cha_switch2_on.value); 
      }
      break;
    case AceButton::kEventReleased:
      relay_2_state = 1;
      digitalWrite(R2, HIGH);
      if(internet_status) MQTT_WRITE(2, "0");
      if (BLYNK_FEATURES) Blynk.virtualWrite(V2, 0);
      if (SIRI_FEATURES){
          cha_switch2_on.value.bool_value = 0;
          homekit_characteristic_notify(&cha_switch2_on, cha_switch2_on.value);
      }
      break;
    case AceButton::kEventDoubleClicked:
//      WiFiManager wifiManager;
//      wifiManager.resetSettings();
      startConfigPortal();
      delay(3000);
      ESP.reset();
      break;
  }
}

void button3Handler(AceButton* button, uint8_t eventType, uint8_t buttonState) {
  switch (eventType) {
    case AceButton::kEventPressed:
      relay_3_state = 0;
      digitalWrite(R3, LOW);
      if(internet_status) MQTT_WRITE(3, "1");
      if (BLYNK_FEATURES) Blynk.virtualWrite(V3, 1);
      if (SIRI_FEATURES){
        cha_switch3_on.value.bool_value = 1;
        homekit_characteristic_notify(&cha_switch3_on, cha_switch3_on.value);
      }
      break;
    case AceButton::kEventReleased:
      relay_3_state = 1;
      digitalWrite(R3, HIGH);
      if(internet_status) MQTT_WRITE(3, "0");
      if (BLYNK_FEATURES) Blynk.virtualWrite(V3, 0);
      if (SIRI_FEATURES){
        cha_switch3_on.value.bool_value = 0;
        homekit_characteristic_notify(&cha_switch3_on, cha_switch3_on.value);
      }
      break;
    case AceButton::kEventDoubleClicked:
      if(SIRI_FEATURES){
        homekit_storage_reset();
        delay(3000);
        ESP.reset();
      }
      break;
  }
}

void button4Handler(AceButton* button, uint8_t eventType, uint8_t buttonState) {
  switch (eventType) {
    case AceButton::kEventPressed:
      relay_4_state = 0;
      digitalWrite(R4, LOW);
      fan_state = 1;
      state = 1;
      tarBrightness = 255;
      if(internet_status) MQTT_WRITE(4, "100");
      if (BLYNK_FEATURES){ 
        Blynk.virtualWrite(V4, 1);
        Blynk.virtualWrite(V5, 100);
      }
      if (SIRI_FEATURES){
        cha_fan_active.value.int_value = 1;
        cha_fan_rotation_speed.value.float_value = 100.0;
        homekit_characteristic_notify(&cha_fan_active, cha_fan_active.value);
        homekit_characteristic_notify(&cha_fan_rotation_speed, cha_fan_rotation_speed.value);
      }
      break;
    case AceButton::kEventReleased:
      fan_state = 0;
      state = 0;
      tarBrightness = 0;
      relay_4_state = 1;
      digitalWrite(R4, HIGH);
      if(internet_status) MQTT_WRITE(4, "0");
      if (BLYNK_FEATURES){
        Blynk.virtualWrite(V4, 0); 
        Blynk.virtualWrite(V5, 0);
      }
      if (SIRI_FEATURES){
        cha_fan_active.value.int_value = 0;
        homekit_characteristic_notify(&cha_fan_active, cha_fan_active.value);
      }
      break;
  }
}

void switches_init(){
  config1.setEventHandler(button1Handler);
  config2.setEventHandler(button2Handler);
  config3.setEventHandler(button3Handler);
  config4.setEventHandler(button4Handler);

  config1.setDebounceDelay(100);
  config2.setDebounceDelay(100);
  config3.setDebounceDelay(100);
  config4.setDebounceDelay(100);
  
  config1.setFeature(ButtonConfig::kFeatureDoubleClick);
  config2.setFeature(ButtonConfig::kFeatureDoubleClick);
  config3.setFeature(ButtonConfig::kFeatureDoubleClick);
  config4.setFeature(ButtonConfig::kFeatureDoubleClick);

  button1.init(S1);
  button2.init(S2);
  button3.init(S3);
  button4.init(S4);
}

void manual_switches(){
  button1.check();
  button2.check();
  button3.check();
  button4.check();
}

// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// %               Update data coming from MQTT Server            %
// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
WiFiClient espClient;
PubSubClient client(espClient);

String email_id_str, device_id_str, email_device, st1, st2, st3, st4, pt1, pt2, pt3, pt4;
const char* sub_top1;
const char* sub_top2;
const char* sub_top3;
const char* sub_top4;
const char* pub_top1;
const char* pub_top2;
const char* pub_top3;
const char* pub_top4;

void generate_mqtt_topics(){
  email_id_str = String(email_id);
  device_id_str = String(device_id);

  email_device = email_id_str + "/" + device_id_str;

  st1 = email_device + "/R1/sub";
  st2 = email_device + "/R2/sub";
  st3 = email_device + "/R3/sub"; 
  //st4 = email_device + "/R4/sub"; 
  st4 = email_device + "/F1/sub";
  
  pt1 = email_device + "/R1/pub"; 
  pt2 = email_device + "/R2/pub";  
  pt3 = email_device + "/R3/pub"; 
  //pt4 = email_device + "/R4/pub";
  pt4 = email_device + "/F1/pub";

  sub_top1 = st1.c_str();  
  sub_top2 = st2.c_str();
  sub_top3 = st3.c_str();
  sub_top4 = st4.c_str();

  pub_top1 = pt1.c_str();
  pub_top2 = pt2.c_str();
  pub_top3 = pt3.c_str();
  pub_top4 = pt4.c_str();
 
  // print_mqtt_topics();
}

void print_mqtt_topics(){
  Serial.println(sub_top1);  
  Serial.println(sub_top2);
  Serial.println(sub_top3);
  Serial.println(sub_top4);
  Serial.println(pub_top1);
  Serial.println(pub_top2);
  Serial.println(pub_top3);
  Serial.println(pub_top4);
}

void reconnect() {
  if (internet_status) {
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str() , mqtt_username, mqtt_pass)) {
      client.subscribe(st1.c_str());
      client.subscribe(st2.c_str());
      client.subscribe(st3.c_str());
      client.subscribe(st4.c_str());
    } else {
      // Not connected
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
     if(strstr(topic, sub_top1)){
        if ((char)payload[0] == '1'){
            digitalWrite(R1, LOW);  
            relay_1_state = 0;   
            if (BLYNK_FEATURES) Blynk.virtualWrite(V1, 1);  
            if (SIRI_FEATURES){  
              cha_switch1_on.value.bool_value = 1;
              homekit_characteristic_notify(&cha_switch1_on, cha_switch1_on.value);
            }
        }
        else if ((char)payload[0] == '0'){
            digitalWrite(R1, HIGH);
            relay_1_state = 1; 
            if (BLYNK_FEATURES) Blynk.virtualWrite(V1, 0); 
            if (SIRI_FEATURES){
              cha_switch1_on.value.bool_value = 0;
              homekit_characteristic_notify(&cha_switch1_on, cha_switch1_on.value);
            }
        }
     }else if(strstr(topic, sub_top2)){
        if ((char)payload[0] == '1'){
            digitalWrite(R2, LOW);
            relay_2_state = 0;
            if (BLYNK_FEATURES) Blynk.virtualWrite(V2, 1);
            if (SIRI_FEATURES){
              cha_switch2_on.value.bool_value = 1;
              homekit_characteristic_notify(&cha_switch2_on, cha_switch2_on.value);
            }
        }
        else if ((char)payload[0] == '0'){
            digitalWrite(R2, HIGH);
            relay_2_state = 1;
            if (BLYNK_FEATURES) Blynk.virtualWrite(V2, 0);
            if (SIRI_FEATURES){
              cha_switch2_on.value.bool_value = 0;
              homekit_characteristic_notify(&cha_switch2_on, cha_switch2_on.value);
            }
        }
      }
      else if(strstr(topic, sub_top3)){
        if ((char)payload[0] == '1'){
            digitalWrite(R3, LOW);
            relay_3_state = 0;
            if (BLYNK_FEATURES) Blynk.virtualWrite(V3, 1);
            if (SIRI_FEATURES){
              cha_switch3_on.value.bool_value = 1;
              homekit_characteristic_notify(&cha_switch3_on, cha_switch3_on.value);
            }
        }
        else if ((char)payload[0] == '0'){
            digitalWrite(R3, HIGH);
            relay_3_state = 1;
            if (BLYNK_FEATURES) Blynk.virtualWrite(V3, 0);
            if (SIRI_FEATURES){
              cha_switch3_on.value.bool_value = 0;
              homekit_characteristic_notify(&cha_switch3_on, cha_switch3_on.value);
            }
        }
      }else if(strstr(topic, sub_top4)){
        String brightness = "";
        int brightness_int = 1000;
        if((char)payload[0] != 'x'){
          for (int i = 0; i < length; i++){
              brightness.concat((char)payload[i]);
            }                    
          brightness_int = brightness.toInt();
          tarBrightness = map(brightness_int,0,100,0,255);
          if (BLYNK_FEATURES) Blynk.virtualWrite(V5, brightness_int);    
          
          if(brightness_int > 0) {
            relay_4_state = 0;
            digitalWrite(R4, LOW);
            state = 1;
            fan_state = 1;
            if (BLYNK_FEATURES) Blynk.virtualWrite(V4, 1);
            if (SIRI_FEATURES){
              cha_fan_active.value.int_value = 1;
              cha_fan_rotation_speed.value.float_value = (float)brightness_int;
              homekit_characteristic_notify(&cha_fan_active, cha_fan_active.value);
              homekit_characteristic_notify(&cha_fan_rotation_speed, cha_fan_rotation_speed.value);
            }
          }
          else {
            state = 0;
            fan_state = 0;  
            relay_4_state = 1;
            digitalWrite(R4, HIGH);   
            if (BLYNK_FEATURES) Blynk.virtualWrite(V4, 0); 
            if (SIRI_FEATURES){
              cha_fan_active.value.int_value = 0;
              homekit_characteristic_notify(&cha_fan_active, cha_fan_active.value);
            }
           }
         }        
      }
  }

void MQTT_WRITE(int pub_top_num, char* value){
  switch (pub_top_num) {
    case 1:
      client.publish(pub_top1, value);      
      break;
    case 2:
      client.publish(pub_top2, value);
      break;
    case 3:
      client.publish(pub_top3, value);   
      break;
    case 4:
      client.publish(pub_top4, value);      
      break;
  }
}

void mqtt_init(){
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
  // %                  WifiManager init                 %
  // %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

  bool shouldSaveConfig = false;
  void saveConfigCallback() {
    shouldSaveConfig = true;
  }

  void read_internal_memory() {
    if (SPIFFS.begin()) {
      if (SPIFFS.exists("/config.json")) {
        //file exists, reading and loading
        File configFile = SPIFFS.open("/config.json", "r");
        if (configFile) {
          size_t size = configFile.size();
          // Allocate a buffer to store contents of the file.
          std::unique_ptr<char[]> buf(new char[size]);

          configFile.readBytes(buf.get(), size);

          DynamicJsonBuffer jsonBuffer;
          JsonObject& json = jsonBuffer.parseObject(buf.get());
          if (json.success()) {
            strcpy(mqtt_server, json["mqtt_server"]);
            strcpy(blynk_server_ip, json["blynk_server_ip"]);
            strcpy(mqtt_username, json["mqtt_username"]);
            strcpy(blynk_auth, json["blynk_auth"]);
            strcpy(email_id, json["email_id"]);
            strcpy(device_id, json["device_id"]);
            strcpy(serial_number, json["serial_number"]);
            String ap_ssid_str = "ASAP S-Board " + String(device_id);
            ap_ssid_str.toCharArray(ap_ssid, ap_ssid_str.length() + 1);
          } else {
          }
          configFile.close();
        }
      }
    } else {
    }
  }

  void write_internal_memory() {
    if (shouldSaveConfig) {
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.createObject();
      json["mqtt_server"] = mqtt_server;
      json["blynk_server_ip"] = blynk_server_ip;
      json["mqtt_username"] = mqtt_username;
      json["blynk_auth"] = blynk_auth;
      json["email_id"] = email_id;
      json["device_id"] = device_id;
      json["serial_number"] = serial_number;

      File configFile = SPIFFS.open("/config.json", "w");
      if (!configFile) {
        // Not
      }

      json.printTo(configFile);
      configFile.close();
    }
  }

  void startConfigPortal() {
    WiFiManagerParameter custom_mqtt_server("mqtt_server", "mqtt server", mqtt_server, 40);
    WiFiManagerParameter custom_blynk_server_ip("blynk_server_ip", "blynk server ip", blynk_server_ip, 40);
    WiFiManagerParameter custom_mqtt_username("mqtt_username", "mqtt username", mqtt_username, 20);
    WiFiManagerParameter custom_blynk_auth("blynk_auth", "blynk auth", blynk_auth, 34);
    WiFiManagerParameter custom_email_id("email_id", "Email ID", email_id, 100);
    WiFiManagerParameter custom_device_id("device_id", "Device ID", device_id, 5);
    //WiFiManagerParameter custom_serial_number("serial_number", "Serial Number", serial_number, 9);

    WiFiManager wifiManager;

    //set config save notify callback
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    //add all your parameters here
    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_blynk_server_ip);
    wifiManager.addParameter(&custom_mqtt_username);
    wifiManager.addParameter(&custom_blynk_auth);
    wifiManager.addParameter(&custom_email_id);
    wifiManager.addParameter(&custom_device_id);
    //wifiManager.addParameter(&custom_serial_number);

    /*
    //Admin Panel
    std::vector<const char *> menu = {"wifi","param","restart"};
    wifiManager.setMenu(menu);
  */

    wifiManager.setConfigPortalTimeout(60);
    // If wifi not found, search for 30 seconds and move to offline mode.
    if (!wifiManager.startConfigPortal(ap_ssid, ap_pass)) {
      //      Serial.println("Failed to connect and hit timeout");
    }

    //if you get here you have connected to the WiFi

    //read updated parameters
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(blynk_server_ip, custom_blynk_server_ip.getValue());
    strcpy(mqtt_username, custom_mqtt_username.getValue());
    strcpy(blynk_auth, custom_blynk_auth.getValue());
    strcpy(email_id, custom_email_id.getValue());
    strcpy(device_id, custom_device_id.getValue());
    //strcpy(serial_number, custom_serial_number.getValue());

    write_internal_memory();
  }

  void wifimanager_init() {
    read_internal_memory();

    // Generate a unique serial number for device
    String sno = generate_serial_number(serial_number);
    // Update the generated Serial Number in global variable
    sno.toCharArray(serial_number, sno.length() + 1);

    char wifi_ssid[30];
    char wifi_pass[30];
    WiFi.SSID().toCharArray(wifi_ssid, 30);
    WiFi.psk().toCharArray(wifi_pass, 30);
    if (WiFi.SSID() != "") { // check that ESP8266 configured previpusly or not, if yes then changed wifir mode to STA
      WiFi.mode(WIFI_STA);
      WiFi.begin(wifi_ssid, wifi_pass);
      int wifi_connection_ctr = 0;
      while (wifi_connection_ctr < 30) {
        if (WiFi.status() == WL_CONNECTED) break;
        delay(1000);
        wifi_connection_ctr++;
      }
    } else {
      WiFiManagerParameter custom_mqtt_server("mqtt_server", "SERVER", mqtt_server, 40);
      WiFiManagerParameter custom_blynk_server_ip("blynk_server_ip", "blynk server ip", blynk_server_ip, 40);
      WiFiManagerParameter custom_mqtt_username("mqtt_username", "USERNAME", mqtt_username, 20);
      WiFiManagerParameter custom_blynk_auth("blynk_auth", "BLYNK AUTH", blynk_auth, 34);
      WiFiManagerParameter custom_email_id("email_id", "Email ID", email_id, 100);
      WiFiManagerParameter custom_device_id("device_id", "Device ID", device_id, 5);
      //WiFiManagerParameter custom_serial_number("serial_number", "Serial Number", serial_number, 9);

      WiFiManager wifiManager;

      //set config save notify callback
      wifiManager.setSaveConfigCallback(saveConfigCallback);

      //add all your parameters here
      wifiManager.addParameter(&custom_mqtt_server);
      wifiManager.addParameter(&custom_blynk_server_ip);
      wifiManager.addParameter(&custom_mqtt_username);
      wifiManager.addParameter(&custom_blynk_auth);
      wifiManager.addParameter(&custom_email_id);
      wifiManager.addParameter(&custom_device_id);
      //wifiManager.addParameter(&custom_serial_number);

      /*
      //Admin Panel
      std::vector<const char *> menu = {"wifi","param","restart"};
      wifiManager.setMenu(menu);
    */

      // Portal shuts down in 30 seconds
      //      wifiManager.setTimeout(90);

      // If wifi not found, search for 30 seconds and move to offline mode.
      if (!wifiManager.autoConnect(ap_ssid, ap_pass)) internet_status = false;

      //if you get here you have connected to the WiFi

      //read updated parameters
      strcpy(mqtt_server, custom_mqtt_server.getValue());
      strcpy(blynk_server_ip, custom_blynk_server_ip.getValue());
      strcpy(mqtt_username, custom_mqtt_username.getValue());
      strcpy(blynk_auth, custom_blynk_auth.getValue());
      strcpy(email_id, custom_email_id.getValue());
      strcpy(device_id, custom_device_id.getValue());
      //strcpy(serial_number, custom_serial_number.getValue());

      write_internal_memory();
    }
  }



// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// %    Update relays from data coming from Blynk      %
// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%


void blynk_init() {
  Blynk.config(blynk_auth, blynk_server_ip, port);
}


BLYNK_CONNECTED() {
  // Request Blynk server to re-send latest values for all pins
  Blynk.syncAll();
 
  // You can also update individual virtual pins like this:
  //Blynk.syncVirtual(V0, V2);
}

BLYNK_WRITE(V1)
{
  int pinValue = param.asInt(); // assigning incoming value from pin V1 to a variable
  if (pinValue == 1){
      digitalWrite(R1, LOW);
      relay_1_state = 0;
      if(internet_status) MQTT_WRITE(1, "1");
      if (SIRI_FEATURES){
        cha_switch1_on.value.bool_value = 1;
        homekit_characteristic_notify(&cha_switch1_on, cha_switch1_on.value);
      }
  }
  else{
      digitalWrite(R1, HIGH);
      relay_1_state = 1;
      if(internet_status) MQTT_WRITE(1, "0");
      if (SIRI_FEATURES){
        cha_switch1_on.value.bool_value = 0;
        homekit_characteristic_notify(&cha_switch1_on, cha_switch1_on.value);
      }
  }
}

BLYNK_WRITE(V2)
{
  int pinValue = param.asInt(); // assigning incoming value from pin V2 to a variable
  if (pinValue == 1){
      digitalWrite(R2, LOW);
      relay_2_state = 0;
      if(internet_status) MQTT_WRITE(2, "1");
      if (SIRI_FEATURES){
        cha_switch2_on.value.bool_value = 1;
        homekit_characteristic_notify(&cha_switch2_on, cha_switch2_on.value);
      }
  }
  else{
      digitalWrite(R2, HIGH);
      relay_2_state = 1;
      if(internet_status) MQTT_WRITE(2, "0");
      if (SIRI_FEATURES){
        cha_switch2_on.value.bool_value = 0;
        homekit_characteristic_notify(&cha_switch2_on, cha_switch2_on.value);
      }
  }
}

BLYNK_WRITE(V3)
{
  int pinValue = param.asInt(); // assigning incoming value from pin V3 to a variable
  if (pinValue == 1){
      digitalWrite(R3, LOW);
      relay_3_state = 0;
      if(internet_status) MQTT_WRITE(3, "1");
      if (SIRI_FEATURES){
        cha_switch3_on.value.bool_value = 1;
        homekit_characteristic_notify(&cha_switch3_on, cha_switch3_on.value);
      }
  }
  else{
      digitalWrite(R3, HIGH);
      relay_3_state = 1;
      if(internet_status) MQTT_WRITE(3, "0");
      if (SIRI_FEATURES){
        cha_switch3_on.value.bool_value = 0;
        homekit_characteristic_notify(&cha_switch3_on, cha_switch3_on.value);
      }
  }
}

BLYNK_WRITE(V4)
{
  int pinValue = param.asInt(); // assigning incoming value from pin V4 to a variable0.00
  if (pinValue == 0){
      tarBrightness = 0;
      state = 0;
      fan_state = 0;
      relay_4_state = 1;
      digitalWrite(R4, HIGH);
      Blynk.virtualWrite(V5, 0);
      if(internet_status) MQTT_WRITE(4, "0");
      if (SIRI_FEATURES){
        cha_fan_active.value.int_value = 0;
        homekit_characteristic_notify(&cha_fan_active, cha_fan_active.value);  
      }    
    }else{
      relay_4_state = 0;
      digitalWrite(R4, LOW);
      tarBrightness = 255;
      state = 1;
      fan_state = 1;
      Blynk.virtualWrite(V5, 100);      
      if(internet_status) MQTT_WRITE(4, "100");
      if (SIRI_FEATURES){
        cha_fan_active.value.int_value = 1;
        cha_fan_rotation_speed.value.float_value = 100.0;
        homekit_characteristic_notify(&cha_fan_active, cha_fan_active.value);
        homekit_characteristic_notify(&cha_fan_rotation_speed, cha_fan_rotation_speed.value); 
      }
    }
}

BLYNK_WRITE(V5)   // function to assign value to variable Slider_Value whenever slider changes position
{
  int blynk_slider_value = param.asInt(); // assigning incoming value from pin V1 to a variable
  if (blynk_slider_value == 0){
      Blynk.virtualWrite(V4, 0);    
      tarBrightness = 0;
      state = 0;
      fan_state = 0; 
      relay_4_state = 1;
      digitalWrite(R4, HIGH);  
      if(internet_status) MQTT_WRITE(4, "0"); 
      if (SIRI_FEATURES){
        cha_fan_active.value.int_value = 0;
        homekit_characteristic_notify(&cha_fan_active, cha_fan_active.value); 
      } 
  } 
  else{
      Blynk.virtualWrite(V4, 1);
      relay_4_state = 0;
      digitalWrite(R4, LOW);
      tarBrightness = map(blynk_slider_value,0,100,0,255);
      state = 1;
      fan_state = 1;
      if (SIRI_FEATURES){
        float blynk_slider_float = (float) blynk_slider_value;
        cha_fan_active.value.int_value = 1;
        cha_fan_rotation_speed.value.float_value = blynk_slider_float;
        homekit_characteristic_notify(&cha_fan_active, cha_fan_active.value);
        homekit_characteristic_notify(&cha_fan_rotation_speed, cha_fan_rotation_speed.value);
      }
      if(internet_status){
          char speed_str[8];
          itoa(blynk_slider_value, speed_str, 10);
          MQTT_WRITE(4, speed_str);   
      }
  }
}

// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// %    Initialise relays with respect to switches     %
// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
void relays_init(){
  if(digitalRead(S1) == LOW){
    digitalWrite(R1, LOW); 
    relay_1_state = 0;
  }else{
    digitalWrite(R1, HIGH); 
    relay_1_state = 1;
  }

  if(digitalRead(S2) == LOW){
    digitalWrite(R2, LOW); 
    relay_2_state = 0;
  }else{
    digitalWrite(R2, HIGH); 
    relay_2_state = 1;
  }

  if(digitalRead(S3) == LOW){
    digitalWrite(R3, LOW); 
    relay_3_state = 0;
  }else{
    digitalWrite(R3, HIGH); 
    relay_3_state = 1;
  }

  if(digitalRead(S4) == LOW){
    relay_4_state = 0;
    digitalWrite(R4, LOW);
    fan_state = 1;
    state = 1;
    tarBrightness = 255;
  }else{
    fan_state = 0;
    state = 0;
    tarBrightness = 0;
    relay_4_state = 1;
    digitalWrite(R4, HIGH);
  }
}

ICACHE_RAM_ATTR void setup()
{ delay(1000);
  pinMode(R1, OUTPUT);
  pinMode(R2, OUTPUT);
  pinMode(R3, OUTPUT);
  pinMode(R4, OUTPUT);
  pinMode(pwmPin, OUTPUT);

  pinMode(S1, INPUT_PULLUP);
  pinMode(S2, INPUT_PULLUP); 
  pinMode(S3, INPUT_PULLUP);
  pinMode(S4, INPUT_PULLUP);
  
  pinMode(zcPin, INPUT_PULLUP);
  attachInterrupt(zcPin, zcDetectISR, RISING);
  hw_timer_init(NMI_SOURCE, 0);
  hw_timer_set_func(dimTimerISR);

  relays_init();
  switches_init();
  delay(1000);
  wifimanager_init();
  generate_mqtt_topics();
  if (BLYNK_FEATURES) blynk_init();
  if (SIRI_FEATURES) my_homekit_setup();
  mqtt_init();
  ping_init();
}

void loop()
{  
    manual_switches();
    if (SIRI_FEATURES) arduino_homekit_loop();
    
    if(internet_status){
      if (BLYNK_FEATURES) Blynk.run();
       if (!client.connected())
          reconnect();        
        else{ 
          client.loop();
        }
     }
}
