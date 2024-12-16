# Alexa-Spotify Box


Wiring Guide:
RC522 Pin	ESP32 Pin Label	Description
SDA (SS)	D5	Slave Select (Chip Select)
SCK	D18	Serial Clock
MOSI	D23	Master Out Slave In
MISO	D19	Master In Slave Out
IRQ	Not connected	Interrupt Request (optional)
GND	GND	Ground
RST	D4	Reset
VCC	3.3V	Power supply (3.3V preferred)


## Get refresh token
- Go to spotify dev page https://developer.spotify.com/
- Use getrefreshtoken.py to obtain refresh token

## Setup
- Put your data in settings.h

## RFID Writer
- Flash rfid writer and encode spotify URI to card

## Final Setup
- Flash script and try

## Original Post
For Christmas, I signed myself up for the "Creative Engineering with Mark Rober". As part of the class we have to build something with an Arduino. Here is my project. 

For my final project in the Creative Engineering with Mark Rober class, I submit to you my magic Alexa-Spotify-CD box. 
I just don't have the heart to throw out all my old CDs and thought this project might give them a second life. I took an 
esp32 microcontroller, hooked a RFID-RC522 reader to it, and wrote some code to read a Spotify link from NFC tags that I 
place on the CD cases and make a API call to Spotify to play that album on the Echo Dot that I mounted into a Walnut and 
Birdseye Maple case that I built.


See my YouTube video for more information.

## Magical Spotify Player  

[![How to Build a Cool Spotify Player](https://img.youtube.com/vi/H_9H8qFnDr8/0.jpg)](https://youtu.be/H_9H8qFnDr8 "How to Build a Cool Spotify Player")


## How to use Spotify API on ESP32 with NFC Reader to control Echo Dot

[![Code walk thru](https://img.youtube.com/vi/RMtRH-3sTR4/0.jpg)](https://youtu.be/RMtRH-3sTR4 "Code walk thru")

## Magical Alexa-Spotify-CD Box  

[![Magical Alexa-Spotify-CD Box](https://img.youtube.com/vi/H2HJ-LY7-lQ/0.jpg)](https://youtu.be/H2HJ-LY7-lQ "Magical Alexa-Spotify-CD Box")

## My Project for Creative Engineering with Mark Rober | The Idea  

[![My Project for Creative Engineering with Mark Rober | The Idea](https://img.youtube.com/vi/7CPkmOHev_A/0.jpg)](https://youtu.be/7CPkmOHev_A "My Project for Creative Engineering with Mark Rober | The Idea")


## Progress  

[![Progress](https://img.youtube.com/vi/dpDbMA8f0VY/0.jpg)](https://youtu.be//dpDbMA8f0VY "Progress")

## It's working  

[![It's working](https://img.youtube.com/vi/isom4NREq14/0.jpg)](https://youtu.be/isom4NREq14 "It's working")


And checkout my YouTube channel for other maker projects I have done. https://www.youtube.com/c/MakerAtPlay
