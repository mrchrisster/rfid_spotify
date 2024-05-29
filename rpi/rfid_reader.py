import RPi.GPIO as GPIO
from mfrc522 import SimpleMFRC522
import csv
import subprocess
import logging
import time

# Configure logging
logging.basicConfig(level=logging.DEBUG)

# Initialize the RFID reader
reader = SimpleMFRC522()

# Load UID to URI mappings
def load_mappings(filename='rfid_tracks.csv'):
    mappings = {}
    with open(filename, mode='r') as infile:
        reader = csv.reader(infile, delimiter=';')
        next(reader)  # Skip header
        for rows in reader:
            if len(rows) >= 2:
                mappings[rows[0].strip()] = rows[1].strip()
    return mappings

def main_loop(reader, mappings):
    while True:
        logging.debug("Starting main loop iteration")
        try:
            logging.info("Hold a tag near the reader")
            id, text = reader.read()
            uid_str = str(id).strip()
            logging.info(f"Card read UID: {uid_str}")

            if uid_str in mappings:
                uri = mappings[uid_str]
                subprocess.run(['python3', 'spotify_play.py', uri])
            else:
                logging.warning(f"No track mapped for UID {uid_str}")
            time.sleep(1)
        except Exception as e:
            logging.error(f"An error occurred in the main loop: {e}")
            time.sleep(5)  # Delay before restarting the loop

# Main program
if __name__ == "__main__":
    mappings = load_mappings()

    try:
        main_loop(reader, mappings)
    except KeyboardInterrupt:
        logging.info("Program terminated")
    finally:
        GPIO.cleanup()
