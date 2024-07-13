/*
TODO:
* DONE: remove "delay()" from the quote display. replace it with millis so it is non-blocking
* DONE: get touch working. Tap to force a refresh of current view is the goal.  
* DONE: Fix jaggie font for symbol and price (change looks OK)
* DONE: Detect swipe left and swipe right. Adjust for screen rotation. 
* DONE: Detect tap and long tap
* DONE: Stocks: round price 2 digits   see https://github.com/Bodmer/TFT_eSPI/discussions/2039  tft.printf
* DONE: arc graph of day hi/lo and current. 
* DONE: adjust font size for expensive stocks (NVR and BRK-A are good tests)
* DONE: introduce display "pages" 
    with simple time/date first
    weather being the next after that. 
    Swipe either direction switches topic being displayed. 
* DONE: replace String with char[] - stocks DONE, Time DONE, Weather DONE. (It's still in the JSON payload parsing)
* DONE: color range for temp
* DONE: Time page: long press to toggle 12/24 hr time with am/pm indicator
* DONE: Time page: 3 letter month name
* DONE Get time from worldtimeapi.org rather than weather page
* DONE: Auto update weather - every 16 mins seems good. 
* DONE: more colors for clock (red, silver, sky blue, green, yellow, orange)
* DONE: fallback wifi. Try main wifi 3 times, then fallback wifi 3 times, then back to main, etc. 
* DONE: store settings in device flash (clock 12/24 and color to start with)  
* DONE: if ZIP is empty in secrets.h, use public IP address to get weather 
* DONE: Refresh time when the day changes to protect against drift
* DONE: Delay clock color and mode writes to flash to reduce flash wear as user flips through settings
* DONE: Auto rotate through pages when idle
* 1. Scripts to manage changing displays for TFT_espi library  
* 2. Better quotes API with 15 min delayed quotes when market is open
* 3. next google calendar item on time/date 
* STRETCH: split refresh onto a seperate ESP32 core
 */


#include <CST816S.h>      // touch
#include <TFT_eSPI.h>     // display 
#include <WiFi.h>         
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include <TimeLib.h>
#include <Preferences.h>

Preferences preferences;

#include "secrets.h"      // wifi and acct creds & other config stuff

// custom fonts (generated from https://oleddisplay.squix.ch/)
#include "RobotoThin_10.h"
#include "Roboto_32.h"
#include "Roboto_44.h"
#include "Roboto_48.h"
#include "Roboto_22.h"
#include "Roboto_24.h"
// built in font and GFXFF reference handle
#define GFXFF 1
// font aliases
#define ROBOTOTHIN10 &RobotoThin_10
#define ROBOTO32 &Roboto_32
#define ROBOTO44 &Roboto_44
#define ROBOTO48 &Roboto_48
#define ROBOTO24 &Roboto_24
#define ROBOTO22 &Roboto_22

#define DEBUG

// gesture thresholds
#define MILLIS_LONGPRESS 500      // how long makes a tap into a tap and hold?
#define SWIPE_PIXEL_VARIANCE 40   // how many pixels to ignore from the start for a swipe gesture 
#define SWIPE_MIN_LENGTH 30       // how many pixels required to make a swipe

enum touchGesture 
{
  NOTOUCH = 0,
  TAP = 1,
  LONGTAP = 2,
  SWIPERIGHT =3,
  SWIPELEFT = 4,
  SWIPEDOWN = 5,
  SWIPEUP = 6    
};

// originally an enum but it wasn't actually bringing much to the party so just doing it manually
//  TIME = 0,
//  WEATHER = 1,
//  STOCKS = 2
const int numPages = 3;
int curPage = 0;
bool pageJustChanged = false;

// clock globals
int prevMinute = -1;
int timeColors[] = {TFT_RED, TFT_SILVER, TFT_SKYBLUE, TFT_GREEN, TFT_YELLOW, TFT_ORANGE};    // available colors for clock page
int maxColorIndex = (sizeof(timeColors) / sizeof(timeColors[0]))-1;
int curColorIndex = 0;
bool time12HrMode = false;
int lastDayClockRefresh = 0;

// timer values
#define PAGE_DISPLAY_MILLIS 30000         // how long to display a page
int lastPageDisplayMillis = millis();     // initialize page pause timer
#define QUOTE_DISPLAY_MILLIS 5000         // how long to display a quote
#define QUOTE_REFRESH_MILLIS 60000        // interval for refreshing quotes
#define WEATHER_REFRESH_MILLIS 960000     // interval for refreshing weather
// delay writing prefs to flash until they haven't changed for the defined interval
int flashWriteDelayStart = -1;            // if it is -1 that means it hasn't changed
#define FLASH_WRITE_DELAY_MS 5000         // wait this ms after setting changes to write to flash (to reduce flash wear)
//#define QUOTE_REFRESH_MILLIS 0          // interval for refreshing quotes -1 = no auto refresh
int lastQuoteRefreshMillis = millis();    // initialize quote refresh timer
int lastDisplayMillis = millis();         // initialize display pause timer       
int lastWeatherRefreshMillis = millis();  // initialize weather refresh timer

