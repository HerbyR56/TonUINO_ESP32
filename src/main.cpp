#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiManager.h>  
#include <WebServer.h>
#include <ArduinoJson.h>
#include "ESP32FtpServer.h"
//#include <SPI.h>
#include <MFRC522.h>
#include "pinout.h"
#include "carddata.h"
//#include "dirdata.h"
//#include "SampleMp3.h"
#include "vs1053_ext.h"
#include <JC_Button.h>          // https://github.com/JChristensen/JC_Button
//#include "Mp3Player/src/mp3player.h"
#include "stringarray.h"
#include <FastLED.h>

#define buttonPlay      button1
#define buttonSkipPlus  button2
#define buttonSkipMinus button3
#define buttonVolMinus  button4
#define buttonVolPlus   button5

unsigned long displayTimer; //courtesy time to display info
TaskHandle_t Task1;
TaskHandle_t Task2;

//char testString[256];
const unsigned long WIFI_TIMEOUT = 5000; //ms

bool volumeEnabled = true;

const uint8_t START_VOLUME = 14;
const uint8_t MAX_VOLUME = 21; //org = 20
const uint8_t MIN_VOLUME = 4;
uint8_t   volume = START_VOLUME;

      unsigned long lastStandbyCheck      = 0;
const unsigned long standbyCheckIntervall = 1000;

const unsigned long BLINK_TIME            = 50;
      unsigned long lastGetFileList       = 0;
      unsigned long getFileListValidFor   = 5 *60 *1000;

String lastTrack;
String lastCardId;
WiFiClient client;

unsigned long       standbyTimer=0;
const unsigned long gotoStandbyAfter= 5 *60 *1000; //ms


const char PROGMEM rootFileName[]        = "/root.html";
const char PROGMEM mainPageFileName[]    = "/mainpage.html";

const char PROGMEM assignPageFileName[]  = "/assignpage.html";
const char PROGMEM tmpFileName[]         = "/mainpage.tmp";
const char PROGMEM cardsFileName[]       = "/cards.json";
const char PROGMEM dirFileName[]         = "/dir.tmp";


#define TRACK_NAME      "#TRACK_NAME#"
#define TRACK_IDX       "#TRACK_IDX#"
#define CARD_ID         "#CARD_ID#"
#define CARD_ID_LEGEND  "#CARD_ID_LEGEND#"
#define OPTIONS         "#OPTIONS#"
#define SELECTED        "#SELECTED#"
#define NEW_CARD_SELECT "#NEW_CARD_SELECT#"
/*
const char PROGMEM CARD_SELECT [] =   "<label for=\"" CARD_ID "\">Karte " CARD_ID "</label>\r\n"
                                      "<select name=\"" CARD_ID "\" id=\"" CARD_ID "\">\r\n"
                                      "<option value=\"none\">---</option>\r\n"
                                      OPTIONS
                                      "</select>\r\n\r\n";
*/
const char PROGMEM CARD_SELECT [] =   "<fieldset>\r\n<legend>" CARD_ID_LEGEND "</legend>\r\n"
                                      "<select name=\"" CARD_ID "\" id=\"" CARD_ID "\">\r\n"
                                      "<option value=\"none\">---</option>\r\n"
                                      OPTIONS
                                      "</select>\r\n"
                                      "<button type=\"submit\" formaction=\"/deletecard?deletecardid=" CARD_ID "\">delete</button>\r\n"
                                      "</fieldset>\r\n"
                                      "\r\n";


const char PROGMEM OPTION [] ="<option  value=\"" TRACK_IDX "\" " SELECTED ">" TRACK_NAME "</option>";



const char PROGMEM OK_REDIRECT_MAINPAGE [] = "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"1; URL=/\"><title>OK</title></head><body>OK :-) (mainpage in 3 sec.)</body></html>";


MFRC522   mfrc522(MFRC522_CS, 0/*MFRC522_RST*/);  // Create MFRC522 instance

FtpServer ftpSrv;   
WebServer server(80); 
VS1053 player(VS1053_CS, VS1053_DCS, VS1053_DREQ);


Button button1(BUTTON_1, 25, INPUT, false); 
Button button2(BUTTON_2, 25, INPUT_PULLUP, true); 
Button button3(BUTTON_3, 25, INPUT_PULLDOWN, false); 
Button button4(BUTTON_4, 25, INPUT, false); 
Button button5(BUTTON_5, 25, INPUT, false); 

/*
#define uS_TO_S_FACTOR 1000000  // Conversion factor for micro seconds to seconds 
#define TIME_TO_SLEEP  10       // Time ESP32 will go to sleep (in seconds) 
*/

