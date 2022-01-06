/*
 *  This sketch sends data via HTTP GET requests to data.sparkfun.com service.
 *
 *  You need to get streamId and privateKey at data.sparkfun.com and paste them
 *  below. Or just customize this script to talk to other HTTP servers.
 *
 */
//#define PRINT_REPLY
//#define PRINT_REQUEST

#define PIN_VCC_IO 5
#define PIN_EN 32
#define PIN_DIAG 34
#define PIN_RED 23
#define PIN_GREEN 18
#define PIN_STEP 21
#define PIN_DIR 22

#define TX1_pin  10
#define RX1_pin  9

#define USC     4
#define FSC     256
#define V_RPS  (V_HZ / USC / FSC)
#define FCLK   12000000
#define FLCK_DIV (2^24)


#include <stdlib.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_sleep.h>
#include "DEV_Config.h"
#include "EPD.h"
#include "GUI_Paint.h"
#include "imagedata.h"

#include "include/Functions.h"
#include "include/TMC2300.h"
#include "include/CRC.h"

#define MAX_TWEETS        10
#define MOTOR_ON_SECONDS  15
#define TWEET_SECONDS     10

const char* ssid     = "<YOURSSID>";
const char* password = "<PASSWORD>";
const String BEARER_TOKEN = "<YOUR BEARER TOKEN>";

const char* HOST = "api.twitter.com";

typedef struct 
{
  String id;
  String text;
  String author;
} Tweet;

Tweet tweets[MAX_TWEETS];
int numTweets = 0;
int currTweet = 0;
int secCounter = 0;
int cyclesCounter = 0;
int secOnLeft = 0;
int secTweetLeft = 0;
UBYTE *BlackImage;

WiFiClientSecure client;
int targetVelocity = 0;
int targetVActual = 0;
int prevMillis;
bool direction = true;
bool enable = false;

bool tweetIdExists(String id)
{
  for (int i=0; i<numTweets; i++)
  {
    if (tweets[i].id == id)
      return true;
  }

  return false;
}

String getUrl(String url)
{
    client.println("GET "+ url + " HTTP/1.1");
    client.println("Host: api.twitter.com");
    client.println("User-Agent: arduino/1.0.0");
    client.println("Authorization: Bearer " + BEARER_TOKEN);
    client.println("");

    // This will send the request to the server
    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 5000) {
        Serial.println(">>> Client Timeout !");
        break;
      }
    }

    // Read all the lines of the reply from server and print them to Serial
    String line; 
    int startFound = 0;
    while(client.available()) {
      String tmp = client.readStringUntil('\r');
      //Serial.println(tmp);

      int idx = 0;
      while ((idx <= 4) && (tmp[idx] != '{'))
        idx ++;
      
      if (tmp[idx] == '{')
        startFound = 1;

      if (startFound) line += tmp;

      if (!client.available())
        delay(100);
  }

  return line;
}

String getTweets(String hashtag)
{
  String url = "/2/tweets/search/recent?query=%23" + hashtag + "&max_results=" + String(MAX_TWEETS) + "&expansions=author_id";
#ifdef PRINT_REQUEST
  Serial.print("Requesting URL: ");
  Serial.println(url);
#endif
  
  String line = getUrl(url);

#ifdef PRINT_REPLY
  Serial.println("--------------------------");
  Serial.println(line);
  Serial.println("--------------------------");
#endif
  
  return line;
}

String getAuthor(String authorId)
{
  Serial.println("Lookp user: " + authorId);

  String url = "/2/users/"+authorId;
#ifdef PRINT_REQUEST
  Serial.print("Requesting URL: ");
  Serial.println(url);
#endif
 
  String line = getUrl(url);

#ifdef PRINT_REPLY
  Serial.println("--------------------------");
  Serial.println(line);
  Serial.println("--------------------------");
#endif
  
  return line;
}

