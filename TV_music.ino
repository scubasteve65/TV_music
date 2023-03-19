/*
  TV Music

  

 webserver walkthrough from https://randomnerdtutorials.com/esp8266-web-server-spiffs-nodemcu/
   installing LittleFS rather than SPIFFS as SPIFFS is being deprecated  LittleFS-2.6.0
      You need to install the arduino IDE plugin so that you can upload files
      https://github.com/earlephilhower/arduino-esp8266littlefs-plugin/releases
   installing ESPAsyncWebServer from https://github.com/me-no-dev/ESPAsyncWebServer  
   NOTE that getting data from the webserver example came from here:  https://randomnerdtutorials.com/esp8266-nodemcu-wi-fi-manager-asyncwebserver/

*/

// Import required libraries
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>  // to handle discovery of the IP addresses of the fish
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "LittleFS.h"


// attach a button as an interrupt
#define RESETBUTTON D3
void ICACHE_RAM_ATTR resetButtonPressed();
bool bResetButtonPressed = false;
long lastDebounceTime = 0;
long debounceDelay = 1000;  // debounce time.  dont recognize the button push more than once per debounceDelay


#include <ArduinoJson.h>

// UDP
#define UDP_PORT 4210
WiFiUDP UDP;
#define UDP_BUFFER 255
char rcvUDPPacket[UDP_BUFFER];

long heartbeatTimer=0;
#define HEARTBEAT_TIME 10000
bool bFirstHeartbeat = true;
long offsetHeartbeat =0;

IPAddress IP_Parent={0,0,0,0};
bool bHaveIPofParent = false;

//unsigned int lastUDPPacketID = 0;
//unsigned int UDPPacketID = 0;
//unsigned int UDPDropCount = 0;

unsigned int lastCommandPacketID = 0;
unsigned int CommandPacketID = 0;
unsigned int CommandDropCount = 0;

unsigned int lastMusicPacketID = 0;
unsigned int MusicPacketID = 0;
unsigned int MusicDropCount = 0;

bool bUDPState = true;
bool bUDPPreviousState = true;

// UDP to talk to all the fish to send them music data

// IPAddress SendIP(192,168,1,255);
// IPAddress SendIP(192,168,50,255);  // dont do this.  generate the IP address from the local IP + 255
IPAddress SendIP; // build this in setup
unsigned int iUDP_id = 0;

long lastUDPTime = 0;

// web page variables and settings.  SSID and password are stored by the arduino so be careful how you modify .begin() type statements
const char* PARAM_STRING_SSID = "SSID";
const char* PARAM_STRING_PW = "Password";
const char* PARAM_STRING_FISHNAME = "Fishname";
String inputSSID;
String inputPassword;
int failCount = 0;
bool bConnectSuccess;
#define WIFI_FAIL_COUNT 30  // 30 * 1/2 seconds
bool bSetupWifiPostAP;
long lastWifiMilli = 0;
String errorStringFromPrevious;
String stringNetEnum;
bool bDoBlinkSuccess=false;
long lastBlickSuccessMilli = 0;
bool bIsOn = true;
int lastBlinkCount = 0;
long timeNoWifiReboot=0;   


//////////////////////////////////////////////////////////////////
//  OTA (over the air update)

#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>  // OTA library



///////////////////////////////////////////////////////////////////
// FastLED library setup
#include <FastLED.h>
#define DATA_PIN    D5
//#define CLK_PIN   4
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS    594 
CRGB leds[NUM_LEDS];
#define BRIGHTNESS          80
#define FRAMES_PER_SECOND  120

bool bRainbow = false;
uint8_t gHue = 0; // rotating "base color" used by many of the patterns
                          
// Set LED GPIO
#define LEDPIN D4

const char* PARAM_STRING_COMMAND = "command";  // test, get command from controller

bool bGotColor;
long timeDelayToNextChange = 250;
long timeToRandomColor = 0;
bool bNoRandom = false;

void doBlinkSucess();
void checkRandomColorChange();





// Flow music animation
// https://www.youtube.com/watch?v=3PEdup1dybc
// https://github.com/jordanadania/fastled-newFlow
#define ESP8266

bool bIsMusic = false;
#define MILLI_AMPS  1200    // change this to fit your setup (1200 = 1.2 amps)
long lastMusicSample = 0;


uint16_t edges[26];   // Array used to mirror and divide
uint8_t  modus[26];   // Array that stores division remainders
uint8_t  newHue,
      speed=2;  // dont pick 1.  0 makes it linear

byte trebConf;
byte bassConf;
byte bassBand = 0;
byte trebBand = 6;

#include "audio.h"
#include "pattern.h"



///////////////////////////////////////////////////////////////////
// Controller to Client setup

// Structure to receive data
// Must match the sender structure