// set up quote storage
char* mySymbols[]={"MSFT","AMZN","PRU","U"};
int currentSymbol = 0;
int maxSymbolIndex = (sizeof(mySymbols) / sizeof(mySymbols[0]))-1;
struct quote {
                char* symbol;
                double price;     // string because it is parsed json
                double change;    // same
                double dayhigh;   
                double daylow;
              };
quote quotes[(sizeof(mySymbols) / sizeof(mySymbols[0]))];

// set up weather storage
struct weather {
                  char city[100];
                  char state[50];
                  int curTemp;
                  int curHumidity;
                  int todayLow;
                  int todayHigh;
                  char conditions[100];
                  int wind_spd;
                  char wind_dir[10];
                };
weather myWeather;

// display config and init
TFT_eSPI tft = TFT_eSPI(240, 240); 

// touch config and init
#define TOUCH_INT 5
#define TOUCH_SDA 6
#define TOUCH_SCL 7
#define TOUCH_RST 13
CST816S touch(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);

void setup() {
  #ifdef DEBUG   
    Serial.begin(115200);
  #endif

  preferences.begin("GlanceDisplay", false);
  // Remove all preferences under the opened namespace
  //preferences.clear();
  // pull settings from Flash if they exist
  curColorIndex = preferences.getUInt("ClockColorIdx", 0);
  time12HrMode = preferences.getBool("Clock12HrMode", false);

  // init the array with my symbols
  for(int i=0; i<= maxSymbolIndex; i++)
  {
    quotes[i].symbol = mySymbols[i];
    quotes[i].price = -1.0f;
    quotes[i].change = -1.0f;
    quotes[i].dayhigh = -1.0f;
    quotes[i].daylow = -1.0f;
  }
  dumpArrayToDebug();  

  touch.begin(); 
      
  // prep the display
  tft.init();
  tft.setRotation(1); 

  // attempt connection with retries and exit if failed
  connectWifi();
  if(!WiFi.isConnected())
  {
    tft.fillScreen(TFT_RED);
    tft.drawString("No Wifi connection", tft.width()/2, 120,4);
    delay(50000);
  }
  if(WiFi.isConnected()) // only makes sense to do this if we have a connection
  {
    tft.fillScreen(TFT_BLACK);
    // initialize quotes 
    tft.drawString("Getting quotes...", tft.width()/2, 70,4);
    getQuotes();
    tft.fillScreen(TFT_BLACK);

    // get weather
    tft.drawString("Getting weather...", tft.width()/2, 70,4);
    getWeather();
    tft.fillScreen(TFT_BLACK);

    // get time
    tft.drawString("Getting time...", tft.width()/2, 70,4);
    getTime();
    tft.fillScreen(TFT_BLACK);
  }

}

int startX, startY;
int curX, curY;
int tapX, tapY;
long startTouchMillis, endTouchMillis;
enum touchGesture gestureResult = NOTOUCH;

int lastSymbolID = -1;

