/*******************************************************************************
Copyright 2016 Microchip Technology Inc. (www.microchip.com)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

To request to license the code under the MLA license (www.microchip.com/mla_license), 
please contact mla_licensing@microchip.com
 *******************************************************************************/
/**
 * Modified by Sasaji at 2018/02/10
 */

#include "../common.h"
#include "usb.h"
#include "usb_host_hid.h"

#include <stdint.h>
#include <stdbool.h>
#include "system.h"
//#include <stdlib.h>
//#include <stdio.h>
#include <string.h>
#include "print_lcd.h"
#include "timer_1ms.h"
#include "main.h"

// *****************************************************************************
// *****************************************************************************
// Type definitions
// *****************************************************************************
// *****************************************************************************

typedef enum _APP_STATE {
    DEVICE_NOT_CONNECTED,
    WAITING_FOR_DEVICE,
    DEVICE_CONNECTED, /* Device Enumerated  - Report Descriptor Parsed */
    GET_INPUT_REPORT, /* perform operation on received report */
    INPUT_REPORT_PENDING,
    SEND_OUTPUT_REPORT, /* Not needed in case of mouse */
    OUTPUT_REPORT_PENDING,
    ERROR_REPORTED
} KEYBOARD_STATE;

typedef struct {
    uint8_t address;
    KEYBOARD_STATE state;
    bool inUse;

    struct {
        uint16_t id;
        uint16_t size;
        uint16_t pollRate;
        uint8_t *buffer;

        struct {

            struct {
                HID_DATA_DETAILS details;
//                HID_USER_DATA_SIZE newData[6];
//                HID_USER_DATA_SIZE oldData[6];
                HID_USER_DATA_SIZE data[6];
            } parsed;
        } normal;

        struct {

            struct {
                HID_USER_DATA_SIZE data[8];
                HID_DATA_DETAILS details;
            } parsed;
        } modifier;
    } keys;

    struct {
        bool updated;

        union {
            uint8_t value;

            struct {
                uint8_t numLock : 1;
                uint8_t capsLock : 1;
                uint8_t scrollLock : 1;
uint8_t:
                5;
            } bits;
        } report;

        struct {
            HID_DATA_DETAILS details;
        } parsed;
    } leds;
} KEYBOARD;

typedef struct {
    USB_HID_KEYBOARD_KEYPAD key;
    char unmodified;
    char modified;
} HID_KEY_TRANSLATION_TABLE_ENTRY;

#define MAX_ERROR_COUNTER               (10)

// *****************************************************************************
// *****************************************************************************
// Local Variables
// *****************************************************************************
// *****************************************************************************

static KEYBOARD keyboard;