typedef struct struct_message {
    int id;
    bool bPowerOn;
    bool bRainbow;
    int iRed;
    int iGreen;
    int iBlue;
    int iState;
    
} struct_message;

const int ID_POND = 0;
const int ID_JELLYFISH = 1;
const int ID_KOI = 2;
const int ID_PUFFER = 3;
const int ID_LIONFISH = 4;
const int ID_TV = 5;
const int ID_ROOM = 6;
// the ID_ME allows me to write more portable code so that the packet handles can be generic
const int ID_ME = ID_TV;

CRGB newCommandColor;


int freeRam() {
  return ESP.getFreeHeap();
}


//////////////////////////////////////////////////////////////////////////////////////////////////
// Create AsyncWebServer object on port 80
AsyncWebServer server(80);


//////////////////////////////////////////////////////////////////////////////////////////////////
void handleNotFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// when the button is pressed, this ISR function is called.  
//  no delays allowed here, just note it and allow the main loop to handle it when it gets time
void ICACHE_RAM_ATTR resetButtonPressed()           
{                                          
   bResetButtonPressed = true;
}


//////////////////////////////////////////////////////////////////////////////////////////////////
// Read File from LittleFS
String readFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path, "r");
  if(!file || file.isDirectory()){
    Serial.println("- failed to open file for reading");
    return String();
  }

  String fileContent;
  while(file.available()){
    fileContent = file.readStringUntil('\n');
    break;
  }
  file.close();
  return fileContent;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// Write file to LittleFS
void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, "w");
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("- file written");
  } else {
    Serial.println("- frite failed");
  }
  file.close();
}


//////////////////////////////////////////////////////////////////////////////////////////////////
// after we exit AP mode, we need to reconnect.  but we have to do this out of the main loop so that 
//  delays can naturally happen (so that the reconnect is not inside the web callback)
// This function stores an error string in a global upon failure to connect so that the next load
//  of the webpage can show the error
void setupWifiAfterWeb () {
  
  wl_status_t statusCheck;
  String errorSnip;

  // start wifi with creds, turn on persist
  
  
  //  WiFi.disconnect();
  //  WiFi.mode(WIFI_STA);        
  //  WiFi.softAPdisconnect();
  
  //  WiFi.enableSTA(true); // just enable Station mode (doesn’t affect AP mode).
  Serial.print ("net [");
  Serial.print (inputSSID);
  Serial.println ("]");
  Serial.print ("pw [");
  Serial.print (inputPassword);
  Serial.println ("]");
  
  WiFi.persistent(true);  // make sure the arduino caches the creds
  WiFi.begin(inputSSID, inputPassword);

  Serial.println("Connecting to WiFi in setupWifiAfterWeb...");

  failCount = 0;
  while (failCount < WIFI_FAIL_COUNT )  {

    statusCheck = WiFi.status();
    
    bConnectSuccess =  (statusCheck == WL_CONNECTED);
    if (bConnectSuccess){
      Serial.println (".  Connected");
      break;
    }
    switch (statusCheck) {
      case WL_CONNECTED:
        errorSnip = "WL_CONNECTED";
        break;
      case WL_NO_SHIELD:
        errorSnip = "WL_NO_SHIELD";
        break;
      case WL_IDLE_STATUS:
        errorSnip = "WL_IDLE_STATUS";
        break;
      case WL_NO_SSID_AVAIL:
        errorSnip = "WL_NO_SSID_AVAIL";
        break;
      case WL_SCAN_COMPLETED:
        errorSnip = "WL_SCAN_COMPLETED";
        break;
      case WL_CONNECT_FAILED:
        errorSnip = "WL_CONNECT_FAILED";
        break;
      case WL_CONNECTION_LOST:
        errorSnip = "WL_CONNECTION_LOST";
        break;
      case WL_DISCONNECTED:
        errorSnip = "WL_DISCONNECTED";
        break;
      case WL_WRONG_PASSWORD:
        errorSnip = "WL_WRONG_PASSWORD";
        break;
      default:
        errorSnip = "Unknown Status = ";
        errorSnip.concat ((int)statusCheck);
        break;
    } 
    delay(500);
    failCount++;
    Serial.print(".");
  }

  if (!bConnectSuccess)
  {
    Serial.println ("\nConnection failure after AP");
    errorStringFromPrevious = "Failure.  Last error was [" + errorSnip + "]";
    // clear the creds so that we go back and do an AP.  we no longer rely on falling into AP in setup if we cannot connect
    WiFi.disconnect( true );
  }
  else
  {
    Serial.println ("\nConnection success after AP");
    errorStringFromPrevious = "  ";
    bDoBlinkSuccess=true;
  }
};




////////////////////////////////////////////////////////////////////////////////////////////////
//  We got a command from the fish controller