void loop() 
{
  // leave immediately if no connection
  if(!WiFi.isConnected())
  {
    Serial.println(WiFi.isConnected());
    Serial.println("restarting");
    delay(2000);
    esp_restart();
  }
  int m = millis();
    if(touch.available())
  {
    if(touch.data.points == 1) // only one finger
    {
      if(startTouchMillis == 0)  // new touch
      {
        startX = touch.data.x;
        startY = touch.data.y;
        curX = startX;
        curY = startY;
        startTouchMillis = millis();
      }
      else  // continuing touch
      {
        curX = touch.data.x;
        curY = touch.data.y;
      }
    }
    else  // no touch
    {
      if(startTouchMillis !=0)   // a touch just ended
      {
        endTouchMillis = millis();   
        if(endTouchMillis - startTouchMillis >= MILLIS_LONGPRESS) // tap & hold
        {
          tapX = curX;
          tapY = curY;
          gestureResult = LONGTAP;
        }
        else if((curX > startX) &&
                (abs(curX-startX) > SWIPE_MIN_LENGTH) && 
                (abs(curY-startY)< SWIPE_PIXEL_VARIANCE))  // right swipe
        {
          gestureResult = SWIPERIGHT;
        }
        else if((curX < startX) &&
                (abs(curX-startX) > SWIPE_MIN_LENGTH) && 
                (abs(curY-startY)< SWIPE_PIXEL_VARIANCE))  // left swipe
        {
          gestureResult = SWIPELEFT;
        }
        else if((curY > startY) &&
                (abs(curY-startY) > SWIPE_MIN_LENGTH) && 
                (abs(curX-startX)< SWIPE_PIXEL_VARIANCE))  // down swipe
        {
          gestureResult = SWIPEDOWN;
        }
        else if((curY < startY) &&
                (abs(curY-startY) > SWIPE_MIN_LENGTH) && 
                (abs(curX-startX)< SWIPE_PIXEL_VARIANCE))  // up swipe
        {
          gestureResult = SWIPEUP;
        }
        
        else // must be a regular tap if we've gotten here
        {
          tapX = curX;
          tapY = curY;
          gestureResult = TAP;
        }
  
        // reset the touch tracking
        startTouchMillis = 0;  
        startX = 0;
        startY = 0;
        curX = 0;
        curY = 0;
        endTouchMillis = 0;

      }
    }
  } // end gesture handling

  if (gestureResult == LONGTAP) 
  {
    switch(curPage)
    {
      case 0: // Time/Date
        // toggle 12/24 hr mode
        time12HrMode = !time12HrMode;
        flashWriteDelayStart = millis();
        tft.fillScreen(TFT_BLACK);
        break; 
      case 1: // Weather
        // get latest weather
        getWeather();
        break;
      case 2: // Stocks
      // refresh on longtap but limited to more than 2 sec since last refresh (REST calls will take longer anyway, but just for completeness)
      if (m > (lastQuoteRefreshMillis + 2000))
      {
        getQuotes();
        lastQuoteRefreshMillis = millis(); // reset the refresh timer since we just refreshed      
      }
    }
    gestureResult = NOTOUCH;
  }

  // we are swipe up/down rather than right/left because display screen is rotated but touch screen doesn't rotate
  if(gestureResult == SWIPEUP)
  {
    // what to do for SWIPEUP on each page type
    switch (curPage)
    {
      case 0: 
        curColorIndex++;
        flashWriteDelayStart = millis();
        break;
      case 1:
        break;
      case 2:
        currentSymbol++;
        lastDisplayMillis = millis();
        lastQuoteRefreshMillis = millis();
        break;
    }
    gestureResult = NOTOUCH;
    tft.fillScreen(TFT_BLACK);
  }
  if(gestureResult == SWIPEDOWN)
  {
    switch (curPage)
    {
      // what to do for SWIPEDOWN on each page type
      case 0:
        curColorIndex--;
        flashWriteDelayStart = millis();
       break;
      case 1:
        break;
      case 2:
        currentSymbol--;
        // we don't want the display to change or the refresh to happen right when the page changes
        // as that ends up at a blank screen for a moment, so just reset the timers for them
        lastDisplayMillis = millis();
        lastQuoteRefreshMillis = millis();
        break;
    }
    gestureResult = NOTOUCH;
    tft.fillScreen(TFT_BLACK);
  }
  if(gestureResult == SWIPELEFT)
  {
    // move to next page type   
    curPage++;
    lastPageDisplayMillis = millis(); // reset page display timer
    if(curPage > (numPages-1))
    {
      curPage = 0;
    }
    gestureResult = NOTOUCH;
    pageJustChanged = true;
    tft.fillScreen(TFT_BLACK);
   }
  if(gestureResult == SWIPERIGHT)
  {
    // move to prev page type
    curPage--;
    lastPageDisplayMillis = millis(); // reset page display timer
    if(curPage < 0)
    {
      curPage = numPages-1;
    }
    gestureResult = NOTOUCH;
    pageJustChanged = true;
    tft.fillScreen(TFT_BLACK);
  }

  // display the page based on the type
  switch(curPage)
  {
    case 0:
      if(curColorIndex > maxColorIndex)
      {
        curColorIndex = 0;
      }
      else if (curColorIndex < 0)
      {
        curColorIndex = maxColorIndex;
      }
      displayTime();
      pageJustChanged = false;
      break;
    case 1:
      if(m > (WEATHER_REFRESH_MILLIS + lastWeatherRefreshMillis))
      {
          getWeather();
          lastWeatherRefreshMillis = millis();
      }
      displayWeather();
      pageJustChanged = false;
      break;
    case 2:
      if(pageJustChanged)
      {
        lastQuoteRefreshMillis = millis();
      }
      if(currentSymbol > maxSymbolIndex)
      {
        currentSymbol = 0;
      }
      else if (currentSymbol < 0)
      {
        currentSymbol = maxSymbolIndex;
      }
      if(lastSymbolID == -1)
      {
        lastSymbolID = currentSymbol;
      }
      // if the page just changed, or the symbol changed, and there is data, show it
      if(pageJustChanged || (quotes[currentSymbol].price > -1 && lastSymbolID != currentSymbol))
      {
        lastSymbolID = currentSymbol;
        displayQuote(quotes[currentSymbol].symbol, quotes[currentSymbol].price, quotes[currentSymbol].change, quotes[currentSymbol].dayhigh, quotes[currentSymbol].daylow);
        pageJustChanged = false;
      }
      if(m > (QUOTE_DISPLAY_MILLIS + lastDisplayMillis))
      {
        currentSymbol++;  // next stock
        tft.fillScreen(TFT_BLACK);
        lastDisplayMillis = millis();
      }

      // check if we need to refresh quotes ONLY when showing stock page. Set QUOTE_REFRESH_MILLIS to -1 to turn off timed refresh
      if(QUOTE_REFRESH_MILLIS > 0)
      {
        if((m > (QUOTE_REFRESH_MILLIS + lastQuoteRefreshMillis)) && !pageJustChanged)
        {
          getQuotes();
          lastQuoteRefreshMillis = millis();
        }
      }  
      else
      {
        #ifdef DEBUG
          Serial.println("Timed refresh is off");
        #endif
      }
      break;
  }
  // check the page timer and increment the page if its time
  if((m > (lastPageDisplayMillis + PAGE_DISPLAY_MILLIS)))
  {
    curPage++;
    lastPageDisplayMillis = millis(); // reset page display timer
    if(curPage > (numPages-1))
    {
      curPage = 0;
    }
    gestureResult = NOTOUCH;
    pageJustChanged = true;
    tft.fillScreen(TFT_BLACK);
  }  
}