// for Japanese keyboard
// F9 -> Break key
static const uint8_t key2scancodeTable[256] = {
//  0     1     2     3     4     5     6     7     8     9     a     b     c     d     e     f
//0                         A     B     C     D     E     F     G     H     I     J     K     L
    0xff, 0xff, 0xff, 0xff, 0x38, 0x49, 0x4b, 0x3b, 0x2b, 0x31, 0x39, 0x32, 0x23, 0x30, 0x33, 0x3c,
//1 M     N     O     P     Q     R     S     T     U     V     W     X     Y     Z     1!    2"
    0x40, 0x42, 0x2c, 0x24, 0x28, 0x21, 0x3a, 0x29, 0x20, 0x41, 0x2a, 0x4a, 0x22, 0x48, 0x1a, 0x1b,
//2 3#    4$    5%    6&    7'    8(    9)    0     ENTER ESC   BS    TAB   SPACE -=    ^~    @`
    0x17, 0x11, 0x19, 0x12, 0x10, 0x13, 0x1c, 0x14, 0x2f, 0x0c, 0x1e, 0x6c, 0x00, 0x16, 0x15, 0x26,
//3 [     \\    ]     ;+    :*    Zen   ,<    .>    /?    CAPS  F1    F2    F3    F4    F5    F6
    0x25, 0x1f, 0x35, 0x34, 0x36, 0xff, 0x43, 0x4c, 0x44, 0x09, 0x50, 0x51, 0x52, 0x53, 0x54, 0x67,
//4 F7    F8    F9    F10   F11   F12   PRINT SCROL PAUSE INSER HOME  PAGEU DELET END   PAGED RIGHT
    0x0b, 0x0a, 0x80, 0xff, 0x46, 0x68, 0x68, 0xff, 0xff, 0x65, 0x2e, 0xff, 0x6f, 0x02, 0x6d, 0x05,
//5 LEFT  DOWN  UP    NUMLO num/  num*  num-  num+  numEN num1  num2  num3  num4  num5  num6  num7
    0x03, 0x04, 0x01, 0xff, 0x45, 0x0f, 0x3f, 0x4f, 0x2f, 0x47, 0x4d, 0x4e, 0x37, 0x3d, 0x3e, 0x1d,
//6 num8  num9  num0  num.  \\|   APP   POWER EQSIZ F13   F14   F15   F16   F17   F18   F19   F20
    0x0d, 0x0e, 0x27, 0x2d, 0x1f, 0xff, 0xff, 0xff, 0x65, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
//7 F21   F22   F23   F24   EXEC  HELP  MENU  SELEC STOP  AGAIN UNDO  CUT   COPY  PASTE FIND  MUTE
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
//8 VOLUP VOLDW LCAPS LNUM  LSCRO num,  numEQ (_)   Kata  (\\|) Henka Muhen Inte6 Inte7 Inte8 Inte9
    0xff, 0xff, 0xff, 0xff, 0xff, 0x6d, 0xff, 0x46, 0x0a, 0x1f, 0x67, 0x0b, 0xff, 0xff, 0xff, 0xff,
//9 KanaA EisuA Lang3 Lang4 Lang5 Lang6 Lang7 Lang8 Lang9 ALTES SYSRE CALCE CLEAR PRIOR RETUR SEPAR
    0x0a, 0x0b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x2f, 0xff,
//A OUT   OPER  CLEAA CRSEL EXSEL
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
//B num00 num00 THOUS DECI  CURR  CURRS KEYOP CLOSP OPENC CLOSC numTA numBS numA  numB  numC  numD
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x6c, 0x1e, 0xff, 0xff, 0xff, 0xff,
//C numE  numF  numXO numCA numPE num<  num>  num&  num&& num|  num|| num:  numHA numSP num@  num!
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
//D 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
//E LCTRL LSHIF LALT  LWIN  RCTRL RSHIF RALT  RWIN
    0x06, 0x07, 0xff, 0x0b, 0x06, 0x07, 0x46, 0x0a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};


// *****************************************************************************
// *****************************************************************************
// Local Function Prototypes
// *****************************************************************************
// *****************************************************************************
static void App_ProcessInputReport(void);

// *****************************************************************************
// *****************************************************************************
// Functions
// *****************************************************************************
// *****************************************************************************

/*********************************************************************
 * Function: void APP_HostHIDTimerHandler(void);
 *
 * Overview: Switches over the state machine state to get a new report
 *           periodically if the device is idle
 *
 * PreCondition: None
 *
 * Input: None
 *
 * Output: None
 *
 ********************************************************************/
static void APP_HostHIDTimerHandler(void) {
    if (keyboard.state == DEVICE_CONNECTED) {
        keyboard.state = GET_INPUT_REPORT;
    }
}

static void APP_LED_OK_Handler(void) {
    led_hira_inv = (led_hira_inv ^ 0x04);
}

/*********************************************************************
 * Function: void APP_HostHIDKeyboardInitialize(void);
 *
 * Overview: Initializes the demo code
 *
 * PreCondition: None
 *
 * Input: None
 *
 * Output: None
 *
 ********************************************************************/
void APP_HostHIDKeyboardInitialize() {
    keyboard.state = DEVICE_NOT_CONNECTED;
    keyboard.inUse = false;
    keyboard.keys.buffer = NULL;
    keyboard.address = 0;
}

/*********************************************************************
 * Function: void APP_HostHIDKeyboardTasks(void);
 *
 * Overview: Keeps the demo running.
 *
 * PreCondition: The demo should have been initialized via
 *   the APP_HostHIDKeyboardInitialize()
 *
 * Input: None
 *
 * Output: None
 *
 ********************************************************************/
