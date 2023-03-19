TV Music - arduino project

The TV / Music controller is a member of a set of LED fish.
The TV_Music controllers job is twofold: 
	1) read music packets from the audio input and broadcast them to the other fish in the room 
	2) light the back of the TV as part of the "pond"
  
The code is built for an ESP8266.

dependencies:
* SPIFFS filesystem
* ESPAsyncWebServer for initial setup of the hardware (AP mode wifi webpage)
* FastLED
* Music Animation borrowed from jordanadania  https://github.com/jordanadania/fastled-newFlow

TODO circuit diagram