void handleCommand(AsyncWebServerRequest *request)
{
  String ID;
  String rainbow;
  String red = "";
  String green = "";
  String blue = "";
  
  int result = 400;  // BAD REQUEST by default


  Serial.print ("inside handleCommand   ->");
  // DEBUG_MSG(SERVER, "%s (%d args)\n", request->url().c_str(), request->params());
  Serial.println (request->url().c_str());

  /* 
  int paramsNr = request->params();
  Serial.print ("num of params = ");
  Serial.println(paramsNr);

  for(int i=0;i<paramsNr;i++){

      AsyncWebParameter* p = request->getParam(i);
      Serial.print("Param name: ");
      Serial.println(p->name());
      Serial.print("Param value: ");
      Serial.println(p->value());
      Serial.println("------");
  }
  */

  if (request->hasParam("ID", true)) {
    ID = request->getParam("ID", true)->value();
    if ( (ID == "pond" ) || ( ID == "lionfish") ) {  // then this is for us, else dump it
      Serial.println ("this message is for us");
    } 
    else {
      Serial.println ("wrong fish");
      request->send(result); // send back the 400
      return;
    }
  }

  if (request->hasParam("rainbow", true)) {
    rainbow = request->getParam("rainbow", true)->value();
    if ( rainbow== "true" ){
      Serial.println ("turn on rainbow");
      bRainbow = true;
      // no need to do more work on getting colors... rainbow overrides
      return;
    }
    else {
      bRainbow = false; 
    }
  }
  // TODO ignore power on state for now until we figure out what to do with it

  
  // process the color request
  // request is only valid is all 3 are present
  if (request->hasParam("red", true) && request->hasParam("green", true) && request->hasParam("blue", true)) {
    red = request->getParam("red", true)->value();
    green = request->getParam("green", true)->value();
    blue = request->getParam("blue", true)->value();

    // additional validation, but you could assume numbers in the proper range…

    // Do something useful
    
    Serial.print ("red = ");
    Serial.print (red);
    Serial.print ("  green = ");
    Serial.print (green);
    Serial.print ("  blue = ");
    Serial.println (blue);

    newCommandColor.red = red.toInt();
    newCommandColor.green = green.toInt();
    newCommandColor.blue = blue.toInt();
    bGotColor = true;
    Serial.print ("inside GotCommand new color = ");
    Serial.println (newCommandColor);

    // ignore state for now as we dont know how to process this from the busy board.  state will help set the pattern on the jellyfish
    result = 200; // OK
  } // else a 400 will be returned

  request->send(result); // simple status is enough. You’re not going to do anything with content you’d send back…
}