#define BUTTON_PIN_BITMASK 0x200000000 // 2^33 in hex
RTC_DATA_ATTR int bootCount = 0;


//start fastled
#define DATA_PIN    LED_Ring
//#define CLK_PIN   4
#define LED_TYPE    WS2812
#define COLOR_ORDER GRB
#define NUM_LEDS    16
CRGB leds[NUM_LEDS];
#define BRIGHTNESS          128
#define FRAMES_PER_SECOND 120
//end fastled
//int volumeLED, playLED = 0;  //global variable for palystate and volume state
const int LED_COUNT = 16;

/*
Method to print the reason by which ESP32
has been awaken from sleep
*/
void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

void resetStandbyTimer()
{
   standbyTimer = millis();
}

void led(int pin, bool on)
{
  if ((pin == LED_1)
    ||(pin == LED_2)
    ||(pin == LED_3)
    ||(pin == LED_4)
    ||(pin == LED_5))
  {
    digitalWrite(pin, !on);
  }
}

void flashLed(int pin, unsigned long time = BLINK_TIME)
{
  if ((pin == LED_1)
    ||(pin == LED_2)
    ||(pin == LED_3)
    ||(pin == LED_4)
    ||(pin == LED_5))
  {
    //led(pin,false);
    led(pin,true);
    delay(time);
    //led(pin,true);
    led(pin,false);
  }
}

void flashRing(struct CRGB ringColor, unsigned long time = BLINK_TIME)
{
  FastLED.clear(true);
  for (int i = 0; i < LED_COUNT; i=i+(round(LED_COUNT/4)))
  {
    leds[i] = ringColor;
  }
  FastLED.show();
  delay(time);
}

void getFileList(fs::FS &fs, StringArray* array, const char * dirname, uint8_t levels, bool onlyDirAndM3U =true)
{
    lastGetFileList= millis();
    File root = fs.open(dirname);
    if(!root){
        Serial.println("Failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  adding DIR : ");
            Serial.println(file.name());
            array->add(file.name());
            if(levels){
                getFileList(fs, array, file.name(), levels -1, onlyDirAndM3U);
            }
        } else {
          
            String fileName = file.name();
            fileName.toUpperCase();
            if ((!onlyDirAndM3U && fileName.endsWith(".MP3")) 
             || (!onlyDirAndM3U && fileName.endsWith(".M4A"))
             || fileName.endsWith(".M3U"))
            {
              Serial.print("adding  FILE: ");
              Serial.print(file.name());
              Serial.print("  SIZE: ");
              Serial.println(file.size());
              array->add(file.name());
            }
        }
        file = root.openNextFile();
    }
}


void saveAssignPage(StringArray* array, const char * fileName, bool allCards)
{
  String page;
  if (SD.exists(mainPageFileName))
  {
    fs::File file = SD.open(mainPageFileName, "r");
    if (file)
    {
      while (file.available())
      {
        page += (char)file.read();
      }
      file.close();
      
      file = SD.open(fileName,"w");
      if (file)
      {
        int pos = page.indexOf(NEW_CARD_SELECT);
        if (pos>0)
        {
          file.write((const uint8_t*)page.c_str(), pos);

          for (int cardIdx=0; cardIdx<cardData.getCardCount(); cardIdx++)
          {
            Card& card = cardData.getCard(cardIdx);
            if (!card.isDeleted && card.ID.length())
            {
              String select = CARD_SELECT;
              String cardId = card.ID;
              select.replace(CARD_ID, cardId);

              if (card.ID.equals(lastCardId))
              {
                cardId = "<b>" + cardId + "</b>";
              }
              select.replace(CARD_ID_LEGEND, cardId);

              String options;
              bool foundOption=false;

              for (int item=0; item < array->getCount(); item++)
              {
                String option = OPTION;
                const String& track = array->getItem(item);

                String printableTrack = StringArray::convertToUTF8(track);
                //String htmlTrack = StringArray::convertToHTML(printableTrack);
                //option.replace(TRACK_NAME, htmlTrack);
                option.replace(TRACK_NAME, printableTrack);
                option.replace(TRACK_IDX, String(item));
                if (card.track.equalsIgnoreCase(track))
                {
                  Serial.print("\tfound option for card  ");
                  Serial.print(card.ID);
                  Serial.print(": ");
                  Serial.println(track);
                  foundOption=true;
                  option.replace(SELECTED, "selected");
                }
                else
                {
                  option.replace(SELECTED, "");
                }
                options+=option + "\r\n";

              }

              if (!foundOption)
              {
                Serial.print("\tNo option found for card ");
                Serial.print(card.ID);
                Serial.print(": ");
                Serial.println(card.track);
              }

              if (allCards || !foundOption) 
              {
                select.replace(OPTIONS, options);
                file.print(select.c_str());
              }
            }
          }

          file.print(page.c_str()+pos+strlen(NEW_CARD_SELECT)); //dirty
          file.close();
        }
        else
        {
          int todo;
        }
      }
      else
      {
        int todo;
      }
    } 
    else
    {
      int todo;
    }
  }  
}

