#include <Wire.h>
#include <VL53L0X.h>
#include <FastLED.h>
#include <Adafruit_MCP23017.h>

// Sensors
#define AVOID_RANGE       500
#define I2C_SENSOR_SPEED  400000
#define NB_SENSORS        16
#define NB_SAMPLES        5
#define ROBOT_RANGE       1000
#define SENSOR_ADDR       0x29
#define TIMEOUT_SENSOR    500
#define XSHUT_START_GPIO  0
#define LONG_RANGE

VL53L0X sensors[NB_SENSORS];
int     scan[NB_SENSORS];
int     front_zone[]  = {15, 16, 1, 2, 3};
int     back_zone[]   = {7, 8, 9, 10, 11};
int     no_zone[]     = {4, 5, 6, 12, 13, 14};
bool    is_front    = false;
bool    is_back     = false;
bool    is_robot    = false;
int* samples[NB_SAMPLES];
int currentSample = 0;
bool enableSensors = true;

// Led strips
#define BRIGHTNESS        4
#define COLOR_ORDER       GRB
#define LED_PIN           29
#define LED_TYPE          WS2812
#define LED_LOADING_U     5
#define LED_LOADING_DELAY 100

CRGB led_loading_color = 
  //CRGB::Orange;
  CRGB::Blue;
unsigned int led_loading_next = 0;
int led_loading_current = 0;

enum debug_modes {
  DIST,
  ZONES,
  LOADING
};

enum debug_modes debug_mode = LOADING;
float   coef = 255.0 / (ROBOT_RANGE / 2);
CRGB    leds[NB_SENSORS];

// Communication
#define FRONT_GPIO        32
#define BACK_GPIO         31
#define ROBOT_GPIO        30
#define DEBUG_SERIAL

// Remove
Adafruit_MCP23017 mcp_xshut;

void init_sensor(int i) {
  #ifdef DEBUG_SERIAL
    Serial.print("Init sensor nb: ");
    Serial.println(i + 1);
  #endif

  sensors[i].init();
  sensors[i].setTimeout(TIMEOUT_SENSOR);
  sensors[i].setAddress(SENSOR_ADDR + i);

  // Enable Long Range mode
  #ifdef LONG_RANGE
    sensors[i].setSignalRateLimit(0.1);
    sensors[i].setVcselPulsePeriod(VL53L0X::VcselPeriodPreRange, 18);
    sensors[i].setVcselPulsePeriod(VL53L0X::VcselPeriodFinalRange, 14);
  #endif

  // Start continous mode
  sensors[i].startContinuous();
}


void setup() {
  // Init communication
  Wire.setClock(I2C_SENSOR_SPEED);
  Wire.begin();

  #ifdef DEBUG_SERIAL
    Serial.begin(115200);
  #endif

  pinMode(FRONT_GPIO, OUTPUT);
  digitalWrite(FRONT_GPIO, LOW);
  pinMode(BACK_GPIO, OUTPUT);
  digitalWrite(BACK_GPIO, LOW);
  pinMode(ROBOT_GPIO, OUTPUT);
  digitalWrite(ROBOT_GPIO, LOW);

  // Init Led strip
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NB_SENSORS);
  FastLED.setBrightness(BRIGHTNESS);

  // Init XSHUTs
  #ifdef DEBUG_SERIAL
    Serial.println("Init XSHUTs");
  #endif

  // Remove
  mcp_xshut.begin();

  // Set every pin to output
  for (int i = 0; i < 15; ++i)
  {
    // Remove
    mcp_xshut.pinMode(i, OUTPUT);
    mcp_xshut.digitalWrite(i, LOW);

    //pinMode(XSHUT_START_GPIO + i, OUTPUT);
    //digitalWrite(XSHUT_START_GPIO + i, LOW);
    //
  }

  // Init sensors
  for (int i = NB_SENSORS -1; i >= 0; i--) {

    // Remove
    mcp_xshut.digitalWrite(i, HIGH);

    //digitalWrite(XSHUT_START_GPIO + i, HIGH);
    init_sensor(i);
  }

  for(int i = 0; i < NB_SAMPLES; i++) {
    samples[i] = (int*) malloc(sizeof(int)*NB_SENSORS);
    doSampleScan();  
  }

  if(debug_mode == LOADING)
    enableSensors = false;
}