void dumpArrayToDebug()
{
  #ifdef DEBUG
    Serial.println(F("****Dump symbol quotes array***"));
    for(int i=0; i<= maxSymbolIndex; i++)
    { 
      Serial.printf("%s \t %.2f \t %.2f \t %.2f \t %.2f\n",quotes[i].symbol, quotes[i].price, quotes[i].change, quotes[i].dayhigh, quotes[i].daylow);
    }
   #endif
}
void getQuotes()
{  
  String payload =  "";
  JSONVar jsonObj = null;

  tft.fillCircle(200,200,5,TFT_BLUE); // draw refresh indicator dot
  for(int i=0; i<= maxSymbolIndex; i++)
  {
    payload = getQuote(quotes[i].symbol);
    // Parse response
    jsonObj = JSON.parse(payload);
    // Read values
    quotes[i].price = jsonObj["c"]; // price
    quotes[i].change = jsonObj["d"]; // $ change
    quotes[i].dayhigh = jsonObj["h"]; // $ day high
    quotes[i].daylow = jsonObj["l"]; // $ day low
  }
  dumpArrayToDebug();
}

String getQuote(String symbol)
{
  // Initialize the HTTPClient object
  HTTPClient http;
  
  // Construct the URL using token from secrets.h  
  //this is finhub request
  String url = "https://finnhub.io/api/v1/quote?symbol="+symbol+"&token="+(String)FINNHUB_TOKEN;
  // Make the HTTP GET request 
  http.begin(url);
  int httpCode = http.GET();

  String payload = "";
  // Check the return code
  if (httpCode == HTTP_CODE_OK) {
    // If the server responds with 200, return the payload
    payload = http.getString();
  } else if (httpCode == HTTP_CODE_UNAUTHORIZED) {
    // If the server responds with 401, print an error message
    #ifdef DEBUG
      Serial.println(F("Invalid email or API key."));
      Serial.println(String(http.getString()));
    #endif
    tft.fillScreen(TFT_RED);
    tft.setTextSize(1); 
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Quote Service Token Error", tft.width()/2, 100,4);
    delay(1000);
    return "";
  } else {
    // For any other HTTP response code, print it
    #ifdef DEBUG
      Serial.println(F("Received unexpected HTTP response:"));
      Serial.println(httpCode);
    #endif
    tft.fillScreen(TFT_RED);
    tft.setTextSize(1); 
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Quote Service Error", tft.width()/2, 100,4);
    delay(1000);
    return "";
  }
  // End the HTTP connection
  http.end();
  
  return payload;
}