String getUidString(MFRC522::Uid* uid)
{
  String uidString;
	for (byte i = 0; i < uid->size; i++) 
  {
    if (i>0)
    {
      uidString +=":";
    }
		if(uid->uidByte[i] < 0x10)
    {
			uidString +=F("0");
    }
		uidString +=String (uid->uidByte[i], HEX);
	} 
  return uidString;
}
/*
bool writeJsonFile(const char * fileName, const JsonArray& json) 
{
  Serial.print("Saving json file ");
  Serial.print (fileName);

  fs::File f = SD.open(fileName, "w");
  if (!f) 
  {
    Serial.println(" Failed to open json file for writing");
    return false;
  }

  json.prettyPrintTo(Serial);

  json.prettyPrintTo(f);
  f.close();

  Serial.println(" json file was successfully saved");
  
  return true;
}
*/

bool writeJsonFile(const char * fileName, const JsonObject& json) 
{
  
  Serial.print("Saving json file ");
  Serial.print (fileName);

  fs::File f = SD.open(fileName, "w");
  if (!f) 
  {
    Serial.println(" Failed to open json file for writing");
    return false;
  }

  json.prettyPrintTo(Serial);
  json.prettyPrintTo(f);
  f.close();

  Serial.println(" json file was successfully saved");
  
  return true;
}

bool writeCardsFile() 
{
   
  Serial.println("Saving Cards file");
   
  // JSONify local configuration parameters
  // json["ip"] = staticIp.c_str();

  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

  JsonArray& jsonArray = root.createNestedArray(("Cards"));
  
  for (int i=0; i<cardData.getCardCount();i++)
  {
    //DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonArray.createNestedObject();
    const Card& card = cardData.getCard(i);
    if (!card.isDeleted)
    {
      Serial.print("Writing Card ");
      Serial.print(card.ID);
      Serial.print(": ");
      Serial.println(card.track);
      json["ID"] = card.ID;
      json["Track"] = card.track;
    }
  }

  return writeJsonFile(cardsFileName, root );
}

bool readCardsFile() 
{
  fs::File f = SD.open(cardsFileName, "r");
  
  if (!f) 
  {
    Serial.println("Cards file not found");
    return false;
  } 
  else 
  {
    size_t size = f.size();
    // Allocate a buffer to store contents of the file.
    std::unique_ptr<char[]> buf(new char[size+1]);

    unsigned long read = f.readBytes(buf.get(), size);
    buf[size]=0;
    f.close();
    if (read)
    {
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.parseObject(buf.get());
      //JsonArray& array = jsonBuffer.parseArray(buf.get());
      if (!json.success()) 
      {
        Serial.println("JSON Object() failed");
        Serial.println(buf.get());
        return false;
      }
      json.prettyPrintTo(Serial);

      JsonArray& cards = json["Cards"];
      for (int i=0; i<cards.size();i++)
      {
        JsonObject& j = cards[i];
        String ID    = j["ID"];
        String Track = j["Track"];
        if (ID.length())
        {
          cardData.addCard(ID,Track);
        }
      } 
  /*
      if (json.containsKey("ip")) 
      {
        staticIp = (const char*)json["ip"];      
      }
      if (json.containsKey("gw")) 
      {
        staticGateway = (const char*)json["gw"];      
      }
      if (json.containsKey("sn")) 
      {
        staticSubnetMask = (const char*)json["sn"];      
      }
  */
    }
    else
    {
      Serial.println("File empty");
    }

  }
  Serial.println("\nConfig file was successfully parsed");
  
  return true;
}

void notFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (int i=0; i<server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);

}
/*
void gradientRing(int R, int G, int B)
{
  for (int i = 0; i != LED_COUNT; i++)
  {
    if (i <= voler) leds[i].setRGB();
    else leds[i] = CRBG:BLACK; //turn other LEDs off
  }

}*/

void setVolume(int newVolume)
{
  player.setVolume(newVolume);
  volume = newVolume;
  ESP_LOGV(TAG, "Changing volume to %d", volume);
    float volers = (float) (( (float) volume / (float) MAX_VOLUME) * (float)LED_COUNT);
    int volumeLEDS = (int) volers;
    int volumeLEDS = round( (volume / MAX_VOLUME) * LED_COUNT); //test
    for (int j = 0; j != LED_COUNT; j++)
      {
      if (j <= volumeLEDS) leds[j].setRGB(j*(128/NUM_LEDS), (LED_COUNT*(128/NUM_LEDS))-(j*(128/NUM_LEDS)), 0); //dim green
      else leds[j]= CRGB::Black; //off*/
    }
    FastLED.show();
    delay(10);
    displayTimer = millis();  //make sure it stays visible
}

