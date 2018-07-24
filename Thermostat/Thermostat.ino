/*Autore: Caliman Alberto
 * Titolo: Thermostat
 * 
 * 
 * Description: this project can read temperatur and humidity inside your home and push data on firebase, finaly you can control more thermostats (setting themrostats id) and switch on/off manualy or automatic (set temperature) your thermostats
 *              
 * 
 * IO PORT:
 * Relè --> D8
 * dht  --> D4
 * DS18B20 -->D1
 * Backlight --> D0
 * SDA --> D3
 * SCL --> D5
*/

#include <ESP8266WiFi.h>
#include <FirebaseArduino.h>
#include <dht11.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include "Gsender.h"
#include "Credential.h"

//Variables
boolean automan,boiler;
const int rele=D8;
const int lcdlight=16;
const int inPin = D2;
int hout,h,hin,hold=0;
String message,stateRadiators,stateBoiler;
int from=0,to=0;
int m=0;
float tin=0.0,told=0.0,settemp=0.0,tout=0.0;
bool displayState=0;
int previous=LOW;
bool releold=true;
float *reading;
bool flagemail=true;

//GENERAL SETUP
const int hysteresis=1; //1°C
const int BACKLIGHT=125;
const float VERSION=2.0;

//PIN SETUP
#define DHT11_PIN D4 //temperature sensor dht
#define ONE_WIRE_BUS D1 //temperature sensor dallas

//---OBJECT---
dht11 DHT;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
Gsender *gsender = Gsender::Instance();    //create gsender object to send email
HTTPClient http;  //Declare an object of class HTTPClient
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
LiquidCrystal_I2C lcd(0x3f, 16, 2); //lcd setup



void setup() {
  //Serial.begin(9600); //run serial
  pinMode(rele,OUTPUT); 
  pinMode(lcdlight,OUTPUT);
  pinMode(A0,INPUT);
  Wire.begin(D3, D5); //set display connection
  sensors.begin(); ////Initialize dallas sensor temperature
  lcd.begin();//Initialize the LCD
  //analogWrite(lcdlight,BACKLIGHT); //if you want set backlight 
  lcd.home();
  
  // connect to wifi.
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lcd.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    lcd.print(".");
    delay(500);
  }

  //SET OTA PARAMETERS
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("MyThermostat");
  //OTA functions
   ArduinoOTA.onStart([]() {
    lcd.clear();
    lcd.home();
    lcd.print("New update");
    delay(1000);
    lcd.clear();
    lcd.print("Progress:");
  });
  ArduinoOTA.onEnd([]() {
    lcd.clear();
    lcd.home();
    lcd.print("End update!");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    lcd.print(progress/(total/100));
    lcd.print("%");   
    lcd.setCursor(9,0); //to update every time value of percent in the display
  });
  ArduinoOTA.onError([](ota_error_t message) {
    lcd.clear();
    lcd.printf("message[%u]: ", message);
    if (message == OTA_AUTH_ERROR) PrintError("Auth Failed");
    else if (message == OTA_BEGIN_ERROR) PrintError("Begin Failed");
    else if (message == OTA_CONNECT_ERROR) PrintError("Connect Failed");
    else if (message == OTA_RECEIVE_ERROR) PrintError("Receive Failed");
    else if (message == OTA_END_ERROR) PrintError("End Failed");
  });
  ArduinoOTA.begin();
  
  
  setupVisualization();
  lcd.print("Set Paramas");
  setupFirebase(FIREBASE_HOST,FIREBASE_AUTH,TOKENTERM);
  Weather(KEY,COUNTRY,STATE,UNITS);
  lcd.print(".");
  digitalWrite(rele,false);
  lcd.print(".");
  timeClient.begin();
  lcd.print(".");
  lcd.clear(); 
  //end initialization compononents
}