void displayQuote (char* symbol, float price, float change, float dayhigh, float daylow)
{
  static uint32_t count = 0;
  uint16_t fg_color = 0x39E8;  // dark grey 
  uint16_t bg_color = TFT_BLACK;       // This is the background colour used for smoothing (anti-aliasing)

  uint16_t x = 120;  // Position of centre of arc
  uint16_t y = 120;

  uint8_t radius       = 110; // Outer arc radius
  uint8_t thickness    = 3;     // Thickness
  uint8_t inner_radius = (radius - thickness);        // Calculate inner radius (can be 0 for circle segment)

  // 0 degrees is at 6 o'clock position
  // Arcs are drawn clockwise from start_angle to end_angle
  uint16_t start_angle = 30; // Start angle must be in range 0 to 360
  uint16_t end_angle   = 150; // End angle must be in range 0 to 360
  bool arc_end = false;           // true = round ends, false = square ends (arc_end parameter can be omitted, ends will then be square)
  tft.drawSmoothArc(x, y, radius, inner_radius, start_angle, end_angle, fg_color, bg_color, arc_end);
  fg_color = 0x0192;  // dark blue
  bg_color = TFT_BLACK;

  //int pct = map(price.toFloat(), daylow.toFloat(), dayhigh.toFloat(), 1, 100); 
  float pct = mapFloat(price, daylow, dayhigh, 1.0f, 100.0f);
  
  uint16_t pct_end_angle = (((end_angle-start_angle)*pct)/100)+start_angle;

  arc_end = false;
  inner_radius = (radius - thickness)-2; // make the current price bar a little thicker
  tft.drawSmoothArc(x, y, radius, inner_radius, start_angle, pct_end_angle, fg_color, bg_color, arc_end);
  
  tft.setTextSize(1); 
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.setFreeFont(ROBOTO48);
  tft.drawString(symbol, tft.width()/2, 66, GFXFF); 
  tft.setTextColor(TFT_WHITE);
  char sP [80];
  sprintf (sP, "$%.2f", price);
  if(price < 1000.00f)
  {
    tft.setFreeFont(ROBOTO44);  
  }
  else
  {
    tft.setFreeFont(ROBOTO32);
  }
  tft.drawString(sP, tft.width()/2, 130, GFXFF);  
  
  float fd=change;
  char sC [80];
  sprintf(sC,"%.2f",0);
  
  if (fd != 0)
  {
    if(fd<0)
    {
      tft.setTextColor(TFT_RED);
    }
    else
    { 
      tft.setTextColor(TFT_GREEN);
    }

    // if it is up, add a plus sign in front (if it is down, it already has a negative)
    // and draw the appropriate up/down arrow
    int arrowLeftStart  = 0;
    tft.setFreeFont(ROBOTO32);
    if (fd>0)
    {
      sprintf(sC, "+%.2f",fd);
      //change = "+" + change;
      arrowLeftStart = (tft.textWidth(sC)/2)+(tft.width()/2)+10;  // figure x position to start the arrow based on the pixel size of the change string
      tft.fillTriangle(arrowLeftStart,170,arrowLeftStart-5,185,arrowLeftStart+5,185,TFT_GREEN);
    }
    else // it's down
    {
      sprintf(sC, "%.2f",fd);
      arrowLeftStart = (tft.textWidth(sC)/2)+(tft.width()/2)+10;   // yes, this is duplicated from the other condition
      tft.fillTriangle(arrowLeftStart,185,arrowLeftStart-5,170,arrowLeftStart+5,170,TFT_RED);
    } 
  }
  // else it is unchanged so just leave the change amount white 
  tft.drawString(sC, tft.width()/2, 180, GFXFF);

  // draw daily high/low price on arc ends
  tft.setFreeFont(ROBOTOTHIN10);
  tft.setTextColor(TFT_SILVER);
  tft.setCursor(73,27);
  tft.printf("%0.2f",dayhigh);
  tft.setCursor(73,220);
  tft.printf("%0.2f",daylow);
}

