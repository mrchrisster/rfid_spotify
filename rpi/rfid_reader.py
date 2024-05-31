import os
import sys
import spotipy
from spotipy.oauth2 import SpotifyOAuth
import logging
import RPi.GPIO as GPIO
from mfrc522 import SimpleMFRC522
import csv
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
LOOKUP_FILE = 'uid_lookup.csv'

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
    else:
        sp = spotipy.Spotify(auth=token_info['access_token'])
        return sp, token_info

def ensure_token(func):
    def wrapper(*args, **kwargs):
        sp, sp_oauth, token_info = args[-3:]
        if sp_oauth.is_token_expired(token_info):
            sp, token_info = refresh_spotify_token(sp_oauth, token_info)
            args = args[:-3] + (sp, sp_oauth, token_info)
        return func(*args, **kwargs)
    return wrapper

@ensure_token
def get_track_name(sp, uri, sp_oauth, token_info):
    if uri.startswith('spotify:track'):
        track_info = sp.track(uri)
        return track_info['name']
    elif uri.startswith('spotify:album'):
        album_info = sp.album(uri)
        return album_info['name']
    elif uri.startswith('spotify:playlist'):
        playlist_info = sp.playlist(uri)
        return playlist_info['name']
    elif uri.startswith('spotify:show'):
        show_info = sp.show(uri)
        return show_info['name']
    return "Unknown"

def load_mappings(filename='rfid_tracks.csv'):
    mappings = {}
    with open(filename, mode='r') as infile:
        reader = csv.reader(infile, delimiter=';')
        next(reader)  # Skip header
        for rows in reader:
            if len(rows) >= 2:
                uid = rows[0].strip()
                uri = rows[1].strip()
                mappings[uid] = uri
    return mappings

def load_lookup(filename=LOOKUP_FILE):
    lookup = {}
    if os.path.exists(filename):
        with open(filename, mode='r') as infile:
            reader = csv.reader(infile)
            for rows in reader:
                if len(rows) >= 2:
                    uid = rows[0].strip()
                    name = rows[1].strip()
                    lookup[uid] = name
    return lookup

def populate_lookup(mappings, sp, sp_oauth, token_info, filename=LOOKUP_FILE):
    lookup = {}
    if not os.path.exists(filename):
        with open(filename, mode='w', newline='') as outfile:
            writer = csv.writer(outfile)
            for uid, uri in mappings.items():
                track_name = get_track_name(sp, uri, sp_oauth, token_info)
                lookup[uid] = track_name
                writer.writerow([uid, track_name])
    else:
        lookup = load_lookup(filename)
    return lookup

def update_lookup(uid, name, filename=LOOKUP_FILE):
    with open(filename, mode='a', newline='') as outfile:
        writer = csv.writer(outfile)
        writer.writerow([uid, name])

@ensure_token
def play_content(sp, device_id, uri, track_name, sp_oauth, token_info):
    sp.shuffle(False, device_id=device_id)  # Ensure shuffle is off
    if uri.startswith('spotify:track'):
        sp.start_playback(device_id=device_id, uris=[uri])
    else:
        sp.start_playback(device_id=device_id, context_uri=uri)
    print(f"{time.strftime('%Y-%m-%d %H:%M:%S')} - Playing {track_name} on device {DESIRED_DEVICE_NAME}")

def main_loop(reader, mappings, lookup, sp, sp_oauth, token_info, device_id):
    last_uid = None
    last_warning_time = 0
    debounce_time = 2  # Time in seconds to suppress duplicate warnings
    duplicate_warning_logged = False

    while True:
        try:
            id, text = reader.read()
            uid_str = str(id).strip()

            current_time = time.time()
            if uid_str == last_uid:
                if not duplicate_warning_logged and current_time - last_warning_time > debounce_time:
                    print(f"Ignoring duplicate scan for UID: {uid_str}")
                    duplicate_warning_logged = True
            else:
                last_uid = uid_str
                last_warning_time = current_time
                duplicate_warning_logged = False

                if uid_str in mappings:
                    uri = mappings[uid_str]
                    if uid_str not in lookup:
                        sp, token_info = refresh_spotify_token(sp_oauth, token_info)
                        track_name = get_track_name(sp, uri, sp_oauth, token_info)
                        lookup[uid_str] = track_name
                        update_lookup(uid_str, track_name)
                    else:
                        track_name = lookup[uid_str]
                    sp, token_info = refresh_spotify_token(sp_oauth, token_info)
                    if sp is not None:
                        play_content(sp, device_id, uri, track_name, sp_oauth, token_info)
                    else:
                        print(f"Failed to refresh Spotify token for UID: {uid_str}")
                else:
                    print(f"No track mapped for UID {uid_str}")

            time.sleep(0.1)  # Short sleep to reduce CPU usage
        except spotipy.exceptions.SpotifyException as e:
            if e.http_status == 401:
                sp, token_info = refresh_spotify_token(sp_oauth, token_info)
            else:
                print(f"An error occurred in the main loop: {e}")
                time.sleep(5)  # Delay before restarting the loop
        except Exception as e:
            print(f"An error occurred in the main loop: {e}")
            time.sleep(5)  # Delay before restarting the loop

def get_device_id(sp, device_name):
    devices = sp.devices()
    print("Available devices:")
    for device in devices['devices']:
        print(f"Name: {device['name']}, ID: {device['id']}")
        if device['name'] == device_name:
            return device['id'], device['supports_volume']
    return None, False

if __name__ == "__main__":
    mappings = load_mappings()
    sp, sp_oauth, token_info = initialize_spotify()
    lookup = populate_lookup(mappings, sp, sp_oauth, token_info)

    # Display the loaded mappings
    print("Loaded RFID to Spotify track mappings:")
    for uid, track_name in lookup.items():
        print(f"UID: {uid} -> Track: {track_name}")

    device_id, supports_volume = get_device_id(sp, DESIRED_DEVICE_NAME)
    if device_id is None:
        print(f"Device with name '{DESIRED_DEVICE_NAME}' not found.")
        sys.exit(1)
    
    if supports_volume:
        try:
            sp.volume(DESIRED_VOLUME, device_id=device_id)
            print(f"Volume set to {DESIRED_VOLUME} on device {DESIRED_DEVICE_NAME}")
        except Exception as e:
            print(f"Could not set volume: {e}")
    else:
        print(f"Device {DESIRED_DEVICE_NAME} does not support volume control.")

    try:
        main_loop(reader, mappings, lookup, sp, sp_oauth, token_info, device_id)
    except KeyboardInterrupt:
        print("Program terminated")
    finally:
        GPIO.cleanup()