void APP_HostHIDKeyboardTasks() {
    uint8_t error;
    uint8_t count;

    if (keyboard.address == 0) {
        keyboard.address = USBHostHIDDeviceDetect();
    } else {
        if (USBHostHIDDeviceStatus(keyboard.address) == USB_HID_DEVICE_NOT_FOUND) {
            keyboard.state = DEVICE_NOT_CONNECTED;
            keyboard.address = 0;
            keyboard.inUse = false;

            if (keyboard.keys.buffer != NULL) {
                free(keyboard.keys.buffer);
                keyboard.keys.buffer = NULL;
            }
        }
    }

    switch (keyboard.state) {
        case DEVICE_NOT_CONNECTED:
#ifdef DEBUG_ENABLE
            PRINT_String("Attach keyboard\r\n", 17);
#endif
            memset(&keyboard.keys, 0x00, sizeof (keyboard.keys));
            memset(&keyboard.leds, 0x00, sizeof (keyboard.leds));
            keyboard.state = WAITING_FOR_DEVICE;
            break;

        case WAITING_FOR_DEVICE:
            if ((keyboard.address != 0) &&
               (USBHostHIDDeviceStatus(keyboard.address) == USB_HID_NORMAL_RUNNING)
            ) /* True if report descriptor is parsed with no error */ {
#ifdef DEBUG_ENABLE
                PRINT_String("Connected\r\n", 11);
#endif
                keyboard.state = DEVICE_CONNECTED;
                TIMER_RequestTick(&APP_LED_OK_Handler, 500, 6);
                TIMER_RequestTick(&APP_HostHIDTimerHandler, 10, -1);
            }
            break;

        case DEVICE_CONNECTED:
            break;

        case GET_INPUT_REPORT:
            if (USBHostHIDReadWrite(false,
                    keyboard.address,
                    keyboard.keys.id,
                    keyboard.keys.normal.parsed.details.interfaceNum,
                    keyboard.keys.size,
                    keyboard.keys.buffer
                    )
                    ) {
                /* Host may be busy/error -- keep trying */
            } else {
                keyboard.state = INPUT_REPORT_PENDING;
            }
            break;

        case INPUT_REPORT_PENDING:
            if (USBHostHIDReadWriteIsComplete(false, keyboard.address, &error, &count)) {
                if (error || (count == 0)) {
                    keyboard.state = DEVICE_CONNECTED;
                } else {
                    keyboard.state = DEVICE_CONNECTED;

                    App_ProcessInputReport();
                    if (keyboard.leds.updated == true) {
                        keyboard.state = SEND_OUTPUT_REPORT;
                    }
                }
            }
            break;

        case SEND_OUTPUT_REPORT: /* Will be done while implementing Keyboard */
            if (USBHostHIDReadWrite(true,
                    keyboard.address,
                    keyboard.leds.parsed.details.reportID,
                    keyboard.leds.parsed.details.interfaceNum,
                    keyboard.leds.parsed.details.reportLength,
                    (uint8_t*) & keyboard.leds.report
                    )
                    ) {
                /* Host may be busy/error -- keep trying */
            } else {
                keyboard.state = OUTPUT_REPORT_PENDING;
            }
            break;

        case OUTPUT_REPORT_PENDING:
            if (USBHostHIDReadWriteIsComplete(true, keyboard.address, &error, &count)) {
                keyboard.leds.updated = false;
                keyboard.state = DEVICE_CONNECTED;
            }
            break;

        case ERROR_REPORTED:
            break;

        default:
            break;

    }
}

/****************************************************************************
  Function:
    BOOL USB_HID_DataCollectionHandler(void)
  Description:
    This function is invoked by HID client , purpose is to collect the
    details extracted from the report descriptor. HID client will store
    information extracted from the report descriptor in data structures.
    Application needs to create object for each report type it needs to
    extract.
    For ex: HID_DATA_DETAILS keyboard.keys.modifier.details;
    HID_DATA_DETAILS is defined in file usb_host_hid_appl_interface.h
    Each member of the structure must be initialized inside this function.
    Application interface layer provides functions :
    USBHostHID_ApiFindBit()
    USBHostHID_ApiFindValue()
    These functions can be used to fill in the details as shown in the demo
    code.

  Precondition:
    None

  Parameters:
    None

  Return Values:
    true    - If the report details are collected successfully.
    false   - If the application does not find the the supported format.

  Remarks:
    This Function name should be entered in the USB configuration tool
    in the field "Parsed Data Collection handler".
    If the application does not define this function , then HID cient
    assumes that Application is aware of report format of the attached
    device.
 ***************************************************************************/