void tmcSetup()
{
  // pull down EN
  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, LOW);

  pinMode(PIN_STEP, OUTPUT);    
  digitalWrite(PIN_STEP, LOW);   
      
  pinMode(PIN_DIR, OUTPUT);    
  digitalWrite(PIN_DIR, LOW);     
  
  // power on TMC
  pinMode(PIN_VCC_IO, OUTPUT);
  digitalWrite(PIN_VCC_IO, LOW);

  // Initialize CRC calculation for TMC2300 UART datagrams
  Serial.print("Initializing CRC...");
  tmc_fillCRC8Table(0x07, true, 0);
  Serial.println("DONE");

  // check communication
  Serial.print("Reading IFCNT...");
  int32_t reg = tmc2300_readInt(TMC2300_IFCNT);
  Serial.print("DONE ("); Serial.print(reg, HEX); Serial.println(")");

  // lower down current (hold=0, run=10)
  Serial.print("Setting IHOLD_IRUN...");
  tmc2300_writeInt(TMC2300_IHOLD_IRUN, 0x00000A00);
  tmc2300_writeInt(TMC2300_CHOPCONF, 0x14008001);     // Re-write the CHOPCONF register periodically    
  Serial.println("DONE");
  
  reg = tmc2300_readInt(TMC2300_IFCNT);
  Serial.print("DONE ("); Serial.print(reg, HEX); Serial.println(")");
}

void setMaxCurrent(int max)
{
  Serial.print("New MaxCurrent set: ");
  Serial.println(max);
  
  uint32_t value = 1 << TMC2300_IHOLDDELAY_SHIFT
                 | ((max << TMC2300_IRUN_SHIFT) & TMC2300_IRUN_MASK)
                 | 8 << TMC2300_IHOLD_SHIFT;
  tmc2300_writeInt(TMC2300_IHOLD_IRUN, value);
}

int tmcComputeVActual(int rpm)
{
  double vhz = rpm * FSC / USC;
  double vact = vhz / 0.715;
  Serial.print("RPM: "); Serial.print(rpm); Serial.print(" --> "); Serial.print(vhz); Serial.print(" --> "); Serial.println(vact);
  
  return (int)vact;
}

void setVelocity(int vel)
{
  Serial.print("New TargetVelocity set: ");
  Serial.println(vel);

  targetVelocity = vel;
  targetVActual = tmcComputeVActual(vel);
  
  tmc2300_writeInt(TMC2300_VACTUAL, direction? targetVActual : -targetVActual);
}

void setVelocityInt(int vel)
{
  Serial.print("New TargetVelocity set: ");
  Serial.println(vel);

  targetVActual = vel;
  tmc2300_writeInt(TMC2300_VACTUAL, direction? targetVActual : -targetVActual);
}

void setDirection(int dir)
{
  Serial.println("Changed Direction");
  
  direction = dir != 0;

  tmc2300_writeInt(TMC2300_VACTUAL, direction? targetVActual : -targetVActual);
}

void tmcEnable(bool en)
{
  enable = en;
  
  if (enable)
  {
    Serial.println("Enable Motor: True");
  }
  else
  {
    Serial.println("Enable Motor: False");
  }
  
  digitalWrite(PIN_EN, enable? HIGH:LOW);
}

// Called once per second by the Blynk timer
void periodicJob()
{
  /*
  if (enable)
  {
    // Toggle the status LED while the motor is active
    digitalWrite(PIN_GREEN, HIGH);
    delay(250);
    digitalWrite(PIN_GREEN, LOW);
    delay(250);
    digitalWrite(PIN_GREEN, HIGH);
    delay(250);
    digitalWrite(PIN_GREEN, LOW);
  }
  */
  
  // Re-write the CHOPCONF register periodically
  tmc2300_writeInt(TMC2300_CHOPCONF, 0x14008001);
}

void initDisplay()
{
  printf("EPD_2IN13D_test Demo\r\n");
  DEV_Module_Init();

  printf("e-Paper Init and Clear...\r\n");
  EPD_2IN13D_Init();
  EPD_2IN13D_Clear();
  DEV_Delay_ms(500);

  //Create a new image cache
  /* you have to edit the startup_stm32fxxx.s file and set a big enough heap size */
  UWORD Imagesize = ((EPD_2IN13D_WIDTH % 8 == 0) ? (EPD_2IN13D_WIDTH / 8 ) : (EPD_2IN13D_WIDTH / 8 + 1)) * EPD_2IN13D_HEIGHT;
  if((BlackImage = (UBYTE *)malloc(Imagesize)) == NULL) {
    printf("Failed to apply for black memory...\r\n");
    while(1);
  }
  printf("Paint_NewImage\r\n");
  Paint_NewImage(BlackImage, EPD_2IN13D_WIDTH, EPD_2IN13D_HEIGHT, 270, WHITE);

#if 1   //show image for array    
  printf("show image for array\r\n");
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);
  Paint_DrawBitMap(gImage_2in13d);

  EPD_2IN13D_Display(BlackImage);
  DEV_Delay_ms(2000);