void loop() {
Deicing:
  if(WiFi.status() != WL_CONNECTED){
     PrintError("disconnect");
     message="NC";
     //Serial.println("disconnected");
     WiFi.disconnect();
     WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
     while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            //Serial.print(".");
     }
     message="CN";
     SendEmail("Allright",flagemail);
     flagemail=false;
  }else{
    flagemail=true;
  }

  ArduinoOTA.handle(); //for new ota update
  timeClient.update();//get time
  h=UTC+timeClient.getHours();
  m=timeClient.getMinutes();
  
  if(m==0 || m==30){
    Weather(KEY,COUNTRY,STATE,UNITS);
  }

   //read data by firebase
   automan=Firebase.getBool(TOKENTERM+"UserOptions/automan");
   boiler=Firebase.getBool(TOKENTERM+"UserOptions/Boiler");
   settemp=Firebase.getFloat(TOKENTERM+"UserOptions/SetTemp");
   from=Firebase.getInt(TOKENTERM+"UserOptions/From");
   to=Firebase.getInt(TOKENTERM+"UserOptions/To");

    
   reading=readData();
   tin=reading[0];
   hin=(int)reading[1];
   tin=18.5;
  
   if(tin<5){
     antiIce("Deicing On",flagemail);
     goto Deicing;
   }else{
     flagemail=true;
   }
   
  //decode data
  if(tin!=told){
      told=tin;
      Firebase.setFloat(TOKENTERM+"Params/INT/Temp",tin);
  }
  
  if(hin!=hold){
      hold=hin;
      Firebase.setFloat(TOKENTERM+"Params/INT/Hum",hin);
  }
  
  if(automan){
      stateRadiators = "AUTO";
      Automatic(settemp,tin,hysteresis,from,to);
  }else{
      stateRadiators="MANU";
      digitalWrite(rele,boiler);
   }

  if(boiler){
    stateBoiler="ON"; 
  }else{
    stateBoiler="OFF";
   }

  if(message!="CN"){
        PrintError(message);
    }else{
      if(displayState){
        PrintExt(tout,hout,settemp,stateRadiators,stateBoiler,message);
        displayState=!displayState;
      }else
      {
        PrintInt(tin,hin,settemp,stateRadiators,stateBoiler,message);
        displayState=!displayState;
      }
     }
}
   

//--------------------------------FUNCTIONS-----------------------------------------------------------------
/*
FIX:return a state of rele and boiler
Control temperature inside home automatic if temperature inside is less than set temperature-hysteresis turn on boiler, and if worktime is out of range automatic don't work
@settemp value of temperature you want inside your home
@tin value of temperature inside your home
@histeresis to creare an histerisis
@From work time from
@To work time to
*/
void Automatic(float settemp,float tin,float hysteresis,int from,int to){
  if(h>=from && h<=to){ //time when thermostat works
    float point = settemp-hysteresis; //hysteresis;
    if(tin<settemp){
      if(tin<point){
        Firebase.setBool(TOKENTERM+"UserOptions/Boiler",true);
        digitalWrite(rele,true);
        stateBoiler="ON ";
      }
    }
    else{    
    Firebase.setBool(TOKENTERM+"UserOptions/Boiler",false);
    digitalWrite(rele,false);
    stateBoiler="OFF";
    }
  }
  else{
    Firebase.setBool(TOKENTERM+"UserOptions/Boiler",false);
    digitalWrite(rele,false);
    stateBoiler="OFF";
  }
}