void getWeather(void)
{  
  JSONVar jsonObj = null;

  // Initialize the HTTPClient object
  HTTPClient http;
  tft.fillCircle(200,200,5,TFT_BLUE); // draw refresh indicator dot
  
  // Construct the URL using token from secrets.h  
  //this is weatherapi.com one day forecast request, which also returns location and current conditions
  // use zipcode if there is one, otherwise use public IP location 
  String url = "";
  if((String)(WEATHER_ZIP)!="")
  {
    url = "http://api.weatherapi.com/v1/forecast.json?key="+(String)WEATHER_TOKEN+"&q="+(String)WEATHER_ZIP+"&aqi=no&days=1";
  }  
  else
  {
    url = "http://api.weatherapi.com/v1/forecast.json?key="+(String)WEATHER_TOKEN+"&q=auto:ip&aqi=no&days=1";
  }
  // Make the HTTP GET request 
  http.begin(url);
  int httpCode = http.GET();

  String payload = "";
  // Check the return code
  if (httpCode == HTTP_CODE_OK) {
    // If the server responds with 200, return the payload
    payload = http.getString();
  } else if (httpCode == HTTP_CODE_UNAUTHORIZED) {
    // If the server responds with 401, print an error message
    #ifdef DEBUG
      Serial.println(F("Weather API Key error."));
      Serial.println(String(http.getString()));
    #endif
    tft.fillScreen(TFT_RED);
    tft.setTextSize(1); 
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Weather Token Error", tft.width()/2, 100,4);
    delay(1000);
    tft.fillScreen(TFT_BLACK);
  } else {
    // For any other HTTP response code, print it
    #ifdef DEBUG
      Serial.println(F("Received unexpected HTTP response:"));
      Serial.println(httpCode);
    #endif
    tft.fillScreen(TFT_RED);
    tft.setTextSize(1); 
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Weather Service Error", tft.width()/2, 100,4);
    delay(1000);
    tft.fillScreen(TFT_BLACK);
  }
  // End the HTTP connection
  http.end();
 
  // Parse response
  jsonObj = JSON.parse(payload);
  // Read values
  const char* city = jsonObj["location"]["name"];
  strcpy(myWeather.city, city);

  const char* state = jsonObj["location"]["region"];
  strcpy(myWeather.state, state);

  myWeather.curTemp = (int)jsonObj["current"]["temp_f"];

  myWeather.curHumidity = (int)jsonObj["current"]["humidity"];        

  const char* conditions = jsonObj["current"]["condition"]["text"];
  strcpy(myWeather.conditions, conditions);

  int todayLow  = jsonObj["forecast"]["forecastday"][0]["day"]["mintemp_f"]; // the zero-eth forecast day is today (and we're only getting one day of forecast)
  myWeather.todayLow = todayLow;

  int todayHigh  = jsonObj["forecast"]["forecastday"][0]["day"]["maxtemp_f"]; // the zero-eth forecast day is today (and we're only getting one day of forecast
  myWeather.todayHigh = todayHigh;

  int wind_spd = jsonObj["current"]["wind_mph"];
  myWeather.wind_spd = wind_spd;

  const char* wind_dir = jsonObj["current"]["wind_dir"];
  // copy wind direction to the struct and make it lowercase
  strcpy(myWeather.wind_dir, wind_dir);
  int len = strlen(myWeather.wind_dir);
  for (int i = 0; i < len; i++) 
  {
      myWeather.wind_dir[i] = tolower(myWeather.wind_dir[i]);
  }
  // sync local time from the server while we're at it
  const char* localTime = jsonObj["location"]["localtime"];
  parseTime(localTime);

  tft.fillScreen(TFT_BLACK);
}

void displayWeather()
{
  tft.setTextSize(1); 
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_SILVER);
  tft.setFreeFont(ROBOTO22);
  tft.drawString(myWeather.city, tft.width()/2, 40, GFXFF); 
  tft.setFreeFont(ROBOTO22);
  tft.drawString(myWeather.state, tft.width()/2, 60, GFXFF);
  tft.setFreeFont(ROBOTO48);
  // temp color bands
  //  red >99
  //  orange 86-99
  //  yellow 71-85
  //  white 55-70
  //  blue <55

  int tempColor = 0;
  int curTemp = myWeather.curTemp;

  if(curTemp > 99)
  {
    tempColor = TFT_RED;
  } else if (curTemp >85 && curTemp <= 99)
  {
    tempColor = TFT_ORANGE;
  } else if (curTemp >70 && curTemp <= 85)
  {
    tempColor = TFT_YELLOW;
  } else if (curTemp >54 && curTemp <= 70)
  {
    tempColor = TFT_WHITE;
  } else // (temp < 55>)
  {
    tempColor = TFT_BLUE;
  }
  tft.setTextColor(tempColor);
  tft.drawNumber(curTemp, (tft.width()/2)-10, 100, GFXFF);
  // draw degree sign (2 circles to make it thicker)
  char sCurTemp[10];
  sprintf(sCurTemp,"%d",curTemp);
  int degStart = (tft.textWidth(sCurTemp)/2)+(tft.width()/2); 
  tft.drawCircle(degStart, 90, 3, tempColor);
  tft.drawCircle(degStart, 90, 4, tempColor);
  tft.setFreeFont(ROBOTO22);
  tft.drawString("F",degStart+12, 94);

  tft.setTextColor(TFT_WHITE);
  if(strlen(myWeather.conditions)<=20)
  {
    tft.setFreeFont(ROBOTO22);
  }
  else
  {
    tft.setFreeFont(ROBOTOTHIN10);
  }
  tft.drawString(myWeather.conditions, tft.width()/2, 138, GFXFF);
  tft.setFreeFont(ROBOTO24);
  int iH=myWeather.curHumidity;
  char sH [80];
  sprintf(sH, "%d%%rH    %d%s",iH ,myWeather.wind_spd, myWeather.wind_dir);
  tft.drawString(sH, tft.width()/2, 164, GFXFF);
  char sMinMax [80];
  sprintf(sMinMax, "%d/%d", myWeather.todayHigh, myWeather.todayLow);
  tft.drawString(sMinMax, tft.width()/2, 190, GFXFF);
}

