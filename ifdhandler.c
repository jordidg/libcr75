/*****************************************************************
/
/ File   :   ifdhandler.c
/ Author :   David Corcoran <corcoran@linuxnet.com>
/ Date   :   June 15, 2000
/ Purpose:   This provides reader specific low-level calls.
/            See http://www.linuxnet.com for more information.
/ License:   See file LICENSE
/
******************************************************************/

#include <pcscdefines.h>
#include <ifdhandler.h>
#include <syslog.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <libusb-1.0/libusb.h>

#define VENDOR_ID 0x1307
#define PRODUCT_ID 0x0361
#define INTERFACE 1
#define TIMEOUT 5000 /* timeout in ms */
#define BUFFER_SIZE 16

#define INS_SELECT_FILE 0xa4
#define INS_READ_BINARY 0xb0

libusb_context *ctx = NULL;
libusb_device_handle *handle = NULL;
RESPONSECODE card_present = IFD_ICC_NOT_PRESENT;
pthread_t card_monitor = NULL;

void log_command(const char *prefix, const PUCHAR in, DWORD length) {
    char out[4 * length * sizeof(char) + 5];
    strcpy(out, "");

    int i;
    for(i=0; i<length; i++) {
        sprintf(&out[strlen(out)], "%02X", in[i]);
        if(i < length - 1) {
            strcat(out, " ");
        }
    }

    strcat(out, " [");
    for(i=0; i<length; i++) {
        if(isprint(in[i])) {
            strncat(out, &in[i], 1);
        } else {
            strcat(out, ".");
        }
    }
    strcat(out, "]");

    syslog(LOG_DEBUG, "%s %s", prefix, out);
}

void *MonitorCardPresence(void *arg) {
    unsigned char buffer[1];
    int len = NULL;

    while(1) {
        int err = libusb_interrupt_transfer(handle, 0x84, buffer, sizeof(buffer), &len, 0);
        if(err) {
            syslog(LOG_ERR, "Error %i while quering card presence.", err);
            card_present = IFD_COMMUNICATION_ERROR;
            if(err == LIBUSB_ERROR_NO_DEVICE)
                return err;
            continue;
        }

        if(len == 1 && buffer[0] == 0x01) {
            syslog(LOG_DEBUG, "Card detected");
            card_present = IFD_ICC_PRESENT;
        } else {
            syslog(LOG_DEBUG, "Card not present");
            card_present = IFD_ICC_NOT_PRESENT;
        }
    }
}


RESPONSECODE IFDHCreateChannel ( DWORD Lun, DWORD Channel ) {
  /* Lun - Logical Unit Number, use this for multiple card slots 
     or multiple readers. 0xXXXXYYYY -  XXXX multiple readers,
     YYYY multiple slots. The resource manager will set these 
     automatically.  By default the resource manager loads a new
     instance of the driver so if your reader does not have more than
     one smartcard slot then ignore the Lun in all the functions.
     Future versions of PC/SC might support loading multiple readers
     through one instance of the driver in which XXXX would be important
     to implement if you want this.
  */
  
  /* Channel - Channel ID.  This is denoted by the following:
     0x000001 - /dev/pcsc/1
     0x000002 - /dev/pcsc/2
     0x000003 - /dev/pcsc/3
     
     USB readers may choose to ignore this parameter and query 
     the bus for the particular reader.
  */

  /* This function is required to open a communications channel to the 
     port listed by Channel.  For example, the first serial reader on COM1 would
     link to /dev/pcsc/1 which would be a sym link to /dev/ttyS0 on some machines
     This is used to help with intermachine independance.
     
     Once the channel is opened the reader must be in a state in which it is possible
     to query IFDHICCPresence() for card status.
 
     returns:

     IFD_SUCCESS
     IFD_COMMUNICATION_ERROR
  */
    syslog(LOG_DEBUG, "IFDHCreateChannel");
    if(libusb_init(&ctx) != 0)
        return IFD_COMMUNICATION_ERROR;

    handle = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if(handle == NULL)
        return IFD_COMMUNICATION_ERROR;

    int err = libusb_claim_interface(handle, INTERFACE);
    if(err) {
        syslog(LOG_ERR, "Error %i while claiming interface", err);
        return IFD_COMMUNICATION_ERROR;
    }

    if(pthread_create(&card_monitor, NULL, MonitorCardPresence, NULL)) {
        syslog(LOG_ERR, "Error creation card presence monitor thread");
        return IFD_COMMUNICATION_ERROR;
    }

    return IFD_SUCCESS;
}

