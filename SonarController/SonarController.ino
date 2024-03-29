#include "Arduino.h"
#include "KiwiMQTT.h"
#include "KiwiSecrets.h"
#include "KiwiSonic.h"
#include "KiwiTemp.h"
#include "KiwiServo.h"
#include "math.h"


KiwiSonic ultrasonicSensor1(D7,350);
KiwiSonic ultrasonicSensor2(D8,350);
KiwiTemp tempSensor(A0);
KiwiServo servo(D2);
KiwiMQTT wireless(ssid,secret);

const int STANDARD_DELAY=100; //Used to delay certain operations for readability or stability.
const int SOUNDWAVE_DISSIPATION_DELAY=250; //Used to prevent accumulation of sound waves when used in smaller rooms.
const long UPDATE_INTERVAL=5000; //How often are we going to fetch new messages from MQTT.
const int MAX_MEASUREMENTS=5; //Maximum measurements

bool servoRun=false;
bool track=false;
bool result=false;
float temperature=0;
int maxRange1=999;
int maxRange2=999;
long lastUpdateTime=0;
int from=0;
int to=180;
void safeDelay(int ms){
  long timeRunning=millis();
   while((millis()-timeRunning)<ms){
    //Perform non-blocking delay.
   }
}

uint8_t STP[3]={83,84,80}; //Stop
uint8_t STR[3]={83,84,82}; //Start
uint8_t SSR[3]={83,83,82}; //Set sector
uint8_t SRR[3]={83,82,82}; //Set range
uint8_t TRK[3]={84,82,75}; //Track
uint8_t SRK[3]={83,82,75}; //Stop Track
int sonar1Measurement[5]={-1,-1,-1,-1,-1};
int sonar2Measurement[5]={-1,-1,-1,-1,-1};
int degrees[5]={-1,-1,-1,-1,-1};
int measurementsMade=0;

void callback(char* topic, uint8_t* data, unsigned int msglen){
  //We know that the msg header is the first three bytes.
  //We loop through the first three bytes of the message and copy it to the messageHeader holder variable.
    uint8_t msgHeader[3]; //Message header has length 3.
    for(int i=0;i<3;i++){
      msgHeader[i]=(uint8_t) data[i]; //Retrieving the message header of the command.
    }
     /*
     * We use the method call: memcmp() to compare if the receive message header
     * matches any of the defined commands.
     **/
  if (memcmp(msgHeader,STP,sizeof(msgHeader))==0){
        Serial.println("RCV: Stop Sonar");        
        servoRun=false;
        wireless.publish("RCVD");

        Serial.println("Stopping sonar");
    } else if(memcmp(msgHeader,STR,sizeof(msgHeader))==0){
        Serial.println("RCV: Start Sonar");        
        servoRun=true;
        wireless.publish("RCVD");

        Serial.println("Starting sonar");
    } else if(memcmp(msgHeader,TRK,sizeof(msgHeader))==0){
        Serial.println("RCV: Tracking mode");        
        track=true;
        wireless.publish("RCVD");

        Serial.println("Starting sonar");
    }else if(memcmp(msgHeader,SRK,sizeof(msgHeader))==0){
        Serial.println("RCV: Stop Tracking mode");        
        track=false;
        wireless.publish("RCVD");

        Serial.println("Starting sonar");
    } else if(memcmp(msgHeader,SSR,sizeof(msgHeader))==0){
        Serial.println("RCV: Go To Sector"); 
        char charFrom[3];
        char charTo[3]; 

        int c=0;
        for(int i=3;i<6;i++){
          Serial.println((char) data[i]);

          charFrom[c]=(char) (data[i]);
          c++;
        }
        c=0;
        for(int i=6;i<9;i++){
          charTo[c]=(char) (data[i]);
          c++;
        }
        Serial.println("Sector Set");
        String strFrom=String(charFrom);
        String strTo=String(charTo);
        from= (strFrom).toInt();
        to=(strTo).toInt();
        Serial.println(from);
        Serial.println(to);
        wireless.publish("RCVD");

    }else if(memcmp(msgHeader,SRR,sizeof(msgHeader))==0){
        Serial.println("RCV: Set Range"); 
        char charMaxRange1[3];
        char charMaxRange2[3]; 

        int c=0;
        for(int i=3;i<6;i++){
          charMaxRange1[c]=(char) (data[i]);
          c++;
        }
        c=0;
        for(int i=6;i<9;i++){

          charMaxRange2[c]=(char) (data[i]);
          c++;
        }
        Serial.println("Range Set");

        String strMaxRange1=String(charMaxRange1);
        String strMaxRange2=String(charMaxRange2);
        maxRange1= (strMaxRange1).toInt();
        maxRange2=(strMaxRange2).toInt();
        Serial.println(strMaxRange1);
        Serial.println(strMaxRange2);        
        wireless.publish("RCVD");

    }

}

void setup(){
  //Delaying for 5 seconds so that some older systems have time to register the initial messages.
  //Connecting to WiFi
  //Awaiting a successful WiFi connection
Serial.begin(115200);
Serial.println("Measuring temperature");
temperature=tempSensor.measureTemp();

Serial.println(temperature);
  wireless.init();

while(wireless.getWiFiStatus()!=WL_CONNECTED){
  Serial.println("Initial connect failed... Attempting reconnect.");
  wireless.init();
  safeDelay(1000);
}
Serial.println("Wifi set.");
wireless.setServer("mqtt-http.jnsl.tk", 1883);
Serial.println("Server set");
wireless.setCallback(callback);
Serial.println("Callback set");
}

