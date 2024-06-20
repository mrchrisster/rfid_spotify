import time
import RPi.GPIO as GPIO
from mfrc522 import SimpleMFRC522

# Create the SimpleMFRC522 object
reader = SimpleMFRC522()

def write_data_to_card(data):
    try:
        print("Place your card to write data...")
        id, text = reader.read()
        print(f'Found card with UID: {hex(id)}')
        print(f'Existing data on card: {text.strip()}')

        # Writing data to the card
        reader.write(data)
        print("Data written to card.")
    except Exception as e:
        print(f"Error writing data to card: {e}")
    finally:
        if GPIO.getmode() is not None:  # Check if any GPIO mode is set
            GPIO.cleanup()  # Clean up GPIO on exit

def read_data_from_card():
    try:
        print("Waiting for RFID/NFC card...")
        while True:
            try:
                id, text = reader.read()
                if id:
                    print(f'Found card with UID: {hex(id)}')
                    print(f'Read data: {text.strip()}')
                    break
            except Exception as read_error:
                print(f"Error while reading: {read_error}")
            time.sleep(0.5)  # Delay between checks
    except Exception as e:
        print(f"Error reading data from card: {e}")
    finally:
        if GPIO.getmode() is not None:  # Check if any GPIO mode is set
            GPIO.cleanup()  # Clean up GPIO on exit

def extract_spotify_url_part(url):
    prefix = "https://open.spotify.com/"
    if url.startswith(prefix):
        return url[len(prefix):]
    else:
        return url

# Prompt user for input
user_input = input("Enter the data you want to write to the card: ")

# Extract relevant part if it's a Spotify URL
data_to_write = extract_spotify_url_part(user_input)

# Write data to the card
write_data_to_card(data_to_write)

# Read data from the card
read_data_from_card()
