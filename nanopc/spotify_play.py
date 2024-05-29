import os
import sys
import spotipy
from spotipy.oauth2 import SpotifyOAuth
import logging

# Configure logging
logging.basicConfig(level=logging.DEBUG)

# Constants for Spotify credentials and settings
CLIENT_ID = ''
CLIENT_SECRET = ''
REDIRECT_URI = 'http://localhost:8888/callback'
DESIRED_DEVICE_NAME = "Van kitchen counter"  # Replace with your desired device name
DESIRED_VOLUME = 50  # Set your desired volume level (0-100)
CACHE_PATH = os.path.join(os.path.expanduser("~"), ".spotify_token_cache")

# Ensure the cache directory exists
os.makedirs(os.path.dirname(CACHE_PATH), exist_ok=True)

def play_content(sp, device_id, uri):
    if uri.startswith('spotify:track'):
        sp.start_playback(device_id=device_id, uris=[uri])
        logging.info(f"Playing track {uri} on device {DESIRED_DEVICE_NAME}")
    elif uri.startswith('spotify:album'):
        sp.start_playback(device_id=device_id, context_uri=uri)
        logging.info(f"Playing album {uri} on device {DESIRED_DEVICE_NAME}")

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 spotify_play.py <uri>")
        sys.exit(1)

    uri = sys.argv[1]

    try:
        # Authenticate and get a token
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

        # Get the current user's devices
        devices = sp.devices()
        logging.debug(f"Devices: {devices}")

        if not devices['devices']:
            print("No devices found")
        else:
            # Find the desired device by name
            device_id = None
            supports_volume = False
            for device in devices['devices']:
                if device['name'] == DESIRED_DEVICE_NAME:
                    device_id = device['id']
                    supports_volume = device['supports_volume']
                    break

            if device_id is None:
                print(f"Device with name '{DESIRED_DEVICE_NAME}' not found.")
            else:
                # Set the volume to the desired level if supported
                if supports_volume:
                    try:
                        sp.volume(DESIRED_VOLUME, device_id=device_id)
                        logging.info(f"Volume set to {DESIRED_VOLUME} on device {DESIRED_DEVICE_NAME}")
                    except Exception as e:
                        logging.error(f"Could not set volume: {e}")
                else:
                    logging.info(f"Device {DESIRED_DEVICE_NAME} does not support volume control.")

                # Play the specified content
                play_content(sp, device_id, uri)

    except Exception as e:
        logging.error(f"An error occurred: {e}")

if __name__ == "__main__":
    main()