RESPONSECODE IFDHCloseChannel ( DWORD Lun ) {
  
  /* This function should close the reader communication channel
     for the particular reader.  Prior to closing the communication channel
     the reader should make sure the card is powered down and the terminal
     is also powered down.

     returns:

     IFD_SUCCESS
     IFD_COMMUNICATION_ERROR     
  */
    pthread_cancel(card_monitor);
    libusb_release_interface(handle, INTERFACE);
    libusb_close(handle);
    libusb_exit(ctx);
    return IFD_SUCCESS;
}

RESPONSECODE IFDHGetCapabilities ( DWORD Lun, DWORD Tag, 
				   PDWORD Length, PUCHAR Value ) {
  
  /* This function should get the slot/card capabilities for a particular
     slot/card specified by Lun.  Again, if you have only 1 card slot and don't mind
     loading a new driver for each reader then ignore Lun.

     Tag - the tag for the information requested
         example: TAG_IFD_ATR - return the Atr and it's size (required).
         these tags are defined in ifdhandler.h

     Length - the length of the returned data
     Value  - the value of the data

     returns:
     
     IFD_SUCCESS
     IFD_ERROR_TAG
  */
  syslog(LOG_DEBUG, "IFDHGetCapabilities: %#06lX", Tag);
  switch(Tag) {
        case TAG_IFD_SIMULTANEOUS_ACCESS: {
            *Length = 1;
            *Value = 0;
            break;
        }
        case TAG_IFD_SLOTS_NUMBER: {
            *Length = 1;
            *Value = 1;
            break;
        }
        default:
            return IFD_ERROR_TAG;
    }
    return IFD_SUCCESS;
}

RESPONSECODE IFDHSetCapabilities ( DWORD Lun, DWORD Tag, 
			       DWORD Length, PUCHAR Value ) {

  /* This function should set the slot/card capabilities for a particular
     slot/card specified by Lun.  Again, if you have only 1 card slot and don't mind
     loading a new driver for each reader then ignore Lun.

     Tag - the tag for the information needing set

     Length - the length of the returned data
     Value  - the value of the data

     returns:
     
     IFD_SUCCESS
     IFD_ERROR_TAG
     IFD_ERROR_SET_FAILURE
     IFD_ERROR_VALUE_READ_ONLY
  */
  syslog(LOG_DEBUG, "IFDHSetCapabilities");
  
}

RESPONSECODE IFDHSetProtocolParameters ( DWORD Lun, DWORD Protocol, 
				   UCHAR Flags, UCHAR PTS1,
				   UCHAR PTS2, UCHAR PTS3) {

  /* This function should set the PTS of a particular card/slot using
     the three PTS parameters sent

     Protocol  - 0 .... 14  T=0 .... T=14
     Flags     - Logical OR of possible values:
     IFD_NEGOTIATE_PTS1 IFD_NEGOTIATE_PTS2 IFD_NEGOTIATE_PTS3
     to determine which PTS values to negotiate.
     PTS1,PTS2,PTS3 - PTS Values.

     returns:

     IFD_SUCCESS
     IFD_ERROR_PTS_FAILURE
     IFD_COMMUNICATION_ERROR
     IFD_PROTOCOL_NOT_SUPPORTED
  */
  syslog(LOG_DEBUG, "IFDHSetProtocolParameters: Protocol %lu, Flags %i, PTS1 %i, PTS2 %i, PTS3 %i", Protocol, Flags, PTS1, PTS2, PTS3);
  return IFD_SUCCESS;

}