//////////////////////////////////////////////////////////////////////////////////////////////////
void setup()                                                      
{ 
  bool bNeedAP = false;
  bConnectSuccess = false;
  
  randomSeed(analogRead(0));

  bool bDoingReset = false;
  bSetupWifiPostAP = false;

  bDoBlinkSuccess=false;
  errorStringFromPrevious = "  ";
  delay(1000);
  Serial.begin(115200); //Initializing Serial Monitor                                 

  pinMode(LEDPIN, OUTPUT);
  
  Serial.println();
  Serial.println("attaching interrupt for reset button");
  pinMode(RESETBUTTON, INPUT);  
  //  resetButtonPressed is a function for creating external interrupts on the pin used for button checking
  //  attachInterrupt(digitalPinToInterrupt(RESETBUTTON),resetButtonPressed,FALLING);  




// TODO reattach reset button on the TV


  // Init FastLED early so that we can use it for showing errors
  #ifdef ESP8266
    FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);       // for WS2812 (Neopixel)
    // FastLED.addLeds<LED_TYPE,DATA_PIN,CLK_PIN,COLOR_ORDER>(leds, NUM_LEDS); // for APA102 (Dotstar)
  #else
    FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);       // for WS2812 (Neopixel)
    // FastLED.addLeds<LED_TYPE,DATA_PIN,CLK_PIN,COLOR_ORDER>(leds, NUM_LEDS); // for APA102 (Dotstar)
  #endif
  LEDS.setDither(false);
  LEDS.setCorrection(TypicalLEDStrip);
  LEDS.setBrightness(BRIGHTNESS);
  LEDS.setMaxPowerInVoltsAndMilliamps(5, MILLI_AMPS);
  for (int i=0; i< NUM_LEDS; i++){
    leds[i] = CRGB::Black;      
  }
  FastLED.show();

  // startup the filesystem
  // TODO we need some way in the documentation to make sure that files were uploaded.  maybe an app that the user runs that formats, uploads and then loads the fish code?
  if(!LittleFS.begin()){
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  if ( LittleFS.exists("\index.html"))
    Serial.println ("index file exists");
  else {
    Serial.println ("Index file missing!!                    !!!");  // files were never uploaded to the chip
    
    leds[0] = CRGB::Red;
    FastLED.show();
    return;
  }


   if (WiFi.SSID() == "") {

      Serial.println ("                         *****************    SSID is blank");
      bNeedAP = true;
  }

  if (!bDoingReset && !bNeedAP) {
    Serial.println("Connecting to WiFi..");
    
    // WiFi.mode(WIFI_AP); // so that we can use ESP.Now at the same time as wifi
    WiFi.mode(WIFI_STA); // so that we can use ESP.Now at the same time as wifi
    
    if (WiFi.SSID() == "")
      Serial.println ("                         *****************    SSID is blank but bNeedAP isnt set!!");
    
    WiFi.begin();  // assume there are stored creds because we set 'persistent' on below
  
    // TODO if this is a new house for this fish, then trying for 5 seconds on no-creds isnt correct.  figure out a way to not do this by seeing if we get a different status
    failCount = 0;
    while (failCount < WIFI_FAIL_COUNT )  {
      bConnectSuccess =  (WiFi.status() == WL_CONNECTED);
      if (bConnectSuccess){
        Serial.println (".  Connected");
        break;
      }
      delay(500);
      if (failCount < NUM_LEDS)
        leds[failCount] = CRGB::Blue;      
      failCount++;
      Serial.print("*");
      FastLED.show();
    }

  }

  if (!bConnectSuccess && !bNeedAP) // try again after a small delay
  {
    
    Serial.print ("Wifi second try ");
    delay(2000);

    WiFi.begin();  // assume there are stored creds because we set 'persistent' on below
  
    // TODO if this is a new house for this fish, then trying for 5 seconds on no-creds isnt correct.  figure out a way to not do this by seeing if we get a different status
    failCount = 0;
    while (failCount < WIFI_FAIL_COUNT )  {
      bConnectSuccess =  (WiFi.status() == WL_CONNECTED);
      if (bConnectSuccess){
        Serial.println (".  Connected");
        break;
      }
      delay(500);
      if (failCount < NUM_LEDS)
        leds[failCount] = CRGB::Purple;      
      failCount++;
      Serial.print("*");
      FastLED.show();
    }

  }

  if (bConnectSuccess) {
  
    Serial.println("\nConnected");
    bDoBlinkSuccess=true; 
 
    Serial.println(WiFi.localIP());
    Serial.print("Wi-Fi Channel: ");
    Serial.println(WiFi.channel());


    // TODO if we are splitting out how we connect to wifi so we dont have to reboot after AP mode, then this code needs to be factored
    // we have connected to the house wifi.  setup OTA

    
    Serial.println (" Wifi connected, start OTA setup");
    ArduinoOTA.setPort(8266);
    ArduinoOTA.setHostname("TV");  // TODO pull this out to a variable to make setup more generic

    // No authentication by default
    ArduinoOTA.setPassword((const char *)"WillaPlayroom");

    ArduinoOTA.onStart([]() {
      Serial.println("Start OTA");  // not sure why the OTA examples have debug print... you cannot have the USB connected so you cannot see it
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd OTA");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("OTA Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
    Serial.println("OTA Ready");
    Serial.print("OTA IP address: ");
    Serial.println(WiFi.localIP());

    uint32_t chipId = ESP.getChipId();
    Serial.printf(" ESP8266 Chip id = %08X\n", chipId);

    
    // NOTE that you must start mDNS _AFTER_ you start OTA or it will mess up your hostname (you wont get XXX.local, you will get XXX_OTA.local (or whatever you named your OTA)
//    if (MDNS.begin("TV")) {  //Start mDNS  
//      Serial.println("mDNS started");
//    }
//    else
//      Serial.println ("......................mDNS failed........................");
//    MDNS.addService("FishNet", "tcp", 80);

    // Begin listening to UDP port
    // first, build the IP address of the local subnet with a 255 on the end
    
    SendIP = WiFi.localIP();
    SendIP[3]=255;  // set the last digit to broadcast
    Serial.print ("broadcast IP = ");
    Serial.println (SendIP);
  
    UDP.begin(UDP_PORT);
    Serial.print("Listening on UDP port ");
    Serial.println(UDP_PORT);
    

    /////////////////////////////////
    initializeAudio();  // resets the music chip

    memset(leds, 0, sizeof(leds));
    LEDS.show();
    for (int j=0; j< NUM_LEDS; j++)
      leds[j]=CRGB::Red;
    LEDS.show();
    
    
    for(byte b=0; b<=25; ++b)
      edges[b] = b==0? NUM_LEDS - 1: NUM_LEDS / b-1;
    for(byte b=0; b<=25; ++b)
      modus[b] = b==0 ? 0 : NUM_LEDS % b;

    ///////////////////////////////////////////////////
    
    bRainbow = true;
    bNoRandom = true;
    bIsMusic = false;
    


    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      Serial.println ("load / from success path ...test.html");
      request->send(LittleFS, "/test.html", "text/html");
      });
      
    // Route to load style.css file
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(LittleFS, "/style.css", "text/css");
      }); 
    Serial.println ("telling webserver to load /test");

    server.serveStatic("/", LittleFS, "/test.html");
    
    Serial.println ("loading route for button");    

    server.on("/api/command",HTTP_POST,handleCommand);
 
    // done setting all the routes.  start the server
    server.begin();
  }
  
  if (bNeedAP)  {  // no wifi creds.  setup a softAP and webpage to go get them  
      
    for (int i =0; i< NUM_LEDS; i++){  // tell the user the error message (no wifi) by turning every other LED blue
      if ( (i % 2) == 0)  
        leds[i] = CRGB::Black;
      else
        leds[i] = CRGB::Blue;
    }
    FastLED.show();

    // TODO what if the router is down and we just need to try again every once in a while?
    Serial.println("\nSetting up AccessPoint");
    // NULL sets an open Access Point
    WiFi.softAP("FishNet Wifi", NULL);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP); 
    Serial.println ("setting up web pages");
    
    WiFi.scanNetworks(true);  // per async web server doc, you have to init the scan from outside the callback or it will return 0 (link below)

    // Setup web routes
    
    // Web Server Root URL.  read this as "when the web server sees a load of the root webpage, a callback happens to this code, which loads index.html"
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(LittleFS, "/index.html", "text/html");
    });    
  
    // Route to load style.css file
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(LittleFS, "/style.css", "text/css");
      });

    server.serveStatic("/", LittleFS, "/");


    // Route to handle main page if the user has hit the submit button.  extract the ssid and pw 
    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
      int params = request->params();
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
          // HTTP POST ssid value
          if (p->name() == PARAM_STRING_SSID) {
            inputSSID = p->value().c_str();
            Serial.print("SSID from webpage is: ");
            Serial.println(inputSSID);
            // the ssd and the password are stored by the arduino during WiFi.begin(ssid,pw) because "persistence" is on
          }
          // HTTP POST pass value
          if (p->name() == PARAM_STRING_PW) {
            inputPassword = p->value().c_str();
            Serial.print("Password from webpage is: ");
            Serial.println(inputPassword);
          }
        }
      }
      
      // this is normally where you would put the wifi into station mode
      bSetupWifiPostAP = true;  // this flag lets us exit back to the main loop so that we can go back into station
                                //  mode while using nasty APIs like delay()
      
      // do we have all the parameters? check to see if everybody has valid strings
      if(inputSSID.isEmpty() || inputPassword.isEmpty() ){
        Serial.println("Undefined SSID or Password.");
        errorStringFromPrevious = "Undefined SSID or Password";
        // This should never happen because the webpage checks for blanks...
      }
      // tell the user what happens next on the webpage
      request->send(200, "text/plain", "In 5 seconds, the fish will now attempt to connect with your creds.\n\nIf it doesn't flash 4 times, reconnect FishNet Wifi and web page at 192.168.4.1 to try again.\n\nIf you see the flashes, then set your webpage to the new IP address");  
    });  

    
    // build a route to get data back to the webpage.  if the previous attempt at setting up wifi fails, show error message
    //Serial.println ("got to the server.on get for error String\n\n");
    
    server.on("/errorString", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println ("request from server for previous error string");
        request->send_P(200, "text/plain", errorStringFromPrevious.c_str());
        });  


      // Build a route for the webpage to acquire the currrent wifi list for the house
      //  from https://github.com/me-no-dev/ESPAsyncWebServer#scanning-for-available-wifi-networks
      // First request will return 0 results unless you start scan from somewhere else (loop/setup)
      //  Do not request more often than 3-5 seconds
      server.on("/netList", HTTP_GET, [](AsyncWebServerRequest *request){
        Serial.println ("request from server for wifi network list");

        /*   We need to express the entire table from inside the arduino code.  the callback doesnt seem to like having the table defined 
         *   outside the getElementById in the xhttp replace.  Here is what the html looks like to pair with the code below:
             <span id="netList">%NETLIST%</span>
                <script>
                    var xhttp = new XMLHttpRequest();
            
                    xhttp.open("GET", "netList", false);
                    xhttp.send();
                    document.getElementById("netList").innerHTML = xhttp.responseText;
                </script>
         
         */
        String json = "<table><caption>(refresh page to get new network scan)</caption><tr><th>Strength</th><th>SSID (name)</th><th>Hidden</th></tr>";
        int n = WiFi.scanComplete();
        if(n == -2){
          WiFi.scanNetworks(true);
        } else if(n){
          for (int i = 0; i < n; ++i){
            
            json += "<tr><td>"+String(WiFi.RSSI(i));
            json +="</td>\n";
            json += "<td>"+WiFi.SSID(i);
            json +="</td>\n";
            // json += "bssid:"+WiFi.BSSIDstr(i);
            // json += "channel:"+String(WiFi.channel(i));
            // json += "secure:"+String(WiFi.encryptionType(i));
            json += "<td>"+String(WiFi.isHidden(i)?"true":"false");
            json +="</td>\n";
            json += "</tr>\n";
          }
          WiFi.scanDelete();
          if(WiFi.scanComplete() == -2){
            WiFi.scanNetworks(true);
          }
        }
        json += "</table>";
        Serial.println (json);
        request->send(200, "text/plain", json);
        json = String();
      });

    // done setting all the routes.  start the server
    server.begin();
  }

  Serial.println ("end of setup");
  
  Serial.print(F("- SRAM left at end of Setup: "));
  Serial.println(freeRam());

}