void parseTime(const char* localTime)
{
  // parse the local time we got from the weather request for use by the time page (ignores second)
  // int tYr = (localTime.substring(0,4)).toInt();
  // int tMon = (localTime.substring(5,7)).toInt();
  // int tDay = (localTime.substring(8,10)).toInt();
  // int tHr = (localTime.substring(11,13)).toInt();
  // int tMin = (localTime.substring(14)).toInt();

  // time string looks like "2024-07-07T09:40:04.944551-05:00",
  // consts to avoid magic numbers
  const int startYr = 0;
  const int lenYr = 4;
  const int startMon = 5;
  const int lenMon = 2;
  const int startDay = 8;
  const int lenDay =  2;
  const int startHr = 11;
  const int lenHr = 2;
  const int startMin = 14;
  const int lenMin = 2;

  // extract the year
  char cYr[lenYr+1];
  for (int i=startYr; i<lenYr; i++)
  {
    cYr[i]=localTime[i];
  }
  cYr[lenYr]='\0';
  int tYr;
  sscanf(cYr,"%d", &tYr);
  
  // extract the month
  char cMon[lenMon+1];
  for (int i=0; i<lenMon; i++)
  {
    cMon[i]=localTime[i+startMon];
  }
  cMon[lenMon]='\0';
  int tMon;
  sscanf(cMon,"%d", &tMon);
  
  // extract the day
  char cDay[lenDay+1];
  for (int i=0; i<lenDay; i++)
  {
    cDay[i]=localTime[i+startDay];
  }
  cDay[2]='\0';
  int tDay;
  sscanf(cDay,"%d", &tDay);

  // extract the hour
  char cHr[lenHr+1];
  for (int i=0; i<lenHr; i++)
  {
    cHr[i]=localTime[i+startHr];
  }
  cHr[lenHr]='\0';
  int tHr;
  sscanf(cHr,"%d", &tHr);

  // extract the minute
  char cMin[lenMin+1];
  for (int i=0; i<lenMin; i++)
  {
    cMin[i]=localTime[i+startMin];
  }
  cMin[lenMin]='\0';
  int tMin;
  sscanf(cMin,"%d", &tMin);

  setTime(tHr,tMin,0,tDay,tMon,tYr);  
  lastDayClockRefresh = tDay;
}

void displayTime(void)
{
  // if the delay has passed after the last setting change, write to flash
  if(flashWriteDelayStart != -1)
  {
    if(millis() > (flashWriteDelayStart + FLASH_WRITE_DELAY_MS))
    {
      // check if the color has changed and if so write the new one
      int checkFlashColorIndex = preferences.getUInt("ClockColorIdx", 0);
      if (checkFlashColorIndex != curColorIndex)
      {
        preferences.putUInt("ClockColorIdx", curColorIndex);
      }
      // check if 12 hr mode has changed and if so write the new one
      int check12HrMode = preferences.getBool("Clock12HrMode", false);
      if (check12HrMode != time12HrMode)
      {
        preferences.putBool("Clock12HrMode", time12HrMode);
      }
      flashWriteDelayStart = -1; // reset the flag
    }
  }

  if(prevMinute != minute())
  {
    prevMinute = minute();
    tft.fillScreen(TFT_BLACK);
  }
  tft.setTextSize(1); 
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(timeColors[curColorIndex]);

  int curHour = hour();
  char sCurTime [80];
  // show am/pm if in 12 hr mode
  if(time12HrMode)
  {
    //int timeEndPos = (tft.width()/2)+(tft.textWidth(sCurTime)/2,7);
    int timeEndPos = 190;
    if(isPM())
    {
      tft.drawString("P", timeEndPos+5, 115, 4);
      // subtract 12 If it is past the noon hour, so 13 becomes 1. But we don't want noon to be 0 
      if(hour()>12)
      {
        curHour -= 12;
      }
    }
    else
    {
      // if it is 12 hr time, we want to show 12 for the hour at midnight
      if (hour()==0)  
      {
        curHour = 12;
      }

      tft.drawString("A", timeEndPos+5, 115, 4);
    }
    // don't force 2 digits for hour
    sprintf(sCurTime,"%d:%02d",curHour,minute());
  }
  else // 24 hr time
  {
    // force 2 digits for hour
      sprintf(sCurTime,"%02d:%02d",curHour,minute());
  }
  tft.drawString(sCurTime, (tft.width()/2)-7, 100, 7);

  char sCurDate[80];
  char sCurMonth[4];
  switch (month())
  {
    case 1: 
      strcpy(sCurMonth,"Jan");
      break;
    case 2: 
      strcpy(sCurMonth,"Feb");
      break;
    case 3: 
      strcpy(sCurMonth,"Mar");
      break;
    case 4: 
      strcpy(sCurMonth,"Apr");
      break;
    case 5: 
      strcpy(sCurMonth,"May");
      break;
    case 6: 
      strcpy(sCurMonth,"Jun");
      break;
    case 7: 
      strcpy(sCurMonth,"Jul");
      break;
    case 8: 
      strcpy(sCurMonth,"Aug");
      break;
    case 9: 
      strcpy(sCurMonth,"Sep");
      break;
    case 10: 
      strcpy(sCurMonth,"Oct");
      break;
    case 11: 
      strcpy(sCurMonth,"Nov");
      break;
    case 12: 
      strcpy(sCurMonth,"Dec");
      break;
  }
  //sprintf(sCurDate, "%d-%d-%04d",month(), day(), year());
  sprintf(sCurDate, "%d %s %04d",day(), sCurMonth, year());
  tft.drawCentreString(sCurDate, tft.width()/2, 160, 4);

  // see if we need to refresh the time
  if(lastDayClockRefresh!=day())
  {
    getTime();
  }
}