int writeMessage(PUCHAR msg, size_t length) {
    log_command(">", msg, length);

    libusb_control_transfer(handle, 0x40, 192, 0xffff, length, 0, 0, TIMEOUT);

    int transferred;
    libusb_bulk_transfer(handle, 0x05, msg, length, &transferred, TIMEOUT);
}

int readMessage(int expected_length, PUCHAR msg) {
    libusb_control_transfer(handle, 0x40, 193, 0xffff, expected_length, 0, 0, TIMEOUT);

    int transferred;
    int total_transferred = 0;
    UCHAR buffer[BUFFER_SIZE];
    while(total_transferred < expected_length) {
        libusb_bulk_transfer(handle, 0x86, buffer, sizeof(buffer), &transferred, TIMEOUT);
        memcpy(&msg[total_transferred], buffer, transferred);
        total_transferred += transferred;
    }

    log_command("<", msg, total_transferred);
}


RESPONSECODE IFDHPowerICC ( DWORD Lun, DWORD Action, 
			    PUCHAR Atr, PDWORD AtrLength ) {

  /* This function controls the power and reset signals of the smartcard reader
     at the particular reader/slot specified by Lun.

     Action - Action to be taken on the card.

     IFD_POWER_UP - Power and reset the card if not done so 
     (store the ATR and return it and it's length).
 
     IFD_POWER_DOWN - Power down the card if not done already 
     (Atr/AtrLength should
     be zero'd)
 
    IFD_RESET - Perform a quick reset on the card.  If the card is not powered
     power up the card.  (Store and return the Atr/Length)

     Atr - Answer to Reset of the card.  The driver is responsible for caching
     this value in case IFDHGetCapabilities is called requesting the ATR and it's
     length.  This should not exceed MAX_ATR_SIZE.

     AtrLength - Length of the Atr.  This should not exceed MAX_ATR_SIZE.

     Notes:

     Memory cards without an ATR should return IFD_SUCCESS on reset
     but the Atr should be zero'd and the length should be zero

     Reset errors should return zero for the AtrLength and return 
     IFD_ERROR_POWER_ACTION.

     returns:

     IFD_SUCCESS
     IFD_ERROR_POWER_ACTION
     IFD_COMMUNICATION_ERROR
     IFD_NOT_SUPPORTED
  */
    syslog(LOG_DEBUG, "IFDHPowerICC");
    switch(Action) {
        case IFD_RESET:
        case IFD_POWER_UP: {
            unsigned char buffer[BUFFER_SIZE];
            libusb_control_transfer(handle, 0xc0, 161, 0xffff, 0xffff, buffer, sizeof(buffer), TIMEOUT);
            *AtrLength = buffer[0];

            int transferred;
            int err = libusb_bulk_transfer(handle, 0x86, buffer, sizeof(buffer), &transferred, TIMEOUT);
            if(err) {
                syslog(LOG_ERR, "Error %i while resetting card", err);
                return IFD_COMMUNICATION_ERROR;
            }
            if(*AtrLength != transferred) {
                syslog(LOG_ERR, "Read invalid");
                return IFD_COMMUNICATION_ERROR;
            }

            memcpy(Atr, buffer, transferred);

            UCHAR command[] = "\xff\x10\x13\xfc";
            writeMessage((PUCHAR) command, strlen((char*) command));
            PUCHAR msg = (PUCHAR) malloc(strlen((char*) command) * sizeof(UCHAR));
            readMessage(strlen((char*) command), msg);
            if(strncmp((char*) command, (char*) msg, strlen((char*) command))) {
                syslog(LOG_ERR, "Read invalid");
                free(msg);
                return IFD_COMMUNICATION_ERROR;
            }
            free(msg);

            libusb_control_transfer(handle, 0x40, 165, 0xffff, 0xffff, (unsigned char*) "\x00\x13", 2, TIMEOUT);
            return IFD_SUCCESS;
        }
  }

}