int changeVolume(int delta)
{
  volume += delta;
  volume = volume > MAX_VOLUME ? MAX_VOLUME : volume;
  volume = volume < MIN_VOLUME ? MIN_VOLUME : volume;
  setVolume(volume);  
  return volume;
}

void sendMainPage()
{
  fs::File file = SD.open("/root.html");
  if (file)
  {
      server.streamFile(file,"text/html");
      delay(1);
      file.close();
      delay(1);
  }

}

void serverStart()
{
  if (WiFi.isConnected())
  {

    server.on("/", HTTP_GET, [](){
      sendMainPage();
      resetStandbyTimer();
    });

   server.on("/vol-", HTTP_GET, [](){
      Serial.println("vol-");
      changeVolume(-5);
      delay(5);
      sendMainPage();
    });

    server.on("/vol+", HTTP_GET, [](){
      Serial.println("vol+");
      delay(5);
      changeVolume(+2);
      sendMainPage();
    });
  
    server.on("/volstart", HTTP_GET, [](){
      Serial.println("volstart");
      setVolume(START_VOLUME);
      sendMainPage();
    });

    server.on("/volzero", HTTP_GET, [](){
      Serial.println("volzero");
      setVolume(0);
      sendMainPage();
    });

    server.on("/start", HTTP_GET, [](){
      Serial.println("start");
      if (!player.isPlaying())
      {
        player.connecttoSD(lastTrack,true); // same track, resume
      }
      sendMainPage();
    });

    server.on("/stop", HTTP_GET, [](){
      Serial.println("stop");
      if (player.isPlaying())
      {
        player.stop_mp3client();
      }
      sendMainPage();
    });

    server.on("/disableVolume", HTTP_GET, [](){
      Serial.println("disableVolume");
      volumeEnabled = false;
      sendMainPage();
    });

    server.on("/enableVolume", HTTP_GET, [](){
      Serial.println("enableVolume");
      volumeEnabled = true;
      sendMainPage();
    });

    server.on("/nexttrack", HTTP_GET, [](){
      Serial.println("next track");
      if (player.isPlaying())
      { 
        player.nextTrack();   //skip track
        player.isPaused = 0;
      }
      sendMainPage();
    });

    server.on("/previoustrack", HTTP_GET, [](){
      Serial.println("previous track");
      if (player.isPlaying())
      {
        Serial.println(player.findPreviousPlaylistEntry());
        //player.connecttoSD(lastTrack,false); // same track, start from the beginnig (of playlist)
        player.connecttoSD(player.findPreviousPlaylistEntry(),false);
      }
      sendMainPage();
    });    

    server.on("/allcards", HTTP_GET, [](){
        ESP_LOGV(TAG, "allcards, free heap: %lu", ESP.getFreeHeap());
        std::unique_ptr<StringArray> array(new StringArray);
        unsigned long msStart =millis();
        bool loaded = false;
        /*
        if (lastGetFileList && (lastGetFileList + getFileListValidFor > millis()))
        {
          fs::File file = SD.open(dirFileName, "r");
          if (file)
          {
            array.get()->load(&file);
            loaded=true;
            file.close();
          }
        }
        */
        if (!loaded)
        {
          getFileList(SD, array.get(), "/",10);
        }
        unsigned long msSort = millis();
        array.get()->sort();
        unsigned long msSave = millis();
        saveAssignPage(array.get(), tmpFileName, true);
        unsigned long msStream = millis();
        fs::File file = SD.open(tmpFileName);
        if (file)
        {
          server.streamFile(file,"text/html");
          file.close();
          file = SD.open(dirFileName,"w");
          if (file)
          {
            array.get()->save(&file);
            file.close();
          }
        }
        else
        {
          server.send(200, "text/plain", "cannot open mainpage.tmp");
        }
        ESP_LOGV(TAG, "getFileList: %lums", msSort-msStart);
        ESP_LOGV(TAG, "sort: %lums", msSave-msSort);
        ESP_LOGV(TAG, "save: %lums", msStream-msSave);
        ESP_LOGV(TAG, "streamFile: %lums", millis()-msStream);
        ESP_LOGV(TAG, "allcards end, free heap: %lu", ESP.getFreeHeap());
        resetStandbyTimer();
  });


      server.on("/newcards", HTTP_GET, [](){
          ESP_LOGV(TAG, "newcards, free heap: %lu", ESP.getFreeHeap());
          std::unique_ptr<StringArray> array(new StringArray);
          unsigned long msStart =millis();
          bool loaded = false;
/*
          if (lastGetFileList && (lastGetFileList + getFileListValidFor > millis()))
          {
            fs::File file = SD.open(dirFileName, "r");
            if (file)
            {
              array.get()->load(&file);
              loaded=true;
              file.close();
            }
          }
*/          
          if (!loaded)
          {
            getFileList(SD, array.get(), "/",10);
          }

          unsigned long msSort = millis();
          array.get()->sort();
          unsigned long msSave = millis();
          saveAssignPage(array.get(), tmpFileName, false);
          unsigned long msStream = millis();
          fs::File file = SD.open(tmpFileName);
          if (file)
          {
            server.streamFile(file,"text/html");
            file.close();
            file = SD.open(dirFileName,"w");
            if (file)
            {
              array.get()->save(&file);
              file.close();
            }
          }
          else
          {
            server.send(200, "text/plain", "cannot open mainpage.tmp");
          }
          ESP_LOGV(TAG, "getFileList: %lums", msSort-msStart);
          ESP_LOGV(TAG, "sort: %lums", msSave-msSort);
          ESP_LOGV(TAG, "save: %lums", msStream-msSave);
          ESP_LOGV(TAG, "streamFile: %lums", millis()-msStream);
          ESP_LOGV(TAG, "newcards end, free heap: %lu", ESP.getFreeHeap());
          resetStandbyTimer();
    });

    server.on("/saveassignment", HTTP_POST, [] () {
      Serial.println("PostMessage");
      String errorString;
      if (server.args())
      {
        std::unique_ptr<StringArray> array(new StringArray);
        fs::File file = SD.open(dirFileName);
        if (file)
        {
          array.get()->load(&file);

          Serial.print("loaded dirFile, found entries:");
          Serial.println(array.get()->getCount());

          file.close();
          Serial.print("Arguments: ");
          Serial.println(server.args());
          for (int i=0; i<server.args(); i++)
          {
            Serial.println(String(server.argName(i)) + ": " + server.arg(i) + "\n");
            Card& card = cardData.getCard(server.argName(i));
            if (card.ID.length())
            {
              if (server.arg(i).equalsIgnoreCase("none"))
              {
                card.track = "";
              }
              else
              {
                int idx = server.arg(i).toInt();
                if (idx>=0)
                {
                  Serial.print(card.ID);
                  Serial.print(": assigning ");
                  card.track = array.get()->getItem(idx);
                  Serial.println(card.track);
                }
              }
            }
          }
          writeCardsFile();
        }
        server.send(200, "text/html", OK_REDIRECT_MAINPAGE);

      }
      else
      {
        int todo;
        server.send(200, "text/plain", "#todo (cant open temp file)");
      }

      int todo;
      Serial.print("Free Heap:");
      Serial.println(ESP.getFreeHeap());
      Serial.println(".");
      resetStandbyTimer();
    });


  server.on("/deletecard", HTTP_POST, [] () {
      Serial.print("delete card");
      String errorString;
      if (server.args())
      {
        Serial.print("Arguments: ");
        Serial.println(server.args());
        for (int i=0; i<server.args(); i++)
        {
          Serial.println(String(server.argName(i)) + ": " + server.arg(i) + "\n");
          if (server.argName(i).equals("deletecardid"))
          {
            Serial.print("looking for deletecardid = ");
            Serial.print(server.arg(i));
            Card& card = cardData.getCard(server.arg(i));
            if (card.ID.length())
            {
              Serial.print("deleting ");
              Serial.println(card.ID);
              card.isDeleted = true;
              server.send(200, "text/html", OK_REDIRECT_MAINPAGE);
            }
            else
            {
              int todo;
              server.send(200, "text/plain", "#todo (cant find card)");
            }
          }
        }
        writeCardsFile();
      }

      Serial.print("Free Heap:");
      Serial.println(ESP.getFreeHeap());
      Serial.println(".");
      resetStandbyTimer();

    });

    server.onNotFound(notFound);
    server.begin();
  }
}