///////////////////////////////////////////////////////////////////////////
// until we get Alexa connected, randomly change the color every 30 seconds
void checkRandomColorChange()
{
    
    if (timeToRandomColor == 0) {
    timeToRandomColor = millis();
  }
  else {
    if ((millis() - timeToRandomColor) > 30000) {
      CRGB newColor;
      
      Serial.print ("Random color change ---------------------------  (");
      newColor.red = random (255);
      Serial.print (newColor.red);
      Serial.print (" ");
      newColor.green = random (255);
      Serial.print (newColor.green);
      Serial.print (" ");
      newColor.blue = random (255);
      Serial.print (newColor.blue);
      Serial.println (")");
     

      
      for (int j =0;j<NUM_LEDS;j++) {
        leds[j]=newColor;
      }    
      timeToRandomColor = 0;
      FastLED.show();
      
    }
  }
}


////////////////////////////////////////////////////////////
void doBlinkSucess()
{
  if (lastBlickSuccessMilli== 0) {
    lastBlickSuccessMilli = millis();
    Serial.println ("blink to show user that the fish worked\n");
  }
  else {
    if ((millis() - lastBlickSuccessMilli) > 1000) {
      if (bIsOn) {
        bIsOn = false;
        digitalWrite(LEDPIN, LOW); 
        lastBlickSuccessMilli = millis();
      }
      else {
        bIsOn = true;
        digitalWrite(LEDPIN, HIGH); 
        lastBlinkCount++;
        lastBlickSuccessMilli = millis();
      }
      if (lastBlinkCount > 4) {
        lastBlickSuccessMilli = 0;
        bDoBlinkSuccess = false;
        digitalWrite(LEDPIN, HIGH); //confused.  why does writing it HIGH turn it off??
      }
    }
  }    
}


