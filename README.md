# ESP32-Glance-Display
Arduino C desktop glance display

Important Notes: Edit secrets.h file to add your wifi credentials and service tokens. TFT_eSPI_config contains the changed header files User_Setup.h & User_Setup_Select.h needed for this display.

Targeting Waveshare ESP32-S3 1.28" round touch display. https://www.waveshare.com/esp32-s3-touch-lcd-1.28.htm
Reference: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28

In Arduino IDE, select the ESP32 -> ESP32S3 Dev Module.
Upload the icon files from the Data folder in this repo to SPIFFS. If you don't do this, the weather still works, the icons just won't display.

The fonts in the header files were generated with https://oleddisplay.squix.ch/ from Google Fonts Roboto. 

Touch library doesn't detect gestures on this device, so implemented my own gesture detection for tap, long-tap, and left-, right-, up- and down-swipe.

Clock page
  Long press - toggle 12/24 hr time
  Swipe up/down - switch display colors

Weather page
  Long-press - manually refresh weather data

Stocks page
  Long-press - manually refresh stocks data
  Swipe up/down - manually cycle through stocks

Stopwatch page
  Tap - start/stop the stopwatch
  Long-press - clear the stopwatch
  Note: when the stopwatch is running, the pages will not rotate. This is by design.

Goal of this project
--------------------------
First and foremost, I wanted to add a smart display to a desk accessory I have. 
In the process of building that, I learned a lot about this ESP32 device and the Waveshare docs aren't great so I thought I'd share the result. 
This project demonstrates the following:
* using the round display with the TFT_eSPI library, including custom fonts and smooth arcs
* using the touch panel with the CST816S library. Touch works with this library but it DOES NOT detect gestures on this device. I had to write my own gesture logic. Tap, Long Tap, Swipes.
* connecting to wifi. The secrets.h file supports a main and backup SSID. In my case, the backup is my mobile hotspot in case I want to demo this when I'm away from home with no code change. 
* HTTPclient and Ardiuno_JSON to attach to a network and make REST calls to multiple endpoints and parse the results
    WorldTimeAPI.org for current time (this device does not have an RTC, so I refresh the time on startup and at midnight). No account needed for this endpoint.
    FinnHub.io for closing stock prices. This needs a free account.
    OpenWeatherMap.org for the weather. Their forecasts were just better and more current. However, I still use WeatherAPI.com for the weather location. Both need free accounts. 
* using the ESP32 preferences library to store and retrieve data with the onboard flash. Here I've included some logic to reduce writes to the flash to reduce wear. This takes the form of waiting a few seconds after the user changes a setting before writing it.
* show drawing of PNGs from a URL (weather page). 

See the companion project at https://github.com/jonpul/Elecrow-2.4-ESP32-Glance-Display