void wifiStart()
  {
    if (digitalRead(BUTTON_PLAY))  //enable WIFI if play button pressed during startup
    {
      WiFiManager WifiManager;
      String hostName = String(F("ESPFTP")) + String((unsigned long)ESP.getEfuseMac()); 

      // generate the Access Point 
      WiFi.setHostname(hostName.c_str());
      WifiManager.autoConnect("ESP32");

      Serial.print("MyIP:");
      Serial.println(WiFi.localIP().toString());
      Serial.print("MyMac:");
      Serial.println(WiFi.macAddress());
      WiFi.setHostname(hostName.c_str());
      Serial.print("MyHostName:");
      Serial.println(WiFi.getHostname());
      flashRing(CRGB::Blue,500); //signal active WIFI
    }
    else
    {
      unsigned long waitUntil = millis() + WIFI_TIMEOUT;
      WiFi.begin(); //
      uint8_t status;
      do
      {
        delay(150);
        //flashLed(LED_2);
        status = WiFi.status();
      } 
      while ((status != WL_CONNECTED && status != WL_CONNECT_FAILED)
             && millis()<waitUntil);

    }

  }

  void standBy()
  {
          // Wake up button 3
      esp_sleep_enable_ext0_wakeup(GPIO_NUM_33,1); //1 = High, 0 = Low
      Serial.println("Going to sleep now");
      Serial.flush(); 
      esp_deep_sleep_start();
  }

  void handleButtons()
  {
    button1.read();
    button2.read();
    button3.read();
    button4.read();
    button5.read();

    if (buttonVolMinus.pressedFor(100))
    { 
      resetStandbyTimer();
      Serial.println("vol- pressed");
      if (player.isPlaying())
      {
        changeVolume(-1);
        flashLed(LED_VOL_MINUS,100);
      }
    }

    if (buttonVolPlus.pressedFor(100))
    {
      resetStandbyTimer();
      Serial.println("vol+ pressed");
      if (player.isPlaying())
      {
        if (volumeEnabled)
        {
          changeVolume(+1);
        }
        flashLed(LED_VOL_PLUS,100);
      }
    }

    /*if (buttonStop.pressedFor(100))
    {
      Serial.println("stop pressed");
      resetStandbyTimer();
      flashLed(LED_STOP);
      player.stop_mp3client();
    }*/
    if (buttonSkipPlus.pressedFor(100))
    {
      Serial.println("Skip plus short pressed.");
      Serial.print(player.getFileSize());
      Serial.print(" ");
      Serial.println(player.getFilePos() + (24*4096));
      if (player.isPlaying() && (player.getFileSize() - 4096) > (player.getFilePos() + (24*4096))) //palying and able to skip
      {    
        flashLed(LED_SKIP_PLUS);
        uint8_t tempVolume = player.getVolume();
        player.setVolume(0);  //mute volume
          player.setFilePos(player.getFilePos() + (24*4096)); //seek
          unsigned long tTemp = millis();
          while (millis() - tTemp < 180)
          {
            player.loop();  //waiting 
          }
          player.setVolume(tempVolume); //restore volume
      }
    }

    if (buttonSkipPlus.pressedFor(3000))
    {
      resetStandbyTimer();
      Serial.println("Next track pressed.");
      if (player.isPlaying())
      { 
        flashLed(LED_SKIP_PLUS);
        player.nextTrack();   //skip track
        player.isPaused = 0;
        delay(300);
      }
    }

    if (buttonPlay.pressedFor(100))
    {
      resetStandbyTimer();
      Serial.println("Play pressed.");
      //flashLed(LED_PLAY);
      if (!player.isPlaying())
      {
        if (player.isPaused) player.isPaused = 0;      //unpause
        else player.connecttoSD(lastTrack,true); // same track, resume
      }
      else  //play is playing
      { 
        Serial.println("...toogle pause");
        delay(300);
        player.isPaused = !player.isPaused; //toggle pause
      }
      delay(200);
    }

    if (buttonSkipMinus.pressedFor(100))
    {
      Serial.println("Skip minus short pressed.");
      if (player.isPlaying() && (player.getFilePos() - (24*4096)) > (18*4096)) //palying and able to skip backwards (mind the header)
      {    
        Serial.println(player.getFilePos() - (24*4096));
        Serial.println(player.getFilePos());
        flashLed(LED_SKIP_MINUS);
        uint8_t tempVolume = player.getVolume();
        player.setVolume(0);  //mute volume
        if (player.getFilePos() - (24*4096) > 0) player.setFilePos(player.getFilePos() - (24*4096)); //seek
        unsigned long tTemp = millis();
        while (millis() - tTemp < 180)  //loop for 180ms
        {
          player.loop();  //waiting 
        }
        player.setVolume(tempVolume); //restore volume
      }
      else if ((player.getFilePos() - (24*4096)) < (18*4096))
      {
        player.connecttoSD(lastTrack,true);
        Serial.println("Restart track");
      }
    }

    if (buttonSkipMinus.pressedFor(2000))
    {
      resetStandbyTimer();
      Serial.println("Previous track pressed.");
      if (player.isPlaying())
      {
        flashLed(LED_SKIP_MINUS);
        Serial.println(player.findPreviousPlaylistEntry());
        //player.connecttoSD(lastTrack,false); // same track, start from the beginnig (of playlist)
        player.connecttoSD(player.findPreviousPlaylistEntry(),false);
      }
    }

    /*if (buttonRestart.pressedFor(100))
    {
      resetStandbyTimer();
      Serial.println("restart pressed");
      if (!player.isPlaying())
      {
        if (lastTrack.length())
        {
          flashLed(LED_RESTART);
          player.connecttoSD(lastTrack,false); // same track, start from the beginnig
        }
      }
    }*/

    /*if (button4.pressedFor(3000))
    {
      standBy();
      Serial.println("This will never be printed");
    }*/

    /*if (button3.pressedFor(3000))
    {
      ESP.restart();
    }*/
 
  }