//////////////////////////////////////////////////////////////
void sendMusicUDP() {

  DynamicJsonDocument send(UDP_BUFFER);
  char char_array [UDP_BUFFER];

  send["PacketType"] = "music";
  send["MusicPacketID"] = iUDP_id;
  for (int j=0; j< NUM_BANDS; j++) {
    send["sound"][j] = spectrumValue[j];
  }
 
  serializeJson(send,char_array);
  iUDP_id += 1;


  
  UDP.beginPacket(SendIP, UDP_PORT);
  UDP.write(char_array);
  UDP.endPacket();
  
//  Serial.print ("UPD Packet written: ");      
//  Serial.println (char_array);
}


///////////////////////////////////////////////////////////////////////////
// We have received a UDP packet.  
// The TV handler is different than other fish.  it ONLY recieves command packets because it is the _sender_ of the
//  music packets.

void handlePacket ( )
{
  DynamicJsonDocument rcvJson(UDP_BUFFER);
  bool bIsCommand = false;

  deserializeJson (rcvJson, rcvUDPPacket);

  if ( rcvJson["PacketType"] == "command") {
    bIsCommand = true;
  }
  else
    return;

  CommandPacketID = rcvJson ["CommandPacketID"];

  if (CommandPacketID == 0) {
    // either at the beginning or we have wrapped this unsigned int
    lastCommandPacketID = 0;  // so that it matches with the next one on a wrap
  }
  else {
    if (CommandPacketID != lastCommandPacketID + 1) {
      // we dropped a packet
      CommandDropCount++;
      Serial.print ("------------------------- COMMAND packet drop: ");
      Serial.println (CommandDropCount);
    }
    lastCommandPacketID = CommandPacketID;
  }

  unsigned int iFish = rcvJson ["fish"];
  if ((iFish== ID_ME) || (iFish == ID_POND)) {
    // this packet is for me.

    if (!bHaveIPofParent) {
      IP_Parent = UDP.remoteIP();
      bHaveIPofParent = true;
    }
      

    bIsMusic = false;
    bIsCommand = true;
    //    Serial.println ("Command packet recieved for me");      

    // On/Off takes precedence over everything
    bUDPState = rcvJson ["power"];
    if (bUDPState == false )  // no need to continue;
    {
      Serial.println ("early exit from HandlePacket because power is false");
      return;
    }
    else // make sure the previous state is cleared
    {      
      bUDPPreviousState = true; // so that we know when we first transition to OFF
      Serial.println ("udp state is true from command");
    }

    // rainbow takes precedence (dont care what the color is) so check that first
    bRainbow = rcvJson ["rainbow"];
    if (bRainbow) {        
    //      Serial.println ("command to turn on rainbow");
      return;  // no need to process the rest of the message
    }
    bIsMusic = rcvJson ["music_on"];
    if ( bIsMusic) {
//      Serial.println ("command to go into Music mode                                      ...");
      return;
    }       
    // TODO should we be making sure the packet is correct?  do we care to check for missing red?
    newCommandColor.red = rcvJson ["red"];
    newCommandColor.green = rcvJson ["green"];
    newCommandColor.blue = rcvJson ["blue"];
    bGotColor = true;
    
    // we have ignored state and power for now.
    // state is likely something fish specific like the pattern for fading dots on the jellyfish
  }
    
    
}
////////////////////////////////////////////////////////////////////////////////////////////
// acknowledge and ON or OFF 

