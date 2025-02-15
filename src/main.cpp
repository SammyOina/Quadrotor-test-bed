#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "HX711-multi.h"
// int button; //input free digital pin 
// int buttonState = 0; // 

// Arduino Wire library is required if I2Cdev I2CDEV_ARDUINO_WIRE implementation
// is used in I2Cdev.h
#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
    #include "Wire.h"
#endif

MPU6050 mpu;

//display yaw, pitch and roll values
#define OUTPUT_READABLE_YAWPITCHROLL

//use yaw, pitch and roll values for visualization on Processing
// #define OUTPUT_TEAPOT

//Pins for MPU6050
#define INTERRUPT_PIN 23
#define SDA 21
#define SCL 22

// MPU control/status vars
bool dmpReady = false;  // set true if DMP init was successful
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

// orientation/motion vars
Quaternion q;           // [w, x, y, z]         quaternion container
VectorInt16 aa;         // [x, y, z]            accel sensor measurements
VectorInt16 aaReal;     // [x, y, z]            gravity-free accel sensor measurements
VectorInt16 aaWorld;    // [x, y, z]            world-frame accel sensor measurements
VectorFloat gravity;    // [x, y, z]            gravity vector
float euler[3];         // [psi, theta, phi]    Euler angle container
float ypr[3];           // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector

// packet structure for InvenSense teapot demo
uint8_t teapotPacket[14] = { '$', 0x02, 0,0, 0,0, 0,0, 0,0, 0x00, 0x00, '\r', '\n' };

// Pins to the HX711
#define CLK 5      // clock pin to the HX711
// #define DOUT1 26    // data pin to the first HX711
#define DOUT2 27    // data pin to the second HX711
#define DOUT3 13    // data pin to the third HX711
#define DOUT4 25    // data pin to the fourth HX711
#define DOUT5 12    // data pin to the fifth HX711
// #define DOUT6 14    // data pin to the sixth HX711
#define TARE_TIMEOUT_SECONDS 4

byte DOUTS[4] = {DOUT2, DOUT3, DOUT4, DOUT5};

#define CHANNEL_COUNT sizeof(DOUTS)/sizeof(byte)

long int results[CHANNEL_COUNT];

HX711MULTI scales(CHANNEL_COUNT, DOUTS, CLK);


// ================================================================
// ===               INTERRUPT DETECTION ROUTINE                ===
// ================================================================

volatile bool mpuInterrupt = false;     // indicates whether MPU interrupt pin has gone high
void dmpDataReady() {
    mpuInterrupt = true;
}

// ================================================================
// ===               LOAD CELL TARE CODE                        ===
// ================================================================

void tare() {
    bool tareSuccessful = false;

    unsigned long tareStartTime = millis();
    while (!tareSuccessful && millis()<(tareStartTime+TARE_TIMEOUT_SECONDS*1000)) {
        tareSuccessful = scales.tare(20,10000);  //reject 'tare' if still ringing
    
    Serial.println(tareSuccessful);
  }
}

float calibratingFactor[CHANNEL_COUNT];

void calibrateSequence(void);


// ================================================================
// ===                      INITIAL SETUP                       ===
// ================================================================

void setup() {
    // initialize serial communication
    Serial.begin(115200);
    Serial.flush();
    tare();
    calibrateSequence();
    // pinMode(button,INPUT); // setting up reset pin
    // join I2C bus (I2Cdev library doesn't do this automatically)
    #if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
      Wire.begin(SDA, SCL);
      // Wire.setClock(400000); // 400kHz I2C clock. Comment this line if having compilation difficulties
    #elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
        Fastwire::setup(400, true);
    #endif

    while (!Serial); 
    Serial.println("Waiting for serial communication...");
    
    // initialize device
    Serial.println(F("Initializing I2C devices..."));
    mpu.initialize();
    pinMode(INTERRUPT_PIN, INPUT_PULLUP);

    // verify connection
    Serial.println(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));

    // load and configure the DMP
    Serial.println(F("Initializing DMP..."));
    devStatus = mpu.dmpInitialize();

    // supply your own gyro offsets here, scaled for min sensitivity
    mpu.setXGyroOffset(220);
    mpu.setYGyroOffset(76);
    mpu.setZGyroOffset(-85);
    mpu.setZAccelOffset(1788); // 1688 factory default for my test chip

    // make sure it worked (returns 0 if so)
    if (devStatus == 0) {
        // Calibration Time: generate offsets and calibrate our MPU6050
        mpu.CalibrateAccel(6);
        mpu.CalibrateGyro(6);
        mpu.PrintActiveOffsets();
        // turn on the DMP, now that it's ready
        Serial.println(F("Enabling DMP..."));
        mpu.setDMPEnabled(true);

        // enable Arduino interrupt detection
        Serial.print(F("Enabling interrupt detection (Arduino external interrupt "));
        Serial.print(digitalPinToInterrupt(INTERRUPT_PIN));
        Serial.println(F(")..."));
        attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), dmpDataReady, RISING);
        mpuIntStatus = mpu.getIntStatus();

        // set our DMP Ready flag so the main loop() function knows it's okay to use it
        Serial.println(F("DMP ready! Waiting for first interrupt..."));
        dmpReady = true;

        // get expected DMP packet size for later comparison
        packetSize = mpu.dmpGetFIFOPacketSize();
    } else {
        // ERROR!
        // 1 = initial memory load failed
        // 2 = DMP configuration updates failed
        // (if it's going to break, usually the code will be 1)
        Serial.print(F("DMP Initialization failed (code "));
        Serial.print(devStatus);
        Serial.println(F(")"));
    }
}