/*
  bool button(int _button)
  {
    int pin;
    switch (_button)
    {
      case 1:
        pin =BUTTON_1;
      break;
      case 2:
        pin =BUTTON_2;
      break;
      case 3:
        pin =BUTTON_3;
      break;
      case 4:
        pin =BUTTON_4;
      break; 
      case 5:
        pin =BUTTON_5;
      break;
      default:
       return false;
    }
    return digitalRead(pin);
  }
  */

 void coreFTP(void * parameter) {  //loop second core
    while (true) {
      delay(1);
  if (WiFi.isConnected())
  {
    unsigned long ms =millis();
    bool isBusy=false;
    do 
    {
      //Serial.println("prepare handle ftp");
      isBusy = ftpSrv.handleFTP();    
      if (isBusy)
      {
        //standbyTimer = millis();
      }
    }
    while (isBusy && millis()<ms+10);
    //Serial.println("prepare handle client");
    //server.handleClient();
  }
  //Serial.println("prepare handle buttons");
  //handleButtons();
      }
} 




void setup(void){
  Serial.begin(115200);
  pinMode(VS1053_ENABLE, OUTPUT);
  digitalWrite(VS1053_ENABLE, false);

  pinMode(LED_1,OUTPUT);
  pinMode(LED_2,OUTPUT);
  pinMode(LED_3,OUTPUT);
  pinMode(LED_4,OUTPUT);
  pinMode(LED_5,OUTPUT);

  pinMode(LED_Ring,OUTPUT);

  led(LED_1,true);
  led(LED_2,true);
  led(LED_3,true);
  led(LED_4,true);
  led(LED_5,true);

  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  // set master brightness control
  FastLED.setBrightness(BRIGHTNESS);


  button1.begin();
  button2.begin();
  button3.begin();
  button4.begin();
  button5.begin();

  wifiStart();

  if (!WiFi.isConnected())
  {
    //power saving
    Serial.println("** Stopping WiFi+BT");
    WiFi.mode(WIFI_OFF);
    btStop();
  }
  else
  {
  //led(LED_2,true);
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID().c_str());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  }
  
  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));
  //Print the wakeup reason for ESP32
  print_wakeup_reason();

  SPI.begin();			// Init SPI bus
  mfrc522.PCD_SetAntennaGain(0x07); //set RFID Antenna Gain to 48db
	mfrc522.PCD_Init();		// Init MFRC522
  mfrc522.PCD_SetAntennaGain(0x07); //set RFID Antenna Gain to 48db again
	mfrc522.PCD_DumpVersionToSerial();	// Show details of PCD - MFRC522 Card Reader details

  Serial.print("MFRC522 Selftest:");
  if (mfrc522.PCD_PerformSelfTest())
  {
    led(LED_3,true);
    Serial.println("OK");
  }
  else
  {
    Serial.println("fail");
  }
  mfrc522.PCD_Init();		 // again! Otherwise the reader will not read

  /////FTP Setup, ensure SD is started before ftp;  /////////
  if (SD.begin(SDCARD_CS)) {
      //led(LED_4,true);
      Serial.println("SD opened!");
      ftpSrv.begin("esp32","esp32");    //username, password for ftp.  set ports in ESP32FtpServer.h  (default 21, 50009 for PASV)
  }    

  digitalWrite(VS1053_ENABLE, true);
  player.begin();
  player.setVolume(0);    
  player.printVersion();