bool APP_HostHIDKeyboardReportParser(void) {
    uint8_t NumOfReportItem = 0;
    uint8_t i;
    USB_HID_ITEM_LIST* pitemListPtrs;
    USB_HID_DEVICE_RPT_INFO* pDeviceRptinfo;
    HID_REPORTITEM *reportItem;
    HID_USAGEITEM *hidUsageItem;
    uint8_t usageIndex;
    uint8_t reportIndex;
    bool foundLEDIndicator = false;
    bool foundModifierKey = false;
    bool foundNormalKey = false;

    /* The keyboard is already in use. */
    if (keyboard.inUse == true) {
        return false;
    }

    pDeviceRptinfo = USBHostHID_GetCurrentReportInfo(); // Get current Report Info pointer
    pitemListPtrs = USBHostHID_GetItemListPointers(); // Get pointer to list of item pointers

    /* Find Report Item Index for Modifier Keys */
    /* Once report Item is located , extract information from data structures provided by the parser */
    NumOfReportItem = pDeviceRptinfo->reportItems;
    for (i = 0; i < NumOfReportItem; i++) {
        reportItem = &pitemListPtrs->reportItemList[i];
        if ((reportItem->reportType == hidReportInput) && (reportItem->dataModes == HIDData_Variable)&&
                (reportItem->globals.usagePage == USB_HID_USAGE_PAGE_KEYBOARD_KEYPAD)) {
            /* We now know report item points to modifier keys */
            /* Now make sure usage Min & Max are as per application */
            usageIndex = reportItem->firstUsageItem;
            hidUsageItem = &pitemListPtrs->usageItemList[usageIndex];
            if ((hidUsageItem->usageMinimum == USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_CONTROL)
                    &&(hidUsageItem->usageMaximum == USB_HID_KEYBOARD_KEYPAD_KEYBOARD_RIGHT_GUI)) //else application cannot suuport
            {
                reportIndex = reportItem->globals.reportIndex;
                keyboard.keys.modifier.parsed.details.reportLength = (pitemListPtrs->reportList[reportIndex].inputBits + 7) / 8;
                keyboard.keys.modifier.parsed.details.reportID = (uint8_t) reportItem->globals.reportID;
                keyboard.keys.modifier.parsed.details.bitOffset = (uint8_t) reportItem->startBit;
                keyboard.keys.modifier.parsed.details.bitLength = (uint8_t) reportItem->globals.reportsize;
                keyboard.keys.modifier.parsed.details.count = (uint8_t) reportItem->globals.reportCount;
                keyboard.keys.modifier.parsed.details.interfaceNum = USBHostHID_ApiGetCurrentInterfaceNum();
                foundModifierKey = true;
            }

        } else if ((reportItem->reportType == hidReportInput) && (reportItem->dataModes == HIDData_Array)&&
                (reportItem->globals.usagePage == USB_HID_USAGE_PAGE_KEYBOARD_KEYPAD)) {
            /* We now know report item points to modifier keys */
            /* Now make sure usage Min & Max are as per application */
            usageIndex = reportItem->firstUsageItem;
            hidUsageItem = &pitemListPtrs->usageItemList[usageIndex];
            reportIndex = reportItem->globals.reportIndex;
            keyboard.keys.normal.parsed.details.reportLength = (pitemListPtrs->reportList[reportIndex].inputBits + 7) / 8;
            keyboard.keys.normal.parsed.details.reportID = (uint8_t) reportItem->globals.reportID;
            keyboard.keys.normal.parsed.details.bitOffset = (uint8_t) reportItem->startBit;
            keyboard.keys.normal.parsed.details.bitLength = (uint8_t) reportItem->globals.reportsize;
            keyboard.keys.normal.parsed.details.count = (uint8_t) reportItem->globals.reportCount;
            keyboard.keys.normal.parsed.details.interfaceNum = USBHostHID_ApiGetCurrentInterfaceNum();
            foundNormalKey = true;
        } else if ((reportItem->reportType == hidReportOutput) &&
                (reportItem->globals.usagePage == USB_HID_USAGE_PAGE_LEDS)) {
            usageIndex = reportItem->firstUsageItem;
            hidUsageItem = &pitemListPtrs->usageItemList[usageIndex];

            reportIndex = reportItem->globals.reportIndex;
            keyboard.leds.parsed.details.reportLength = (pitemListPtrs->reportList[reportIndex].outputBits + 7) / 8;
            keyboard.leds.parsed.details.reportID = (uint8_t) reportItem->globals.reportID;
            keyboard.leds.parsed.details.bitOffset = (uint8_t) reportItem->startBit;
            keyboard.leds.parsed.details.bitLength = (uint8_t) reportItem->globals.reportsize;
            keyboard.leds.parsed.details.count = (uint8_t) reportItem->globals.reportCount;
            keyboard.leds.parsed.details.interfaceNum = USBHostHID_ApiGetCurrentInterfaceNum();
            foundLEDIndicator = true;
        }
    }

    if (pDeviceRptinfo->reports == 1) {
        keyboard.keys.id = 0;
        keyboard.keys.size = keyboard.keys.normal.parsed.details.reportLength;
        keyboard.keys.buffer = (uint8_t*) malloc(keyboard.keys.size);
        keyboard.keys.pollRate = pDeviceRptinfo->reportPollingRate;

        if ((foundNormalKey == true) &&
                (foundModifierKey == true) &&
                (keyboard.keys.buffer != NULL)
                ) {
            keyboard.inUse = true;
        }
    }

    return (keyboard.inUse);
}