void sendRawData() {
    scales.read(results);
    for (int i=0; i<scales.get_count(); ++i) {;
        Serial.print(-results[i]);  
        Serial.print((i!=scales.get_count()-1)?"\t":"\n");
  }  
  Serial.print("calibrated result: ");
  for (size_t i = 0; i < scales.get_count(); i++)
  {
    Serial.print(results[i]/calibratingFactor[i]);
    Serial.print(", ");
  }
  Serial.println();
  
    delay(10);
}

// void sendWeightData() {
//     scales.get_units(weights);
// 	for (int i = 0; i < scales.get_count(); ++i) {
// 		Serial.print(-weights[i]);
// 		Serial.print((i != scales.get_count() - 1) ? "\t" : "\n");
// 	}
// 	delay(10);
// }

// ================================================================
// ===                    MAIN PROGRAM LOOP                     ===
// ================================================================

void loop() {
    sendRawData();
    // buttonState =  digitalRead(button);
    // if(buttonState == HIGH){
    //     scales.tare();
    // }
    scales.tare(); // this is for sending raw data, for where everything else is done in processing

    //on serial data (any data) re-tare
    if (Serial.available()>0) {
        while (Serial.available()) {
            Serial.read();
        }
        tare();
    }
    // if programming failed, don't try to do anything
    if (!dmpReady) return;
    // read a packet from FIFO
    if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) { // Get the Latest packet 
        
        #ifdef OUTPUT_READABLE_YAWPITCHROLL
            // display Euler angles in degrees
            mpu.dmpGetQuaternion(&q, fifoBuffer);
            mpu.dmpGetGravity(&gravity, &q);
            mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
            Serial.print("ypr\t");
            Serial.print(ypr[0] * 180/M_PI);
            Serial.print("\t");
            Serial.print(ypr[1] * 180/M_PI);
            Serial.print("\t");
            Serial.println(ypr[2] * 180/M_PI);
        #endif
    
        #ifdef OUTPUT_TEAPOT
            // display quaternion values in InvenSense Teapot demo format:
            teapotPacket[2] = fifoBuffer[0];
            teapotPacket[3] = fifoBuffer[1];
            teapotPacket[4] = fifoBuffer[4];
            teapotPacket[5] = fifoBuffer[5];
            teapotPacket[6] = fifoBuffer[8];
            teapotPacket[7] = fifoBuffer[9];
            teapotPacket[8] = fifoBuffer[12];
            teapotPacket[9] = fifoBuffer[13];
            Serial.write(teapotPacket, 14);
            teapotPacket[11]++; // packetCount, loops at 0xFF on purpose
        #endif
    }
}

void calibrateSequence(void) {
    Serial.print("load reference weight");
  for (int i=0;i<5;i++){
    Serial.print(".");
    delay(1000);    
  }
  Serial.println();
  
  int sampleCount = 0;
  float sampleSum[4];
  for (int i=0; i < 4; i++){
    sampleSum[i] = 0;
  }
  while(sampleCount != 10){
    if (scales.is_ready()){
        Serial.println("got reading");
        sampleCount++;
        scales.read(results);
        for(int i=0;i <4; i++){
            sampleSum[i] += results[i];
        }
    }
    delay(100);
  }
  float averageReading[4];
  for(int i = 0; i < 4; i++){
    averageReading[i] = sampleSum[i] / 10.0;
  }
  Serial.println("Average Readings");
  for(int i = 0; i < 4; i++){
    Serial.print(i);
    Serial.print(" : ");
    Serial.println(averageReading[i]);
  }

  Serial.print("remove reference weight");
  for (int i=0;i<5;i++){
    Serial.print(".");
    delay(1000);    
  }
  Serial.println();

  Serial.print("input reference weight [g]: ");
  while(Serial.available() == 0) {} 
  float reference_i= Serial.readStringUntil('\n').toFloat();
  Serial.println(reference_i);
  for (size_t i = 0; i < 4; i++)
  {
    calibratingFactor[i] = averageReading[i] / (reference_i / 4);
  }
  
  tare();
  Serial.println("calibration done");
}