/*
  listDir(SD, "/", 10);
  dirInfo.sort();
*/

  readCardsFile();
  serverStart();
  player.setVolume(volume);    
  Serial.println(F("Setup Complete"));
  
  xTaskCreatePinnedToCore( //task for second core
    coreFTP,
    "Handle FTP",
    20000,
    NULL,
    1,
    &Task1,
    0 //core 0 second core
  ); 

  player.connecttoSD("/001_Los_gehts.mp3",false); //playinitial song
}

unsigned long lastCardCheck= 0;
const unsigned long cardCheckIntervall = 1000;



void loop(void)
{
  if (WiFi.isConnected())
  {
    unsigned long ms =millis();
    bool isBusy=false;
    /*todo 
    {
      isBusy = ftpSrv.handleFTP();    
      if (isBusy)
      {
        //standbyTimer = millis();
      }
    }*/
    while (isBusy && millis()<ms+10);
    server.handleClient();
  }

  handleButtons();

  player.loop();
  if (player.isPlaying())
  {
    if (player.isPaused)
    {
      flashLed(LED_1,300);  //blink paused LED while player is paused
    }
    else
    {
      standbyTimer = millis();
      if (standbyTimer - displayTimer > 2000) //wait for other info to expire
      {
        float led_switch = (float) (LED_COUNT * ((float)player.getFilePos()/(float)player.getFileSize()));
        int progressLEDS = (int) led_switch;
        int progressLEDS = roundf(LED_COUNT * (player.getFilePos()/player.getFileSize())); //test
        for (int i = 0; i != LED_COUNT; i++)
        {
          if (i <= progressLEDS) leds[i].setRGB(5+(i*(48/NUM_LEDS)), 0, 5+(i*(48/NUM_LEDS))); //dim purbple
          else leds [i] = CRGB::Black; //off        
        }
        FastLED.show(); 
      }
    }
  }
  else if(millis() - displayTimer > 2000)
  {
    displayTimer = millis();
    FastLED.clear(true); //clear ring when stopped
    FastLED.show();
    delay(1000);
    flashRing(CRGB::White); //pulse ring white while waiting for Card
  }

  if (millis() > lastCardCheck + cardCheckIntervall)
  {
    lastCardCheck = millis();
    if (mfrc522.PICC_IsNewCardPresent()) 
    {
      if (mfrc522.PICC_ReadCardSerial()) 
      {
        mfrc522.PICC_HaltA();
        String uidString = getUidString(&mfrc522.uid);
        lastCardId = uidString;
        if (!cardData.cardExists(uidString))
        {
          flashRing(CRGB::Red); //Signal card known but not assigned
          displayTimer = millis();
          cardData.addCard(uidString,"");
          Serial.print("Added new card: ");
          Serial.println(uidString);
          writeCardsFile();
        }
        else
        {
          // known card
          Serial.print("Card is known: ");
          Serial.println(uidString);
          const Card& card =cardData.getCard(uidString);
          if (card.track.length() && !card.track.equalsIgnoreCase("none"))     // track assigned?
          {
            if(!(player.isPlaying() && card.track.equalsIgnoreCase(lastTrack))) // not already playing that
            {
              const String& track = card.track;
                //enable Play controls
                led(LED_5,false);
                led(LED_4,false);
                led(LED_3,false);
                led(LED_2,false);
                led(LED_1,false);
              Serial.print(" Start playing ");
              Serial.println(track.c_str());
              if (track.startsWith("http"))
              {
                  ESP_LOGV(TAG, "Play Stream");
                  flashLed(LED_GREEN);
                  player.connecttohost(track);
              } 
              else if (track.charAt(0) == '/') 
              {
                  String fileExtension;
                  int pos = track.lastIndexOf('.');
                  if (pos>track.lastIndexOf('/'))
                  {
                    fileExtension = track.substring(track.lastIndexOf('.') + 1, track.length());
                  }

                  ESP_LOGV(TAG, "Play File with Extension \"%s\"", fileExtension.c_str());

                  if (!fileExtension.length() //directory
                    || fileExtension.equalsIgnoreCase("MP3") 
                    || fileExtension.equalsIgnoreCase("M3U"))
                  {
                      flashLed(LED_GREEN);
                      lastTrack = track;
                      player.connecttoSD(track, false);
                  }
                  else 
                  {
                      flashLed(LED_BLUE);
                      ESP_LOGW(TAG, "Unsupported File Extension");
                  }
              } 
              else 
              {
                  flashLed(LED_BLUE);
                  ESP_LOGW(TAG, "Unsupported File Type");
              }
            }
            else
            {
              flashLed(LED_GREEN); // is playing?
            }
          }
          else
          {
            flashRing(CRGB::Yellow);
            displayTimer = millis()+250;
          }
        }
      }
    }
  }

  if (!player.isPlaying() 
  && millis() > lastStandbyCheck + standbyCheckIntervall)
  {
    lastStandbyCheck=millis();
    if (millis() > standbyTimer+gotoStandbyAfter)
    {
      standBy();
    }
    ESP_LOGV(TAG, "Standby in %lu", (standbyTimer+gotoStandbyAfter - millis()) /1000);
  }

}