void manage_leds() {
  
  switch(debug_mode) {
    case DIST:
      for (int i = 0; i < NB_SENSORS; i++) {
        if (scan[i] > ROBOT_RANGE)
          leds[i] = CRGB::Black;
        else {
          int r = 255 - scan[i] * coef;
          int g = 255 - abs(ROBOT_RANGE / 2 - scan[i]) * coef;
          int b = 255 - (ROBOT_RANGE - scan[i]) * coef;
          leds[i].red = r >= 0 ? r : 0;
          leds[i].green = g >= 0 ? g : 0;
          leds[i].blue = b >= 0 ? b : 0;
        }
      }
      break;

    case ZONES: 
      if(is_robot) {
        for(unsigned int i = 0; i < sizeof(leds)/sizeof(CRGB); i++)
          leds[i] = CRGB::Orange;
      } else {
        for(unsigned int i = 0; i < sizeof(leds)/sizeof(CRGB); i++)
          leds[i] = CRGB::Green;
      }   
      if(is_front) {
        for(unsigned int i = 0; i < sizeof(front_zone)/sizeof(int); i++)
          leds[front_zone[i]-1] = CRGB::Red;
      }
      if(is_back) {
        for(unsigned int i = 0; i < sizeof(back_zone)/sizeof(int); i++)
          leds[back_zone[i]-1] = CRGB::Red;
      }
      break;

    case LOADING:
      if(millis() > led_loading_next) {
        leds[led_loading_current] = CRGB::Black;
        leds[(led_loading_current + LED_LOADING_U) % NB_SENSORS] = led_loading_color;
        led_loading_current = (led_loading_current + 1) % NB_SENSORS;
        led_loading_next = millis() + LED_LOADING_DELAY;
      }
      break;

    default:
      break;
  }
   
  FastLED.show();
}

inline void updateFlags(int* _sensors, int nbSensors, bool* obstacle, bool* isRobot) {

  for (int index = 0; index < nbSensors; index++) {
    int i = _sensors[index] -1; // Index of the sensor in the sensors array
    if (obstacle && scan[i] <= AVOID_RANGE)
      *obstacle = true;
    if (scan[i] <= ROBOT_RANGE)
      *isRobot = true;
  }
}

void doSampleScan() {
  for (int sensorIndex = 0; sensorIndex < NB_SENSORS; sensorIndex++) {
      
    #ifdef DEBUG_SERIAL
      Serial.print("Sensor nb : ");
      Serial.print(sensorIndex + 1);
      Serial.print(" dist :");
    #endif

    samples[currentSample][sensorIndex] = 
      sensors[sensorIndex].readRangeContinuousMillimeters();

    #ifdef DEBUG_SERIAL
      Serial.print(samples[currentSample][sensorIndex]);
      if (sensors[sensorIndex].timeoutOccurred())
        Serial.print(" TIMEOUT");
      Serial.println();
    #endif
  }

  currentSample = (currentSample + 1)%NB_SAMPLES;
}

void loop() {

  if(!enableSensors) {
    manage_leds();
    return;
  }
  
  #ifdef DEBUG_SERIAL
    Serial.print("---- STARTING SCAN -----\n");
    int start = millis();
  #endif

  doSampleScan();

  #ifdef DEBUG_SERIAL
    Serial.print("---- END SCAN -----\n");
    Serial.print("Elapsed time: ");
    Serial.println(millis() - start);
  #endif

  for(int i = 0; i < NB_SENSORS; i++) {
    scan[i] = 0;
    #ifdef DEBUG_SERIAL
      Serial.print("Sensor id: ");
      Serial.print(i);
      Serial.print("; values:");
    #endif
    for(int j = 0; j < NB_SAMPLES; j++) {
      scan[i] += samples[j][i];
      #ifdef DEBUG_SERIAL
        Serial.print(" ");
        Serial.print(samples[j][i]);
      #endif
    }
    scan[i] /= NB_SAMPLES;
    #ifdef DEBUG_SERIAL
      Serial.print("; Average: ");
      Serial.print(scan[i]);
      Serial.println();
    #endif
  }

  bool _is_front = false;
  bool _is_back = false;
  bool _is_robot = false;

  updateFlags(front_zone, sizeof(front_zone)/sizeof(int), &_is_front, &_is_robot);
  updateFlags(back_zone, sizeof(front_zone)/sizeof(int), &_is_back, &_is_robot);
  updateFlags(no_zone, sizeof(no_zone)/sizeof(int), NULL, &_is_robot);

  is_front = _is_front;
  is_back  = _is_back;
  is_robot = _is_robot;

  digitalWrite(FRONT_GPIO, is_front);
  digitalWrite(BACK_GPIO,  is_back);
  digitalWrite(ROBOT_GPIO, is_robot);

  manage_leds();
}
