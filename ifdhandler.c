/*****************************************************************
/
/ File   :   ifdhandler.c
/ Author :   Jordi De Groof <jordi.degroof@gmail.com>
/ Date   :   January 14, 2016
/ Purpose:   This provides reader specific low-level calls.
/ License:   See file COPYING
/
******************************************************************/

#include "ifdhandler.h"
#include <syslog.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <libusb.h>

#define VENDOR_ID 0x1307
#define PRODUCT_ID 0x0361
#define INTERFACE 1
#define TIMEOUT 5000 /* timeout in ms */
#define BUFFER_SIZE 16

#define CHECK(x) do { \
    RESPONSECODE retval = (x); \
    if (retval != 0) { \
        return retval; \
    } \
} while (0)
#define CHECK_LIBUSB(x) do { \
    int retval = (x); \
    if (retval < 0) { \
        return libusb_error_to_responsecode(retval); \
    } \
} while (0)

#ifdef __APPLE__
#define PRIdword "u"
#else
#define PRIdword "lu"
#endif

libusb_context *ctx = NULL;
libusb_device_handle *handle = NULL;
pthread_t card_monitor;

RESPONSECODE card_present = IFD_ICC_NOT_PRESENT;
UCHAR cached_Atr[MAX_ATR_SIZE];
DWORD cached_AtrLength = 0;

void log_command(const char *prefix, const PUCHAR in, DWORD length) {
#ifdef DEBUG
        // 2 + 1 characters + 1 space for every byte
        // 3 characters for brackets + NULL
        char out[4 * length + 3];
        strcpy(out, "");

        DWORD i;
        for(i=0; i<length; i++) {
            sprintf(&out[3*i], "%02X ", in[i]);
        }

        strcat(out, "[");
        for(i=0; i<length; i++) {
            if(isprint(in[i])) {
                strncat(out, (char*) &in[i], 1);
            } else {
                strcat(out, ".");
            }
        }
        strcat(out, "]");

        syslog(LOG_DEBUG, "%s %s", prefix, out);
#endif
}