#endif

#if 0   // Drawing on the image
  printf("Drawing\r\n");
  //1.Select Image
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);

  // 2.Drawing on the image
  Paint_DrawString_EN(5, 5, "waveshare", &Font16, BLACK, WHITE);
  Paint_DrawNum(5, 25, 123456789, &Font16, BLACK, WHITE);
  Paint_DrawString_CN(5, 45, "你好abc", &Font12CN, BLACK, WHITE);
  Paint_DrawString_CN(5, 60, "微雪电子", &Font24CN, WHITE, BLACK);
  EPD_2IN13D_Display(BlackImage);
  DEV_Delay_ms(1000);

  Paint_Clear(WHITE);
  Paint_DrawPoint(5, 10, BLACK, DOT_PIXEL_1X1, DOT_STYLE_DFT);
  Paint_DrawPoint(5, 25, BLACK, DOT_PIXEL_2X2, DOT_STYLE_DFT);
  Paint_DrawPoint(5, 40, BLACK, DOT_PIXEL_3X3, DOT_STYLE_DFT);
  Paint_DrawPoint(5, 55, BLACK, DOT_PIXEL_4X4, DOT_STYLE_DFT);
  Paint_DrawLine(20, 10, 70, 60, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawLine(70, 10, 20, 60, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawLine(170, 15, 170, 55, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
  Paint_DrawLine(150, 35, 190, 35, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
  Paint_DrawRectangle(20, 10, 70, 60, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
  Paint_DrawRectangle(85, 10, 130, 60, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawCircle(170, 35, 20, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
  Paint_DrawCircle(170, 80, 20, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  EPD_2IN13D_Display(BlackImage);
  DEV_Delay_ms(2000);
#endif

#if 0   //Partial refresh, example shows time        
  printf("Partial refresh\r\n");
  Paint_SelectImage(BlackImage);
  PAINT_TIME sPaint_time;
  sPaint_time.Hour = 12;
  sPaint_time.Min = 34;
  sPaint_time.Sec = 56;
  UBYTE num = 20;
  for (;;) {
    sPaint_time.Sec = sPaint_time.Sec + 1;
    if (sPaint_time.Sec == 60) {
      sPaint_time.Min = sPaint_time.Min + 1;
      sPaint_time.Sec = 0;
      if (sPaint_time.Min == 60) {
        sPaint_time.Hour =  sPaint_time.Hour + 1;
        sPaint_time.Min = 0;
        if (sPaint_time.Hour == 24) {
          sPaint_time.Hour = 0;
          sPaint_time.Min = 0;
          sPaint_time.Sec = 0;
        }
      }
    }
    Paint_ClearWindows(15, 65, 15 + Font20.Width * 7, 65 + Font20.Height, WHITE);
    Paint_DrawTime(15, 65, &sPaint_time, &Font20, WHITE, BLACK);

    num = num - 1;
    if (num == 0) {
      break;
    }
    EPD_2IN13D_DisplayPart(BlackImage);
    DEV_Delay_ms(500);//Analog clock 1s
  }

#endif

#if 0
  printf("Clear...\r\n");
  EPD_2IN13D_Init();
  Paint_Clear(WHITE);
  Paint_SelectImage(BlackImage);
  Paint_DrawRectangle(0, 0, EPD_2IN13D_WIDTH, EPD_2IN13D_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  EPD_2IN13D_Display(BlackImage);
  DEV_Delay_ms(1000);
  EPD_2IN13D_Clear();

  printf("Goto Sleep...\r\n");
  EPD_2IN13D_Sleep();
  free(BlackImage);
  BlackImage = NULL;
#endif
}

void displayTweet(Tweet t)
{
  Serial.print("Drawing tweet ");
  Serial.print(t.author);
  Serial.print(",");
  Serial.print(t.text);
  Serial.println();

  String author = t.author;
  if (author.length() > 16)
    author = author.substring(0, 16);
  
  // clear display
  EPD_2IN13D_Clear();
  DEV_Delay_ms(500);

  // author (upper left, bold)
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);
  Paint_DrawString_EN(0, 0, author.c_str(), &Font16, WHITE, BLACK);
  
  // text
  Paint_DrawString_EN(5, 20, t.text.c_str(), &Font12, WHITE, BLACK);
  EPD_2IN13D_Display(BlackImage);
}

void setup()
{
  Serial.begin(115200);
  delay(10);

  // We start by connecting to a WiFi network

  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // TMC2300 IC UART connection
  Serial1.begin(115200, SERIAL_8N1, RX1_pin, TX1_pin);

  tmcSetup();
  setMaxCurrent(30);
  setDirection(1);
  setVelocity(100);

  initDisplay();
  Serial.println("Initialization complete");
}

void loop()
{
  secCounter = (secCounter + 1) % 30;
  if ((secCounter == 5) && (cyclesCounter == 0))
  {
    secOnLeft = MOTOR_ON_SECONDS;
    setVelocity(0);
    tmcEnable(true);
  }

  if (secCounter == 0)
    cyclesCounter ++;
  
  /*
  if (cyclesCounter > 2)
  {
    esp_sleep_enable_timer_wakeup(1 * 1000 * 1000);
    esp_light_sleep_start();
  }
  else
  */
    delay(1000);

  Serial.print(".");

  bool newTweets = false;
  if (secCounter == 0)
  {
    Serial.println();
    Serial.print("connecting to ");
    Serial.println(HOST);
  
    // Use WiFiClient class to create TCP connections
    if (!client.connect("api.twitter.com", 443)) {
        Serial.println("connection failed");
        return;
    }
  
    String line = getTweets("amgalbu_xmas");
  
    DynamicJsonDocument doc(16384);
    DeserializationError error = deserializeJson(doc, line);
    Serial.print("Deserialization error: ");
    Serial.println(error.f_str());
    if (error == DeserializationError::Ok)
    {
      numTweets = 0;
      
      String authorId;
      String author;
      String text;
      
      JsonArray arr = doc["data"];
      for (int i=0; i<arr.size() && i<MAX_TWEETS; i++)
      {
        JsonObject obj = arr[i];
        String id = String((const char*)obj["id"]);
  
        // check if tweet is already in list
        if (i == 0)
        {
          newTweets = !tweetIdExists(id);
          currTweet = 0;
        }
        
        authorId = String((const char*)obj["author_id"]);
        Serial.println(authorId);
  
        line = getAuthor(authorId);
  
        DynamicJsonDocument doc1(16384);
        error = deserializeJson(doc1, line);
        Serial.print("Deserialization error: ");
        Serial.println(error.f_str());
        if (error == DeserializationError::Ok)
        {
          JsonObject user = doc1["data"];
          author = String((const char*)user["name"]);
          Serial.println(author);
        }
  
        text = String((const char*)obj["text"]);
        Serial.println(text);
  
        tweets[i].id = id;
        tweets[i].text = text;
        tweets[i].author = author;
        numTweets ++;
      }
    }
  }
  
  if (secTweetLeft > 0)
  {
    secTweetLeft --;
    if (secTweetLeft == 0)
      currTweet = (currTweet + 1) % numTweets;
  }
  
  if (secTweetLeft == 0)
  {
    if (numTweets > 0)
    {
      displayTweet(tweets[currTweet]);
      secTweetLeft = TWEET_SECONDS;
    }
  }

  if (newTweets)
  {
    Serial.println("New tweets: starting motor");
    currTweet = 0;

    secOnLeft = MOTOR_ON_SECONDS;
    //setVelocity(200);
    tmcEnable(true);

/*
    delay(2000);
    setVelocity(0);
    tmcEnable(false);
    secOnLeft = 0;
*/    
  }

  if (secOnLeft > 0)
  {
    Serial.print("Motor seconds left: ");
    Serial.println(secOnLeft);
    if (secOnLeft == MOTOR_ON_SECONDS)
      setVelocity(70);
    else if (secOnLeft == MOTOR_ON_SECONDS-1)
      setVelocity(130);
    else if (secOnLeft == MOTOR_ON_SECONDS-2)
      setVelocity(190);
    else if (secOnLeft == MOTOR_ON_SECONDS-3)
      setVelocity(250);
    else /*if (secOnLeft == MOTOR_ON_SECONDS-4)*/
      setVelocity(270+(MOTOR_ON_SECONDS-4-secOnLenft)*5);

    //setVelocity(70 + ((MOTOR_ON_SECONDS-secOnLeft)*67));
    //setVelocity(338);

    secOnLeft --;
    if (secOnLeft == 0)
    {
      Serial.println("Stopping motor");
      setVelocity(0);
      tmcEnable(false);
    }
  }

  //periodicJob();
}