void getTime(void)
{
  JSONVar jsonObj = null;

  // Initialize the HTTPClient object
  HTTPClient http;
  tft.fillCircle(200,200,5,TFT_BLUE); // draw refresh indicator dot
  
  // Construct the URL using token from secrets.h  
  //this is WorldTimeAPI.org time request for current public IP
  String url = "https://worldtimeapi.org/api/ip";
  // Make the HTTP GET request 
  http.begin(url);
  int httpCode = http.GET();

  String payload = "";
  // Check the return code
  if (httpCode == HTTP_CODE_OK) {
    // If the server responds with 200, return the payload
    payload = http.getString();
  } else if (httpCode == HTTP_CODE_UNAUTHORIZED) {
    // If the server responds with 401, print an error message
    #ifdef DEBUG
      Serial.println(F("Time API Key error."));
      Serial.println(String(http.getString()));
    #endif
    tft.fillScreen(TFT_RED);
    tft.setTextSize(1); 
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Time Token Error", tft.width()/2, 100,4);
    delay(1000);
    tft.fillScreen(TFT_BLACK);
  } else {
    // For any other HTTP response code, print it
    #ifdef DEBUG
      Serial.println(F("Received unexpected HTTP response:"));
      Serial.println(httpCode);
    #endif
    tft.fillScreen(TFT_RED);
    tft.setTextSize(1); 
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Time Service Error", tft.width()/2, 100,4);
    delay(1000);
    tft.fillScreen(TFT_BLACK);
  }
  // End the HTTP connection
  http.end();
 
  // Parse response
  jsonObj = JSON.parse(payload);
  // Read values
  
  // get local time 
  const char* localTime = jsonObj["datetime"];
  parseTime(localTime);
  tft.fillScreen(TFT_BLACK);
}

double mapFloat (double x, double in_min, double in_max, double out_min, double out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

bool connectWifi(void)
{
  const int numRetries =5;
  int retryCount = 0;
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1); 
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE);
  // Check if the secrets have been set
  if (String(SSID) == "YOUR_SSID" || String(SSID_PASSWORD) == "YOUR_SSID_PASSWORD") {
    tft.drawString("Missing secrets!", tft.width()/2, 70,4);
    #ifdef DEBUG
      Serial.println(F("Please update the secrets.h file with your credentials before running the sketch."));
      Serial.println(F("You need to replace YOUR_SSID and YOUR_WIFI_PASSWORD with your WiFi credentials."));
    #endif
    delay(1000);
    return false;  // Stop further execution of the code
  }

  // Connect to Wi-Fi
  WiFi.begin(SSID, SSID_PASSWORD);
  while (!WiFi.isConnected() && (retryCount <= numRetries)) 
  {
    delay(1500);
    tft.fillScreen(TFT_BLACK);
    tft.drawString("Connecting WiFi...", tft.width()/2, 70,4);
    retryCount++;
  }
  if(WiFi.isConnected())
  {
    tft.drawString("Connected!", tft.width()/2, 100,4);
    delay(500);
    return(true);
  }
  else if (String(BACKUP_SSID)!="")  // try the backup if there is one
  {
    retryCount = 0; // reset retry count for backup SSID
    WiFi.disconnect();
    delay(500);
    WiFi.begin(BACKUP_SSID, BACKUP_SSID_PASSWORD);
    while (!WiFi.isConnected() && (retryCount <= numRetries)) 
    {
      delay(1500);
      tft.fillScreen(TFT_BLACK);
      tft.drawString("Backup WiFi...", tft.width()/2, 70,4);
      retryCount++;
    }
    Serial.println(WiFi.isConnected());
    if(WiFi.isConnected())
    {
      tft.drawString("Connected!", tft.width()/2, 100,4);
      delay(500);
      Serial.println("****Connected to backup wifi****");
      return(true);
    }
    else
    {
      return(false);
    }
  }
}