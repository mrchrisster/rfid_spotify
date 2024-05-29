import wiringpi as wp
import spidev
import time
import csv
import subprocess
import logging

# Configure logging
logging.basicConfig(level=logging.DEBUG)

class MFRC522:
    NRSTPD = 0  # WiringPi pin for RST
    MAX_LEN = 16

    PCD_IDLE       = 0x00
    PCD_AUTHENT    = 0x0E
    PCD_RECEIVE    = 0x08
    PCD_TRANSMIT   = 0x04
    PCD_TRANSCEIVE = 0x0C
    PCD_RESETPHASE = 0x0F
    PCD_CALCCRC    = 0x03

    PICC_REQIDL    = 0x26
    PICC_REQALL    = 0x52
    PICC_ANTICOLL  = 0x93
    PICC_SElECTTAG = 0x93
    PICC_AUTHENT1A = 0x60
    PICC_AUTHENT1B = 0x61
    PICC_READ      = 0x30
    PICC_WRITE     = 0xA0
    PICC_DECREMENT = 0xC0
    PICC_INCREMENT = 0xC1
    PICC_RESTORE   = 0xC2
    PICC_TRANSFER  = 0xB0
    PICC_HALT      = 0x50

    MI_OK       = 0
    MI_NOTAGERR = 1
    MI_ERR      = 2

    def __init__(self, spi_bus=1, spi_device=0):
        self.spi = spidev.SpiDev()
        try:
            self.spi.open(spi_bus, spi_device)
        except FileNotFoundError:
            raise RuntimeError(f"SPI device not found. Ensure SPI is enabled and /dev/spidev{spi_bus}.{spi_device} exists.")

        self.spi.max_speed_hz = 1000000

        wp.wiringPiSetup()
        wp.pinMode(self.NRSTPD, 1)
        wp.digitalWrite(self.NRSTPD, 1)
        
        self.MFRC522_Init()

    def MFRC522_Reset(self):
        self.Write_MFRC522(0x01, self.PCD_RESETPHASE)

    def Write_MFRC522(self, addr, val):
        self.spi.xfer([addr<<1 & 0x7E, val])

    def Read_MFRC522(self, addr):
        val = self.spi.xfer([(addr<<1) | 0x80, 0])
        return val[1]

    def SetBitMask(self, reg, mask):
        tmp = self.Read_MFRC522(reg)
        self.Write_MFRC522(reg, tmp | mask)
        
    def ClearBitMask(self, reg, mask):
        tmp = self.Read_MFRC522(reg)
        self.Write_MFRC522(reg, tmp & (~mask))
        
    def AntennaOn(self):
        temp = self.Read_MFRC522(0x14)
        if ~(temp & 0x03):
            self.SetBitMask(0x14, 0x03)

    def AntennaOff(self):
        self.ClearBitMask(0x14, 0x03)

    def MFRC522_ToCard(self, command, send_data):
        back_data = []
        back_len = 0
        status = self.MI_ERR
        irq_en = 0x00
        wait_irq = 0x00
        last_bits = None
        n = 0
        i = 0

        if command == self.PCD_AUTHENT:
            irq_en = 0x12
            wait_irq = 0x10
        if command == self.PCD_TRANSCEIVE:
            irq_en = 0x77
            wait_irq = 0x30

        self.Write_MFRC522(0x02, irq_en | 0x80)
        self.ClearBitMask(0x04, 0x80)
        self.SetBitMask(0x0A, 0x80)
        self.Write_MFRC522(0x01, self.PCD_IDLE)

        while(i<len(send_data)):
            self.Write_MFRC522(0x09, send_data[i])
            i = i+1

        self.Write_MFRC522(0x01, command)

        if command == self.PCD_TRANSCEIVE:
            self.SetBitMask(0x0D, 0x80)

        i = 2000
        while True:
            n = self.Read_MFRC522(0x04)
            i = i-1
            if ~((i!=0) and ~(n&0x01) and ~(n&wait_irq)):
                break

        self.ClearBitMask(0x0D, 0x80)

        if i != 0:
            if (self.Read_MFRC522(0x06) & 0x1B) == 0x00:
                status = self.MI_OK

                if n & irq_en & 0x01:
                    status = self.MI_NOTAGERR

                if command == self.PCD_TRANSCEIVE:
                    n = self.Read_MFRC522(0x0A)
                    last_bits = self.Read_MFRC522(0x0C) & 0x07
                    if last_bits != 0:
                        back_len = (n-1)*8 + last_bits
                    else:
                        back_len = n*8

                    if n == 0:
                        n = 1
                    if n > self.MAX_LEN:
                        n = self.MAX_LEN

                    i = 0
                    while i<n:
                        back_data.append(self.Read_MFRC522(0x09))
                        i = i + 1
            else:
                status = self.MI_ERR

        return (status, back_data, back_len)

    def MFRC522_Request(self, req_mode):
        status = None
        back_bits = None
        tag_type = [req_mode]
        self.Write_MFRC522(0x0D, 0x07)
        (status, back_data, back_bits) = self.MFRC522_ToCard(self.PCD_TRANSCEIVE, tag_type)

        if ((status != self.MI_OK) | (back_bits != 0x10)):
            status = self.MI_ERR
        
        return (status, back_bits)

    def MFRC522_Anticoll(self):
        back_data = []
        ser_num_check = 0
        ser_num = []

        self.Write_MFRC522(0x0D, 0x00)
        ser_num.append(self.PICC_ANTICOLL)
        ser_num.append(0x20)
        (status, back_data, back_len) = self.MFRC522_ToCard(self.PCD_TRANSCEIVE, ser_num)

        if(status == self.MI_OK):
            i = 0
            if len(back_data) == 5:
                while i<4:
                    ser_num_check = ser_num_check ^ back_data[i]
                    i = i + 1
                if ser_num_check != back_data[i]:
                    status = self.MI_ERR
            else:
                status = self.MI_ERR

        return (status, back_data)

    def MFRC522_Init(self):
        wp.digitalWrite(self.NRSTPD, 1)
        self.MFRC522_Reset()
        
        self.Write_MFRC522(0x2A, 0x8D)
        self.Write_MFRC522(0x2B, 0x3E)
        self.Write_MFRC522(0x2D, 30)
        self.Write_MFRC522(0x2C, 0)
        self.Write_MFRC522(0x15, 0x40)
        self.Write_MFRC522(0x11, 0x3D)
        
        self.AntennaOn()

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
        try:
            (status, TagType) = reader.MFRC522_Request(reader.PICC_REQIDL)
            if status == reader.MI_OK:
                logging.info("Card detected")
            (status, uid) = reader.MFRC522_Anticoll()
            if status == reader.MI_OK:
                logging.info(f"Raw UID bytes: {uid}")
                uid_int = int.from_bytes(uid, byteorder='big')
                uid_str = str(uid_int).strip()
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
    reader = MFRC522(spi_bus=1, spi_device=0)
    logging.info("RFID-RC522 initialized")

    mappings = load_mappings()

    try:
        main_loop(reader, mappings)
    except KeyboardInterrupt:
        logging.info("Program terminated")
        wp.digitalWrite(reader.NRSTPD, 0)
