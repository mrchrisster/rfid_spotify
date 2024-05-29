![Nanopc_Rfid](https://github.com/mrchrisster/rfid_spotify/blob/main/media/PXL_20240529_172146660~2.jpg)

# Spotify RFID Player

## What is it?
**Tap a card and listen to a song on Spotify (like a Yoto just free)**  
This is a simple set of scripts to use a RFID-RC522 with a FriendlyELEC Nanopc-t4 or Raspberry Pi (I have an old Raspberry Pi 1b I use it with)  
This is a fun little project for my son's bedtime routine. I have printed a set of cards with his favorite audiobooks he can choose and listen to to fall asleep.  


## Wiring RFID-RC522 to NanoPC-T4
  
VCC (RFID) to 3.3V (NanoPC-T4, pin 1)  
GND (RFID) to GND (NanoPC-T4, pin 6)  
RST (RFID) to GPIO17 (NanoPC-T4, pin 11)  
MISO (RFID) to MISO (NanoPC-T4, pin 21)  
MOSI (RFID) to MOSI (NanoPC-T4, pin 19)  
SCK (RFID) to SCK (NanoPC-T4, pin 23)  
SDA (SS) (RFID) to GPIO8 (NanoPC-T4, pin 24)  
  
## Setup NanoPC-T4
  
Compile for SPI support:
- Compile Kernel with [SPI enabled](https://wiki.friendlyelec.com/wiki/index.php/SPI)
- The only version working for me where WLAN was enabled and SPI was working is friendlycore-focal-4.19-arm64
- After trying out different environments to compile the kernel, I found WSL with Ubuntu 20.04.6 LTS to work well
- Make sure you install the [dev environment](https://github.com/friendlyarm/build-env-on-ubuntu-bionic)
- Use [SD Fuse tools](https://github.com/friendlyarm/sd-fuse_rk3399) to compile  with `./build-kernel.sh friendlycore-focal-arm64`

Or simply Download for SPI enabled version:
  - If you want to use my precompiled image for the Nanopc T4 with SPI and Wifi enabled, [download here](https://drive.google.com/file/d/1pRt_ehEy8QNT3_qfBpB8euX4WRCSb2db/view?usp=sharing)  
  - In order to flash the downloaded files, [download](https://download.friendlyelec.com/NanoPC-T4) rk3399-eflasher-friendlycore-focal-4.19-arm64-20240511.img and flash to SD card with Win32DiskImager  
  - Make sure Google Drive for Desktop is disabled if you use it or else the software doesn't launch
  - Now replace the friendlycore-focal-arm64 folder on SD card and insert into NanoPC to install to emmc
  
Software:
  - Once you have the nanopc up and running, double check with `ls /dev/spi*` that your SPI device is running and active
  - Next we need a special version of wiringpi for the nanopc t4. Install these two versions: [main install](https://wiki.friendlyelec.com/wiki/index.php/WiringPi_for_RK3399) and [python wrapper](https://wiki.friendlyelec.com/wiki/index.php/WiringPi-Python_for_RK3399)
  - Install the following libraries and modules:
    ```
    sudo -s
    apt-get update
    apt-get install python3-dev python3-pip
    virtualenv venv
    source venv/bin/activate
    pip3 install spidev
    pip3 install spotipy requests
    ```
    If everything installed correctly, you now try and run the script:
    `sudo python3 rfid_reader.py`

Autostart:
`nano /etc/systemd/system/rfid.service`
```                                        
[Unit]
Description=RFID
After=network.target

[Service]
Type=forking
ExecStart=/usr/bin/screen -dmS rfid /usr/bin/python3 /home/pi/rfid_reader.py
WorkingDirectory=/home/pi
Restart=always
RestartSec=5
User=root
Environment="PYTHONUNBUFFERED=1"

[Install]
WantedBy=multi-user.target
```


## Spotipy setup
- Go to [Spotify developer portal](https://developer.spotify.com/dashboard)
- In your spotify test app go to settings and write down Client ID and client secret.
- Click on Edit and add `http://localhost:8888/callback` to the redirect URIs
- Add CLIENT_ID and CLIENT_SECRET to spotify_play.py
  
- When you first launch rfid_reader.py and swipe a card that links to a spotify track, you will see in the output:  
`Please navigate to the following URL to authenticate: https://accounts.spotify.com/authorize?client_id=3353ad...`  
- Copy that URL to a browser (make sure you are logged in to spotify here) and push enter
- It will fail to open a site but that doesn't matter
- Just copy the link that it created and feed it back into spotify_play.py. It should say `Enter the URL you were redirected to:`
- Done. You have successfully authenticated Spotify on your device
  
- In the debug settings you should now see available devices, like
  ```
  DEBUG:spotipy.client:RESULTS: {'devices': [{'id': 'd5ccf364d7d8aea34c812b', 'is_active': False, 'is_private_session': False, 'is_restricted': False, 'name': 'SHIELD', 'supports_volume': True, 'type': 'TV', 'volume_percent': 46}, {'id': '687d09a07390b9e6a993232679', 'is_active': False, 'is_private_session': False, 'is_restricted': False,
  ```
- Find your device you want to play songs on and update `DESIRED_DEVICE_NAME` in spotify_play.py

  

## Wiring RFID-RC522 to Raspberry Pi
  
Find the pinout for your raspberry pi and connect all connections except for IRQ.  
For Raspberry Pi 1b its:  
GPIO 8 (SDA): Pin 24  
GPIO 11 (SCK): Pin 23  
GPIO 10 (MOSI): Pin 19  
GPIO 9 (MISO): Pin 21  
GND: Pin 6 (or any other GND pin)  
GPIO 25: Pin 22  
3.3V: Pin 1  
  
## Setup Raspberry Pi
Activate SPI:
`sudo raspi-config`
Go to "3 Interface Options" -> "I3 SPI" and enable

Software:
  - Install the following libraries and modules:
    ```
    sudo -s
    apt-get update
    apt-get install python3-dev python3-pip
    virtualenv venv
    source venv/bin/activate
    pip3 install spidev
    pip3 install mfrc522
    pip3 install spotipy requests
    ```
    If everything installed correctly, you now try and run the script:
    `sudo python3 rfid_reader.py`
    Swipe a RFID card and observe the code. Now put this code into rfid_tracks.csv
    To find the URI for an album or track on Spotify, check [here](https://support.spotify.com/us/artists/article/finding-your-artist-url/)






