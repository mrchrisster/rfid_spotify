import os
import sys
import spotipy
from spotipy.oauth2 import SpotifyOAuth
import logging
import RPi.GPIO as GPIO
from mfrc522 import SimpleMFRC522
import time

# Configure logging with a custom format excluding the log level
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S'
)

# Initialize the RFID reader
reader = SimpleMFRC522()

# Constants for Spotify credentials and settings
CLIENT_ID = '3353adb1a5be45aaa9f60ff88ba8214f'
CLIENT_SECRET = 'e55d5d3cf939446aae339aef3e257c89'
REDIRECT_URI = 'http://localhost:8888/callback'
DESIRED_DEVICE_NAME = "Squam baby"  # Replace with your desired device name
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
    return sp, sp_oauth, token_info

def refresh_spotify_token(sp_oauth, token_info):
    if sp_oauth.is_token_expired(token_info):
        token_info = sp_oauth.refresh_access_token(token_info['refresh_token'])
    sp = spotipy.Spotify(auth=token_info['access_token'])
    return sp, token_info

def ensure_token(sp_oauth, token_info):
    return refresh_spotify_token(sp_oauth, token_info)

def play_content(sp, device_id, uri):
    sp.shuffle(False, device_id=device_id)  # Ensure shuffle is off
    try:
        if uri.startswith('spotify:track'):
            sp.start_playback(device_id=device_id, uris=[uri])
        else:
            sp.start_playback(device_id=device_id, context_uri=uri)
        logging.info(f"Playing {uri} on device {DESIRED_DEVICE_NAME}")
    except spotipy.exceptions.SpotifyException as e:
        logging.error(f"Failed to play URI {uri}: {e}")
        raise

def extract_spotify_uri(text):
    prefix = "https://open.spotify.com/"
    if text.startswith(prefix):
        url_part = text[len(prefix):]
        uri = url_part.replace("/", ":")
        return f"spotify:{uri}"
    return text

def read_data_from_card(reader):
    try:
        print("Waiting for RFID/NFC card...")
        while True:
            id, text = reader.read()
            if id:
                uid_str = str(id).strip()
                print(f'Found card with UID: {hex(id)}')
                data = text.strip()
                print(f'Read data: {data}')
                if data:
                    return uid_str, extract_spotify_uri(f"https://open.spotify.com/{data}")
                else:
                    return uid_str, None
            time.sleep(0.5)  # Delay between checks
    except Exception as e:
        print(f"Error reading data from card: {e}")

def main_loop(reader, sp, sp_oauth, token_info, device_id):
    last_uid = None
    uid_attempts = {}

    while True:
        try:
            result = read_data_from_card(reader)
            if result:
                uid_str, uri = result

                if uid_str != last_uid:
                    last_uid = uid_str
                    uid_attempts[uid_str] = 0
                else:
                    uid_attempts[uid_str] += 1

                if uid_attempts[uid_str] > 10:
                    logging.info(f"Disabling UID {uid_str} after 10 failed attempts.")
                    last_uid = None
                    continue

                if uri:
                    sp, token_info = ensure_token(sp_oauth, token_info)
                    play_content(sp, device_id, uri)
                    uid_attempts[uid_str] = 0  # Reset attempts after a successful read
                else:
                    logging.info(f"No valid data found for UID {uid_str}, attempt {uid_attempts[uid_str]}")

                # Wait for the card to be removed
                while reader.read()[0] == int(uid_str):
                    time.sleep(0.1)

                last_uid = None

            time.sleep(0.1)  # Short sleep to reduce CPU usage
        except spotipy.exceptions.SpotifyException as e:
            if e.http_status == 401:
                sp, token_info = ensure_token(sp_oauth, token_info)
                if sp is None:
                    logging.error("Failed to refresh token, exiting...")
                    sys.exit(1)
            else:
                logging.error(f"An error occurred in the main loop: {e}")
                time.sleep(5)  # Delay before restarting the loop
        except Exception as e:
            logging.error(f"An error occurred in the main loop: {e}")
            time.sleep(5)  # Delay before restarting the loop

def get_device_id(sp, device_name):
    devices = sp.devices()
    logging.info("Available devices:")
    for device in devices['devices']:
        logging.info(f"Name: {device['name']}, ID: {device['id']}")
        if device['name'] == device_name:
            return device['id'], device['supports_volume']
    return None, False

if __name__ == "__main__":
    sp, sp_oauth, token_info = initialize_spotify()

    device_id, supports_volume = get_device_id(sp, DESIRED_DEVICE_NAME)
    if device_id is None:
        logging.error(f"Device with name '{DESIRED_DEVICE_NAME}' not found.")
        sys.exit(1)

    if supports_volume:
        try:
            sp.volume(DESIRED_VOLUME, device_id=device_id)
            logging.info(f"Volume set to {DESIRED_VOLUME} on device {DESIRED_DEVICE_NAME}")
        except Exception as e:
            logging.error(f"Could not set volume: {e}")
    else:
        logging.info(f"Device {DESIRED_DEVICE_NAME} does not support volume control.")

    try:
        main_loop(reader, sp, sp_oauth, token_info, device_id)
    except KeyboardInterrupt:
        logging.info("Program terminated")
    finally:
        GPIO.cleanup()
