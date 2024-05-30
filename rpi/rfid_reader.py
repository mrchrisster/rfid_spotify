import os
import sys
import spotipy
from spotipy.oauth2 import SpotifyOAuth
import logging
import RPi.GPIO as GPIO
from mfrc522 import SimpleMFRC522
import csv
import time

# Configure logging
logging.basicConfig(level=logging.WARNING)

# Initialize the RFID reader
reader = SimpleMFRC522()

# Constants for Spotify credentials and settings
CLIENT_ID = ''
CLIENT_SECRET = ''
REDIRECT_URI = 'http://localhost:8888/callback'
DESIRED_DEVICE_NAME = ""  # Replace with your desired device name
DESIRED_VOLUME = 50  # Set your desired volume level (0-100)
CACHE_PATH = os.path.join(os.path.expanduser("~"), ".spotify_token_cache")

# Initialize the Spotify client
def initialize_spotify():
    sp_oauth = SpotifyOAuth(client_id=CLIENT_ID,
                            client_secret=CLIENT_SECRET,
                            redirect_uri=REDIRECT_URI,
                            scope="user-modify-playback-state,user-read-playback-state",
                            cache_path=CACHE_PATH)

    token_info = sp_oauth.get_cached_token()
    if not token_info:
        auth_url = sp_oauth.get_authorize_url()
        print(f"Please navigate to the following URL to authenticate: {auth_url}")
        response = input("Enter the URL you were redirected to: ")
        code = sp_oauth.parse_response_code(response)
        token_info = sp_oauth.get_access_token(code)

    sp = spotipy.Spotify(auth=token_info['access_token'])
    return sp

def play_content(sp, device_id, uri):
    if uri.startswith('spotify:track'):
        sp.start_playback(device_id=device_id, uris=[uri])
        logging.warning(f"Playing track {uri} on device {DESIRED_DEVICE_NAME}")
    elif uri.startswith('spotify:album'):
        sp.start_playback(device_id=device_id, context_uri=uri)
        logging.warning(f"Playing album {uri} on device {DESIRED_DEVICE_NAME}")
    elif uri.startswith('spotify:playlist'):
        sp.start_playback(device_id=device_id, context_uri=uri)
        logging.warning(f"Playing playlist {uri} on device {DESIRED_DEVICE_NAME}")
    elif uri.startswith('spotify:show'):
        sp.start_playback(device_id=device_id, context_uri=uri)
        logging.warning(f"Playing show {uri} on device {DESIRED_DEVICE_NAME}")

def load_mappings(filename='rfid_tracks.csv'):
    mappings = {}
    with open(filename, mode='r') as infile:
        reader = csv.reader(infile, delimiter=';')
        next(reader)  # Skip header
        for rows in reader:
            if len(rows) >= 2:
                mappings[rows[0].strip()] = rows[1].strip()
    return mappings

def main_loop(reader, mappings, sp, device_id):
    last_uid = None
    while True:
        try:
            id, text = reader.read()
            uid_str = str(id).strip()

            if uid_str == last_uid:
                logging.warning(f"Ignoring duplicate scan for UID: {uid_str}")
            else:
                last_uid = uid_str

                if uid_str in mappings:
                    uri = mappings[uid_str]
                    play_content(sp, device_id, uri)
                else:
                    logging.warning(f"No track mapped for UID {uid_str}")

            time.sleep(0.1)  # Short sleep to reduce CPU usage
        except Exception as e:
            logging.error(f"An error occurred in the main loop: {e}")
            time.sleep(5)  # Delay before restarting the loop

def get_device_id(sp, device_name):
    devices = sp.devices()
    logging.warning(f"Devices: {devices}")

    for device in devices['devices']:
        if device['name'] == device_name:
            return device['id'], device['supports_volume']
    return None, False

if __name__ == "__main__":
    mappings = load_mappings()
    sp = initialize_spotify()

    device_id, supports_volume = get_device_id(sp, DESIRED_DEVICE_NAME)
    if device_id is None:
        logging.error(f"Device with name '{DESIRED_DEVICE_NAME}' not found.")
        sys.exit(1)

    if supports_volume:
        try:
            sp.volume(DESIRED_VOLUME, device_id=device_id)
            logging.warning(f"Volume set to {DESIRED_VOLUME} on device {DESIRED_DEVICE_NAME}")
        except Exception as e:
            logging.error(f"Could not set volume: {e}")
    else:
        logging.warning(f"Device {DESIRED_DEVICE_NAME} does not support volume control.")

    try:
        main_loop(reader, mappings, sp, device_id)
    except KeyboardInterrupt:
        logging.warning("Program terminated")
    finally:
        GPIO.cleanup()