/****************************************************************************
  Function:
    void App_ProcessInputReport(void)

  Description:
    This function processes input report received from HID device.

  Precondition:
    None

  Parameters:
    None

  Return Values:
    None

  Remarks:
    None
 ***************************************************************************/
static void App_ProcessInputReport(void)
{
    int i;
    uint8_t key;
    uint8_t new_key_onoff_flags[20];

    /* process input report received from device */
    USBHostHID_ApiImportData(keyboard.keys.buffer,
            keyboard.keys.size,
            keyboard.keys.modifier.parsed.data,
            &keyboard.keys.modifier.parsed.details
            );

    USBHostHID_ApiImportData(keyboard.keys.buffer,
            keyboard.keys.size,
            keyboard.keys.normal.parsed.data,
            &keyboard.keys.normal.parsed.details
            );

    memset(new_key_onoff_flags, 0, sizeof(new_key_onoff_flags));
    new_key_onoff_flags[0x71 >> 3]=(1 << (0x71 & 7));

    for (i = 0; i < keyboard.keys.modifier.parsed.details.reportLength && i < 8; i++) {
        if (keyboard.keys.modifier.parsed.data[i] == 1) {
            key = key2scancodeTable[i + USB_HID_KEYBOARD_KEYPAD_KEYBOARD_LEFT_CONTROL];  
            if (key != 0xff) {
                new_key_onoff_flags[key >> 3] |= (1 << (key & 7));
            }
        }
    }

    for (i = 0; i < keyboard.keys.normal.parsed.details.reportLength; i++) {
        key = key2scancodeTable[keyboard.keys.normal.parsed.data[i]];
        if (key != 0xff) {
            new_key_onoff_flags[key >> 3] |= (1 << (key & 7));
        }
    }
    memcpy(key_onoff_flags, new_key_onoff_flags, sizeof(key_onoff_flags));
    
//    UART_PutHexa(keyboard.keys.normal.parsed.data, keyboard.keys.normal.parsed.details.reportLength);
}

void APP_HostHIDUpdateLED(uint8_t led_status)
{
    keyboard.leds.report.bits.numLock = (led_status & 2 ? 1 : 0);
    keyboard.leds.report.bits.scrollLock = (led_status & 4 ? 1 : 0);
    keyboard.leds.report.bits.capsLock = (led_status & 8 ? 0 : 1);
    keyboard.leds.updated = true;
}