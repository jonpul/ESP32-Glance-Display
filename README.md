# ESP32-Glance-Display
Arduino C desktop glance display

Important Notes: Edit secrets.h file to add your wifi credentials and service tokens. TFT_eSPI.zip contains the library with the changed User_Setup.h, User_Setup_Select.h and Setup302_Waveshare_ESP32S3_GC9A01.h files for this display.

Targeting Waveshare ESP32-S3 1.28" round touch display. https://www.waveshare.com/esp32-s3-touch-lcd-1.28.htm
Reference: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28

The fonts in the header files were generated with https://oleddisplay.squix.ch/ from Google Fonts Roboto. 

Touch library doesn't detect gestures on this device, so implemented my own gesture detection for tap, long-tap, and left-, right-, up- and down-swipe.
Because of the orientation I'm using to mount this, I've rotated the display 90 deg - so USB connector is on the right side. But the touch 
orientation doesn't rotate, so I am just using swipe-left/right as swipe-up/down in the code.

Clock page
  Long press - toggle 12/24 hr time
  Swipe left/right - switch display colors

Weather page
  Long-press - manually refresh weather data

Stocks page
  Long-press - manually refresh stocks data
  Swipe left/right - manually cycle through stocks

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
    WeatherAPI.com for weather. This needs a free account. 
* using the ESP32 preferences library to store and retrieve data with the onboard flash. Here I've included some logic to reduce writes to the flash to reduce wear. This takes the form of waiting a few seconds after the user changes a setting before writing it.

See the companion project https://github.com/jonpul/Elecrow-2.4-ESP32-Glance-Display