void sendAckUDP (bool bOn) {

  Serial.println ("Send Ack UDP");


  if (!bHaveIPofParent) {
    // we shouldnt get here.  we are Ack'ing a command packet and should have stored the IP of the parent    
    Serial.println ("      ERROR  we dont have the IP of the parent but are being asked to ack a command packet.");
    return;
  }

  DynamicJsonDocument ackBuffer(UDP_BUFFER);
  char char_array [UDP_BUFFER]={0};
  

  ackBuffer["PacketType"] = "onoff";
  ackBuffer["fish"]= ID_ME;
  ackBuffer["ackON"] = bOn; 
  serializeJson(ackBuffer,char_array);
    
  UDP.beginPacket(IP_Parent, UDP_PORT);
  UDP.write(char_array);
  UDP.endPacket();

  Serial.print ("Ack written: ");      
  Serial.println (char_array);
  
}


////////////////////////////////////////////////////////////////////////////////////////////
// acknowledge and ON or OFF 

void sendHeartbeatUDP () {

  Serial.println ("Send Heartbeat UDP");


  if (!bHaveIPofParent) {
    // we shouldnt get here.  we are Ack'ing a command packet and should have stored the IP of the parent    
    Serial.println ("      ERROR  we dont have the IP of the parent. Heartbeat.");
    return;
  }

  DynamicJsonDocument hbBuffer(UDP_BUFFER);
  char char_array [UDP_BUFFER]={0};
  

  hbBuffer["PacketType"] = "heartbeat";
  hbBuffer["fish"]= ID_ME;
  serializeJson(hbBuffer,char_array);
    
  UDP.beginPacket(IP_Parent, UDP_PORT);
  UDP.write(char_array);
  UDP.endPacket();

  Serial.print ("heartbeat written: ");      
  Serial.println (char_array);
  
}