/*
PRINT on display information about outside condition
@Temp value of temperature outsite
@Hum value of humidity outside 0=<Hum<=100
@Settemp float value >0.0
@StateRadiator string that rapresent the state of radiator, it must be AUTO or MANU
@stateRadiator string that rapresent the state of boiler, it must be ON or OFF
@Message if message is CN (Connect) thermostat work correctly if you want print a messagge error or information set messagge with another values
*/
void PrintExt(float Temp,int Hum,float SetTemp,String StateRadiator,String StateBoiler,String Message){
  lcd.noBlink();
  lcd.home();
  String lineone="";
  String linetwo="";
  String stateBoiler="ON";
  if(Message=="CN"){
    lcd.clear();
    if(StateBoiler=="ON"){

      if(settemp>10){
        lineone.concat(StateBoiler+"  ");lineone.concat("|");lineone.concat(StateRadiator);lineone.concat("  ");
      }else{
        lineone.concat(StateBoiler+"  ");lineone.concat("|");lineone.concat(StateRadiator);lineone.concat("  ");
      }
    }else{
      if(settemp>10){
        lineone.concat(StateBoiler+" ");lineone.concat("|");lineone.concat(StateRadiator);lineone.concat("  ");
      }else{
        lineone.concat(StateBoiler+" ");lineone.concat("|");lineone.concat(StateRadiator);lineone.concat("  ");
      }
    }
    if(Hum==100){
      if(Temp<0 && Temp>=-9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("EXT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.print(Hum);lcd.write('%');lcd.setCursor(11,1);lcd.print(Temp,1);lcd.print((char)223);
      }else if(Temp>=0 && Temp<=9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("EXT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.print(Hum);lcd.write('%');lcd.setCursor(12,1);lcd.print(Temp,1);lcd.print((char)223); 
      }else if(Temp<0 && Temp<-9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("EXT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.print(Hum);lcd.write('%');lcd.setCursor(10,1);lcd.print(Temp,1);lcd.print((char)223);
      }
      else{
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("EXT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.print(Hum);lcd.write('%');lcd.setCursor(11,1);lcd.print(Temp,1);lcd.print((char)223);
       }
    }else if(Hum<10){
      if(Temp<0 && Temp>=-9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("EXT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.setCursor(5,1);lcd.print(Hum);lcd.write('%');lcd.setCursor(11,1);lcd.print(Temp,1);lcd.print((char)223);
      }else if(Temp>=0 && Temp<=9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("EXT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.setCursor(5,1);lcd.print(Hum);lcd.write('%');lcd.setCursor(12,1);lcd.print(Temp,1);lcd.print((char)223); 
      }else if(Temp<0 && Temp<-9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("EXT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.setCursor(5,1);lcd.print(Hum);lcd.write('%');lcd.setCursor(10,1);lcd.print(Temp,1);lcd.print((char)223);
      }
      else{
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("EXT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.setCursor(5,1);lcd.print(Hum);lcd.write('%');lcd.setCursor(11,1);lcd.print(Temp,1);lcd.print((char)223);
       }
    }else{
      if(Temp<0 && Temp>=-9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("EXT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.setCursor(5,1);lcd.print(Hum);lcd.write('%');lcd.setCursor(11,1);lcd.print(Temp,1);lcd.print((char)223);
      }else if(Temp>=0 && Temp<=9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("EXT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.setCursor(5,1);lcd.print(Hum);lcd.write('%');lcd.setCursor(12,1);lcd.print(Temp,1);lcd.print((char)223); 
      }else if(Temp<0 && Temp<-9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("EXT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.setCursor(5,1);lcd.print(Hum);lcd.write('%');lcd.setCursor(10,1);lcd.print(Temp,1);lcd.print((char)223);
      }
      else{
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("EXT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.setCursor(5,1);lcd.print(Hum);lcd.write('%');lcd.setCursor(11,1);lcd.print(Temp,1);lcd.print((char)223);
       }
    }
  }else{
    PrintError(Message);   
  }
}


/*
PRINT on display information about inside condition
@Temp value of temperature inside
@Hum value of humidity inside 0=<Hum<=100
@Settemp float value >0.0
@StateRadiator string that rapresent the state of radiator, it must be AUTO or MANU
@stateRadiator string that rapresent the state of boiler, it must be ON or OFF
@Message if message is CN (Connect) thermostat work correctly if you want print a messagge error or information set messagge with another values
*/
void PrintInt(float Temp,int Hum,float SetTemp,String StateRadiator,String StateBoiler,String Message){
  lcd.noBlink();
  lcd.home();
  String lineone="";
  String linetwo="";
  if(Message=="CN"){
    lcd.clear();
    if(StateBoiler=="ON"){
      if(settemp>10){
        lineone.concat(StateBoiler+"  ");lineone.concat("|");lineone.concat(StateRadiator);lineone.concat("  ");
      }else{
        lineone.concat(StateBoiler+"  ");lineone.concat("|");lineone.concat(StateRadiator);lineone.concat("  ");
      }
    }else{
      if(settemp>10){
        lineone.concat(StateBoiler+" ");lineone.concat("|");lineone.concat(StateRadiator);lineone.concat("  ");
      }else{
        lineone.concat(StateBoiler+" ");lineone.concat("|");lineone.concat(StateRadiator);lineone.concat("  ");
      }
    }
    if(Hum==100){
      if(Temp<0 && Temp>=-9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("INT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.print(Hum);lcd.write('%');lcd.setCursor(11,1);lcd.print(Temp,1);lcd.print((char)223);
      }else if(Temp>=0 && Temp<=9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("INT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.print(Hum);lcd.write('%');lcd.setCursor(12,1);lcd.print(Temp,1);lcd.print((char)223); 
      }else if(Temp<0 && Temp<-9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("INT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.print(Hum);lcd.write('%');lcd.setCursor(10,1);lcd.print(Temp,1);lcd.print((char)223);
      }
      else{
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("INT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.print(Hum);lcd.write('%');lcd.setCursor(11,1);lcd.print(Temp,1);lcd.print((char)223);
       }
    }else if(Hum<10){
      if(Temp<0 && Temp>=-9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("INT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.setCursor(5,1);lcd.print(Hum);lcd.write('%');lcd.setCursor(11,1);lcd.print(Temp,1);lcd.print((char)223);
      }else if(Temp>=0 && Temp<=9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("INT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.setCursor(5,1);lcd.print(Hum);lcd.write('%');lcd.setCursor(12,1);lcd.print(Temp,1);lcd.print((char)223); 
      }else if(Temp<0 && Temp<-9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("INT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.setCursor(5,1);lcd.print(Hum);lcd.write('%');lcd.setCursor(10,1);lcd.print(Temp,1);lcd.print((char)223);
      }
      else{
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("INT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.setCursor(5,1);lcd.print(Hum);lcd.write('%');lcd.setCursor(11,1);lcd.print(Temp,1);lcd.print((char)223);
       }
    }else{
      if(Temp<0 && Temp>=-9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("INT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.setCursor(6,1);lcd.print(Hum);lcd.write('%');lcd.setCursor(11,1);lcd.print(Temp,1);lcd.print((char)223);
      }else if(Temp>=0 && Temp<=9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("INT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.setCursor(5,1);lcd.print(Hum);lcd.write('%');lcd.setCursor(12,1);lcd.print(Temp,1);lcd.print((char)223); 
      }else if(Temp<0 && Temp<-9){
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("INT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.setCursor(5,1);lcd.print(Hum);lcd.write('%');lcd.setCursor(10,1);lcd.print(Temp,1);lcd.print((char)223);
      }
      else{
        lcd.print(lineone);lcd.print(SetTemp,1);lcd.print((char)223);
        lcd.setCursor(0,1);
        linetwo.concat("INT ");linetwo.concat("|");lcd.print(linetwo);
        lcd.setCursor(5,1);lcd.print(Hum);lcd.write('%');lcd.setCursor(11,1);lcd.print(Temp,1);lcd.print((char)223);
       }
    }
  }else{
    PrintError(Message);   
  }
}

/*
Print message error on display
@Message message that you want print on display to notify an error
*/
void PrintError(String Message){
  lcd.noBlink();
  lcd.setCursor(0,1);
  lcd.print("                ");
  lcd.setCursor(0,1);
  lcd.print(Message);
}

/*
Send an email with your message at every contact you insert in Email array, set flag true to send message if flag is false message won't be send
@msg message you want send
@flag to decide if you want send message
*/
void SendEmail(String msg,bool flag){
  if(flag){
  int dimension = (sizeof(Email)) / (sizeof(Email[0]));
    String message = msg;
    for(int i=0;i<dimension;i++){
      if(gsender->Subject(subject)->Send(Email[i], message)){
        delay(100);
      }else{
        //Serial.println(gsender->getError());
      }
    }
  }
  flag=false;
}

/*
FIX: return variables and don't set global variables
Set variables tout and hout with value of temperature and humidity by your city
@key your openwhathermap key
@country your country
@state your state
@units string must be "imperial" or "metric"
*/
void Weather(String key,String country,String state,String units){
 String url="http://api.openweathermap.org/data/2.5/weather?q="+country+","+state+"&appid="+key+"&units="+units;
 http.begin(url);  //Specify request destination
 int httpCode = http.GET(); //Send the request
 if(httpCode>0){ //Check the returning code
      String payload = http.getString();   //Get the request response payload
      //Serial.println(payload);                     //Print the response payload
      DynamicJsonBuffer jsonBuffer;
      JsonObject& root = jsonBuffer.parseObject(payload);
    if (!root.success()) {   //Check for errors in parsing
          Serial.println("Parsing failed");
          delay(2000);
          return;
        }
        float temp = root["main"]["temp"];
        float hum = root["main"]["humidity"];
        tout=temp;
        hout=hum;
        Firebase.setFloat(TOKENTERM+"Params/EXT/Temp",temp);
        Firebase.setFloat(TOKENTERM+"Params/EXT/Hum",hum);
        //Serial.println(temp);
        //Serial.println(hum);
    }
  http.end();   //Close connection
}

/*
Return an array in first position you can find temperature values inside your home in second position you find humidity value
*/
float *readData(){
  static float values[1]={};
  //read humidity and temperature
   DHT.read(DHT11_PIN);
   sensors.requestTemperatures();
   values[0] = sensors.getTempCByIndex(0);
   values[1]= DHT.humidity;
   return values;
}

/*
Print on display basic information during setup
@Print: thermostat ip
@Print: title project
@Print: Version
*/
void setupVisualization(){
  lcd.clear(); //clean display
  lcd.print("connected");
  message="CN";  //if thermostat is connect to wifi it's ok!
  lcd.setCursor(0,1);
  lcd.print(WiFi.localIP());
  delay(700);
  lcd.clear();
  lcd.home();
  lcd.print("Smart Thermostat");
  lcd.setCursor(0,1);
  lcd.print("Version: ");
  lcd.print(VERSION,1);
  delay(4000);
  lcd.clear();
}

/*
Setup connection with firebase and set default parameters inside database
@host database address
@auth secret key of database
@token token of my thermostat
*/
void setupFirebase(String host,String auth,String token){
  Firebase.begin(host,auth);//start firebase and time
  lcd.print(".");
  Firebase.setBool(token+"UserOptions/Boiler",false);
  lcd.print(".");
  Firebase.setBool(token+"UserOptions/automan",false);
  lcd.print(".");
}

/*
To prevent tubes ice when temperature inside home is less than 5° switch on boiler
@msg message print and send by email when antiice swtichon
@flag i send only ONE email
*/
void antiIce(String msg,bool flag){
  PrintError(msg);
  SendEmail(msg,flag);
  flagemail=false;
}