RESPONSECODE IFDHTransmitToICC ( DWORD Lun, SCARD_IO_HEADER SendPci, 
				 PUCHAR TxBuffer, DWORD TxLength, 
				 PUCHAR RxBuffer, PDWORD RxLength, 
				 PSCARD_IO_HEADER RecvPci ) {
  
  /* This function performs an APDU exchange with the card/slot specified by
     Lun.  The driver is responsible for performing any protocol specific exchanges
     such as T=0/1 ... differences.  Calling this function will abstract all protocol
     differences.

     SendPci
     Protocol - 0, 1, .... 14
     Length   - Not used.

     TxBuffer - Transmit APDU example (0x00 0xA4 0x00 0x00 0x02 0x3F 0x00)
     TxLength - Length of this buffer.
     RxBuffer - Receive APDU example (0x61 0x14)
     RxLength - Length of the received APDU.  This function will be passed
     the size of the buffer of RxBuffer and this function is responsible for
     setting this to the length of the received APDU.  This should be ZERO
     on all errors.  The resource manager will take responsibility of zeroing
     out any temporary APDU buffers for security reasons.
  
     RecvPci
     Protocol - 0, 1, .... 14
     Length   - Not used.

     Notes:
     The driver is responsible for knowing what type of card it has.  If the current
     slot/card contains a memory card then this command should ignore the Protocol
     and use the MCT style commands for support for these style cards and transmit 
     them appropriately.  If your reader does not support memory cards or you don't
     want to then ignore this.

     RxLength should be set to zero on error.

     returns:
     
     IFD_SUCCESS
     IFD_COMMUNICATION_ERROR
     IFD_RESPONSE_TIMEOUT
     IFD_ICC_NOT_PRESENT
     IFD_PROTOCOL_NOT_SUPPORTED
  */
    syslog(LOG_DEBUG, "IFDHTransmitToICC");
    writeMessage(TxBuffer, 5);

    PUCHAR sw1 = (PUCHAR) malloc(sizeof(UCHAR));
    readMessage(1, sw1);

    if(TxLength > 5) {
        if(*sw1 != INS_SELECT_FILE) {
            *RxLength = 0;
            return IFD_COMMUNICATION_ERROR;
        }
        writeMessage(&TxBuffer[5], TxLength - 5);
        readMessage(1, sw1);
    }

    if(*sw1 == INS_READ_BINARY) {
        size_t response_length = (UCHAR) TxBuffer[4] + 2;
        PUCHAR sw2 = (PUCHAR) malloc(response_length * sizeof(UCHAR));
        readMessage(response_length, sw2);

        memcpy(RxBuffer, sw2, response_length);
        *RxLength = response_length;

        free(sw2);
    } else {
        PUCHAR sw2 = (PUCHAR) malloc(sizeof(UCHAR));
        readMessage(1, sw2);

        memcpy(RxBuffer, sw1, 1);
        memcpy(&RxBuffer[1], sw2, 1);
        *RxLength = 2;

        free(sw2);
    }

    free(sw1);

    return IFD_SUCCESS;
}

RESPONSECODE IFDHControl ( DWORD Lun, PUCHAR TxBuffer, 
			 DWORD TxLength, PUCHAR RxBuffer, 
			 PDWORD RxLength ) {

  /* This function performs a data exchange with the reader (not the card)
     specified by Lun.  Here XXXX will only be used.
     It is responsible for abstracting functionality such as PIN pads,
     biometrics, LCD panels, etc.  You should follow the MCT, CTBCS 
     specifications for a list of accepted commands to implement.

     TxBuffer - Transmit data
     TxLength - Length of this buffer.
     RxBuffer - Receive data
     RxLength - Length of the received data.  This function will be passed
     the length of the buffer RxBuffer and it must set this to the length
     of the received data.

     Notes:
     RxLength should be zero on error.
  */
    syslog(LOG_DEBUG, "IFDHControl");

}

RESPONSECODE IFDHICCPresence( DWORD Lun ) {
  /* This function returns the status of the card inserted in the 
     reader/slot specified by Lun.  It will return either:

     returns:
     IFD_ICC_PRESENT
     IFD_ICC_NOT_PRESENT
     IFD_COMMUNICATION_ERROR
  */
    return card_present;
}