///////////////////////////////////////////////////////////////////////////
void loop()
{

  ArduinoOTA.handle();


  if (bSetupWifiPostAP) {
    // if true, then we just came out of AP mode and need to try to go to station mode
    if (lastWifiMilli == 0)
    {
      lastWifiMilli = millis();
      Serial.println ("setting up delay before calling with new wifi creds\n");
    }
    else {
      if ((millis() - lastWifiMilli) > 5000)  {
        bSetupWifiPostAP = false;
        lastWifiMilli = 0;

        setupWifiAfterWeb();
        
      // TODO the webserver is up and running, but I havent figured out how to initialize it to handle the test.html file 
      //  so instead, just reboot so that it goes through setup again.
      Serial.println ("TODO rebooting so that MCU goes through setup again for test.html setup");
      ESP.restart();
      }
    }
  }

  if ( bResetButtonPressed  ) {
    
    if (lastDebounceTime == 0) {
      lastDebounceTime = millis();
      Serial.println ( "Interrupt RESET button pressed");


      Serial.print(F("- SRAM left: "));
      Serial.println(freeRam());
     
      // the reset button now is the way we trigger the flushing of creds    
      WiFi.persistent(true);    
      WiFi.disconnect (true);
      delay (200);
      ESP.restart();
    }
    else { // dont stutter.  as the button releases, you can see multiple press unpress press states.  50ms seems to debounce it
      if ((millis() - lastDebounceTime) > debounceDelay ) {
        lastDebounceTime = 0;
        bResetButtonPressed = false; // this will likely be reset in the code that connects the fish to the skill
        
      }
    }
  } 
  
  // we told the user to look for 4 blinks if everything is going well after they entered their wifi creds.  do that here.
  if (bDoBlinkSuccess) {
    doBlinkSucess();

  }

  if (!bConnectSuccess){

    if (WiFi.SSID() == "") // then we are in AP mode so just loop
      return;
        
    // there are a couple of scenarios here.  1) we are in AP mode... so leave it alone (just loop) 2) we have an SSID, but we cant connect (the wifi is down?), then periodically reboot.
    // the way we tell is if there is an SSID.  if there is, then reboot every minute
    // on fish with a button, the logic will be to push the button.  on fish without a button (such as the busyboard, where a button would get pushed a BUNCH), force the user to push code and reset wifi
    if (timeNoWifiReboot == 0)
      timeNoWifiReboot = millis();
    else if ((millis() - timeNoWifiReboot) > 60000) {
      Serial.println ("Rebooting after waiting a minute because 1) have SSID, but 2) no connection success");
      timeNoWifiReboot = 0; //  I dont think i need to do this because I am about to reboot
      // we think we have a valid SSID (one is stored), and we waited a minute for the wifi to come back up... so reboot and see if it is available now
      ESP.restart();
    }
    return;
  }

  // If packet received...
  int packetSize = UDP.parsePacket();
  if (packetSize) {
    // Serial.print("Received packet! Size: ");  Serial.println(packetSize);
    int len = UDP.read(rcvUDPPacket, UDP_BUFFER);
    if (len > 0)
    {
      rcvUDPPacket[len] = '\0';
    }
//      Serial.print("Packet received: ");
//      Serial.println(rcvUDPPacket);

    handlePacket ();

    if (!bUDPState ) // OFF state.  
    {  
      bIsMusic = false;
      // if this the first transition, then turn off the lights
      if (bUDPState != bUDPPreviousState) {
        Serial.println ("                            turning OFF");
        bUDPPreviousState = bUDPState;
        for (int i=0; i< NUM_LEDS; i++)
          leds[i] = CRGB::Black;
  
        FastLED.show();
        
        delay (50);   // WHY ??!?  for some reason, THIS fish doesnt always turn off.  it hits the turn-off code above, but doesnt turn off the LEDS (although it changes the first LED's color)
                      //  but doing a delay and doing it again seems to work??
                      // TODO

        for (int i=0; i< NUM_LEDS; i++)
          leds[i] = CRGB::Black;
        FastLED.show();        
        
        sendAckUDP( bUDPState);
        bFirstHeartbeat = true;
        heartbeatTimer = 0;
        
      }
  
      return; // no need to process past here
    }
    
  }

  if (!bUDPState)  // we are OFF
  {
    bIsMusic = false;  // we have 2 masters... and their timing is a bit unpredictable
                       // so we could be getting music packets even though the last command was OFF  
    return; 
  }  
  
    
  if (heartbeatTimer == 0) {
    if (bFirstHeartbeat) {
      Serial.println ("inside reset of first heartbeat                                                    **");
      heartbeatTimer = millis();
      offsetHeartbeat = 200 * ID_ME;  // spread out the heartbeats
      bFirstHeartbeat = false;
    }
    else
    {
      heartbeatTimer = millis();      
      offsetHeartbeat = 0;
      Serial.println ("normal heartbeat setup                                                             **");
    }
  }
  else if ( (millis() - heartbeatTimer) > (HEARTBEAT_TIME+offsetHeartbeat)) {
    sendHeartbeatUDP();
    heartbeatTimer = 0;    
  }

  if (bIsMusic ) {
    if (lastMusicSample == 0 ) {
      lastMusicSample = millis();
    }
    else if ( millis() - lastMusicSample > 50 ) {
      readAudio();
      // send this music packet to the other fish
      sendMusicUDP();
      lastMusicSample = 0;
    }
    
    newFlow();
    FastLED.show();
  }  


  if (bRainbow) {
   
    fill_rainbow (leds, NUM_LEDS, gHue, 3);
    FastLED.show();
    FastLED.delay(1000 / FRAMES_PER_SECOND);
    EVERY_N_MILLISECONDS( 10 ) {
      gHue++;  // slowly cycle the "base color" through the rainbow
      
    }
  }



  if (bGotColor) {  // from command
    // web page gave us a new color.  convert all the lights to it.
    // make sure you set pale and bright pixel

    // string is in hex RGB format
    //int newColor = (uint64_t) strtoull(inputColor.c_str(), 0, 16);
    // now newColor comes from the controller (busy board) and is set in handleCommand
    
    Serial.print ("new color = ");
    Serial.println (newCommandColor);


    for (int j =0;j<NUM_LEDS;j++) {
      leds[j]=newCommandColor;
    }

    bGotColor = false;
    bNoRandom = true;  // if the user said to go to a specific color, stay there
    FastLED.show();
  }
  
  if (!bRainbow && !bNoRandom && bConnectSuccess){
    checkRandomColorChange();
  }
}