void sendBundle(){
  /*
  * Format buffer segment in accordance with specified format.
  */
  String bundle=String("M");
  for(int i=0;i<MAX_MEASUREMENTS;i++){
    bundle=bundle+String("/")+sonar1Measurement[i]+String("/")+sonar2Measurement[i]+String("/")+degrees[i];
  }
  wireless.publish(bundle);
}

bool record(int degree){
  if(measurementsMade>=MAX_MEASUREMENTS){
    /*
    * We have reached the maximum number of measurements for this buffer segment, send it over MQTT.
    */
    sendBundle();
    measurementsMade=0;    
  }
  
  int measure1=ultrasonicSensor1.calculateDistance(temperature);
  safeDelay(SOUNDWAVE_DISSIPATION_DELAY);
  int measure2=ultrasonicSensor2.calculateDistance(temperature);
  int reportedMeasurement1=measure1<maxRange1?measure1:-1;
  int reportedMeasurement2=measure2<maxRange2?measure2:-1;
  sonar1Measurement[measurementsMade]=reportedMeasurement1;
  sonar2Measurement[measurementsMade]=reportedMeasurement2;
  degrees[measurementsMade]=degree;
  measurementsMade=measurementsMade+1;
  if(track && reportedMeasurement2>-1){
    return true;
  } 
  return false;
}

void spin(){
  /*
  * This method is responsible for moving the ultrasonic sensor and tracking objects if tracking mode is enabled.
  */
  if(servoRun){
  for(int i=from;i<to;i+=15){
    servo.goTo(i);
    if(record(i)){
      if(track){
        /*
        * If track mode is activated we enter the track mode.
        */
        int degree=i;
        bool keepTracking=true;
        while(keepTracking){
          safeDelay(STANDARD_DELAY);
          wireless.publish("TRK");
          wireless.sweep();          
          bool res=record(degree); //Checking if anything is in our vicinity at the current degree.
          if(!res){ //If nothing is seen we need to check if there is something to the left.
            if((degree-15)>0){ //Ensure we are not going to break the servo.
              servo.goTo(degree-15);
              if(record(degree-15)){ //Record at new degree 
                  degree=degree-15; //If we found something at degree-15, let this be the new value of degree.
              } else if((degree+15)<180) { //If we are going to break the servo by going more left, we should check if we can go right.
              servo.goTo(degree+15);
                if(record(degree+15)){
                  degree=degree+15; //If we found something at degree+15, let this be the new value of degree.
                } else {
                    keepTracking=false; //If both directions were checked and nothing was found, drop the object.
                }
              }
            } else if((degree+15)<180) {
              /*
              * Same process as above but inverted.
              */
              servo.goTo(degree+15);
                if(record(degree+15)){
                  degree=degree+15;
                } else if((degree-15)>0){
              servo.goTo(degree-15);
                  if(record(degree-15)){
                    degree=degree-15;
                  } else {
                    keepTracking=false;
                  }
                }
            }
          }
        }
  

        }
   
      }
    }
    safeDelay(STANDARD_DELAY);
      for(int i=to;i>from;i-=15){
    servo.goTo(i);
    if(record(i)){
      if(track){
        /*
        * If track mode is activated we enter the track mode.
        */
        int degree=i;
        bool keepTracking=true;
        while(keepTracking){
          safeDelay(STANDARD_DELAY);
          wireless.publish("TRK");
          wireless.sweep();          
          bool res=record(degree); //Checking if anything is in our vicinity at the current degree.
          if(!res){ //If nothing is seen we need to check if there is something to the left.
            if((degree-15)>0){ //Ensure we are not going to break the servo.
              servo.goTo(degree-15);
              if(record(degree-15)){ //Record at new degree 
                  degree=degree-15; //If we found something at degree-15, let this be the new value of degree.
              } else if((degree+15)<180) { //If we are going to break the servo by going more left, we should check if we can go right.
              servo.goTo(degree+15);
                if(record(degree+15)){
                  degree=degree+15; //If we found something at degree+15, let this be the new value of degree.
                } else {
                    keepTracking=false; //If both directions were checked and nothing was found, drop the object.
                }
              }
            } else if((degree+15)<180) {
              /*
              * Same process as above but inverted.
              */
              servo.goTo(degree+15);
                if(record(degree+15)){
                  degree=degree+15;
                } else if((degree-15)>0){
              servo.goTo(degree-15);
                  if(record(degree-15)){
                    degree=degree-15;
                  } else {
                    keepTracking=false;
                  }
                }
            }
          }
        }
  

        }
   
      }
    safeDelay(STANDARD_DELAY);
    }
  }

  }



void loop(){
  if(!wireless.getBrokerStatus()){
    Serial.println("Broker disconnected");
    wireless.connect();
    Serial.println("Connected");
  } else {
    long currentTime=millis(); //Retrieving the number ms the Wio terminal has been alive.
    if((currentTime-lastUpdateTime)>=UPDATE_INTERVAL){
      /*
      * This part can get stuck, this is why have enabled logging, for debugging purposes in case
      * the sonar gets stuck. 
      **/
      Serial.println("Sweep");
      lastUpdateTime=currentTime; //Updating the variable that keeps track how often we fetch new messages.
      result=wireless.sweep(); //Fetching new messages from MQTT broker.
      wireless.publish("CNCTD");  //Short for connected. We send this to let front-end know we are connected and subcribed to topic.
      Serial.println(result);
      if(result==1){
          spin(); //Perform rotation in accorance to the specified sector.
      }
    }
  }
}