void *MonitorCardPresence(void *arg) {
    unsigned char buffer[1];
    int len;

    while(1) {
        int err = libusb_interrupt_transfer(handle, 0x84, buffer, sizeof(buffer), &len, 0);
        if(err) {
            syslog(LOG_ERR, "Error %i while quering card presence.", err);
            card_present = IFD_COMMUNICATION_ERROR;
            if(err == LIBUSB_ERROR_NO_DEVICE)
                return NULL;
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
  switch(Tag) {
        case TAG_IFD_ATR: {
            *Length = cached_AtrLength;
            memcpy(Value, cached_Atr, cached_AtrLength);
            break;
        }
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
  return IFD_NOT_SUPPORTED;
  
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
  syslog(LOG_DEBUG, "IFDHSetProtocolParameters: Protocol %"PRIdword", Flags %i, PTS1 %i, PTS2 %i, PTS3 %i", Protocol, Flags, PTS1, PTS2, PTS3);
  return IFD_SUCCESS;

}

RESPONSECODE libusb_error_to_responsecode(const int err) {
    switch(err) {
        case LIBUSB_ERROR_TIMEOUT:
            return IFD_RESPONSE_TIMEOUT;
        case LIBUSB_ERROR_NO_DEVICE:
            return IFD_NO_SUCH_DEVICE;
        case LIBUSB_ERROR_PIPE:
        case LIBUSB_ERROR_OVERFLOW:
        default:
            return IFD_COMMUNICATION_ERROR;
    }
}

RESPONSECODE writeMessage(PUCHAR msg, size_t length) {
    log_command(">", msg, length);

    CHECK_LIBUSB(libusb_control_transfer(handle, 0x40, 192, 0xffff, length, 0, 0, TIMEOUT));

    int transferred;
    DWORD i;
    for(i = 0; i < length; i+= BUFFER_SIZE) {
        DWORD bytes_remaining = length - i;
        DWORD msg_length = (bytes_remaining < BUFFER_SIZE) ? bytes_remaining : BUFFER_SIZE;
        CHECK_LIBUSB(libusb_bulk_transfer(handle, 0x05, &msg[i], msg_length, &transferred, TIMEOUT));
    }
    return IFD_SUCCESS;
}

RESPONSECODE readMessage(int expected_length, PUCHAR msg) {
    CHECK_LIBUSB(libusb_control_transfer(handle, 0x40, 193, 0xffff, expected_length, 0, 0, TIMEOUT));

    int transferred;
    int total_transferred = 0;
    UCHAR buffer[BUFFER_SIZE];
    while(total_transferred < expected_length) {
        CHECK_LIBUSB(libusb_bulk_transfer(handle, 0x86, buffer, sizeof(buffer), &transferred, TIMEOUT));
        memcpy(&msg[total_transferred], buffer, transferred);
        total_transferred += transferred;
    }

    log_command("<", msg, total_transferred);
    return IFD_SUCCESS;
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
            CHECK_LIBUSB(libusb_control_transfer(handle, 0xc0, 161, 0xffff, 0xffff, buffer, sizeof(buffer), TIMEOUT));

            *AtrLength = buffer[0];

            int transferred;
            CHECK_LIBUSB(libusb_bulk_transfer(handle, 0x86, buffer, sizeof(buffer), &transferred, TIMEOUT));

            if(*AtrLength != transferred) {
                syslog(LOG_ERR, "Read invalid");
                return IFD_COMMUNICATION_ERROR;
            }

            cached_AtrLength = *AtrLength;
            memcpy(Atr, buffer, transferred);
            memcpy(cached_Atr, buffer, cached_AtrLength);

            UCHAR command[] = {0xFF, 0x10, 0x13, 0xFC};
            CHECK(writeMessage(command, sizeof(command)));

            UCHAR msg[sizeof(command)];
            CHECK(readMessage(sizeof(command), msg));
            if(memcmp(command, msg, sizeof(command))) {
                syslog(LOG_ERR, "Read invalid");
                return IFD_COMMUNICATION_ERROR;
            }

            CHECK_LIBUSB(libusb_control_transfer(handle, 0x40, 165, 0xffff, 0xffff, (unsigned char*) "\x00\x13", 2, TIMEOUT));
            return IFD_SUCCESS;
        }
  }
    return IFD_NOT_SUPPORTED;

}

void apdu_message_length(PUCHAR TxBuffer, DWORD TxLength, unsigned int *Lc, unsigned int *Le) {
    // http://www.cardwerk.com/smartcards/smartcard_standard_ISO7816-4_5_basic_organizations.aspx#table5

    DWORD L = TxLength - 4; // Fixed 4-bytes header
    UCHAR B1 = TxBuffer[4];

    if(L == 0) {
        *Lc = 0;
        *Le = 0;
    } else if(L == 1) {
        *Lc = 0;
        *Le = (TxBuffer[4]) ? TxBuffer[4] : 256;
    } else if(L == (1 + B1) && B1 != 0) {
        *Lc = B1;
        *Le = 0;
    } else if(L == (2 + B1) && B1 != 0) {
        *Lc = B1;
        *Le = (TxBuffer[TxLength - 1]) ? TxBuffer[TxLength - 1] : 256;
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

    unsigned int Lc, Le;
    apdu_message_length(TxBuffer, TxLength, &Lc, &Le);

    if(TxLength >= 5) {
        CHECK(writeMessage(TxBuffer, 5));
    } else {
        UCHAR tmpTxBuffer[5] = { 0 };
        memcpy(tmpTxBuffer, TxBuffer, TxLength);
        CHECK(writeMessage(tmpTxBuffer, 5));
    }

    CHECK(readMessage(1, RxBuffer));

    if(Lc > 0) {
        CHECK(writeMessage(&TxBuffer[5], Lc));
        CHECK(readMessage(1, RxBuffer));
    }

    if(Le == 0 || RxBuffer[0] == 0x6c) {
        CHECK(readMessage(1, &RxBuffer[1]));
        *RxLength = 2;
    } else {
        size_t response_length = (UCHAR) TxBuffer[4] + 2; // Data + SW1 + SW2
        if(TxLength == 5 && TxBuffer[4] == 0) {
            response_length = 258;
        }
        CHECK(readMessage(response_length, RxBuffer));
        *RxLength = response_length;
    }

    return IFD_SUCCESS;
}

RESPONSECODE IFDHControl ( DWORD Lun, DWORD dwControlCode,
                           PUCHAR TxBuffer, DWORD TxLength,
                           PUCHAR RxBuffer, DWORD RxLength,
                           PDWORD pdwBytesReturned ) {

    syslog(LOG_DEBUG, "IFDHControl");
    return IFD_NOT_SUPPORTED;
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
