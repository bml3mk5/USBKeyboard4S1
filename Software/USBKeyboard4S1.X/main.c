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

/** INCLUDES *******************************************************/
#include <string.h>
#include "common.h"
#include "usb.h"
#include "usb_host_hid.h"
#include "usb_host_hub.h"

#include "system.h"
#include "app_host_hid_keyboard.h"
#include "timer_1ms.h"
#include "timer_2.h"
#include "interrupt.h"
#include "print_lcd.h"
#include "main.h"

uint8_t key_onoff_flags[20];
uint8_t led_hira_inv;

/****************************************************************************
  Function:
    int main(void)

  Summary:
    main function

  Description:
    main function

  Precondition:
    None

  Parameters:
    None

  Return Values:
    int - exit code for main function

  Remarks:
    None
  ***************************************************************************/
int main(void)
{   
    INTCONbits.MVEC = 1;
    INTCONbits.TPC = 0;
//    IPTMR = 50;
    __builtin_enable_interrupts();

    memset(key_onoff_flags, 0, sizeof(key_onoff_flags));
    led_hira_inv = 0;
//    key_onoff_flags[1] = 2;
    key_onoff_flags[0x71 >> 3] = (1 << (0x71 & 7));    // JIS Keyboard
    ANSELA = 0; // all digital
    ANSELB = 0; // all digital
    TRISB = ~(LAT_Y_N_MASK
        | LAT_BREAK_N_MASK
        | LAT_KATA_LED_MASK
        | LAT_HIRA_LED_MASK
        | LAT_CAPS_LED_MASK
    );
    ODCB = (LAT_Y_N_MASK
        | LAT_BREAK_N_MASK
    );
    LATBSET = (LAT_Y_N_MASK
        | LAT_BREAK_N_MASK
        | LAT_KATA_LED_MASK
        | LAT_HIRA_LED_MASK
    );
    LATBCLR = (LAT_CAPS_LED_MASK
    );

#ifdef DEBUG_ENABLE
    UART_Initialize();
#endif

    TIMER_SetConfiguration(TIMER_CONFIGURATION_1MS);
//    Timer2_Init();
    
    INTR_Init();
    
    //Initialize the stack
    USBHost();

    APP_HostHIDKeyboardInitialize();

    while(1)
    {
        USBHostTasks();
        USBHostHUBTasks();
        USBHostHIDTasks();

        //Application specific tasks
        APP_HostHIDKeyboardTasks();
        
#ifdef DEBUG_ENABLE
        UART_Tasks();
#endif
        // BREAK key
        if (key_onoff_flags[16] & 1) {
            LAT_BREAK_N_SET;
        } else {
            LAT_BREAK_N_CLR;
        }
    }//end while
}//end main



/****************************************************************************
  Function:
    bool USB_ApplicationEventHandler( uint8_t address, USB_EVENT event,
                void *data, uint32_t size )

  Summary:
    This is the application event handler.  It is called when the stack has
    an event that needs to be handled by the application layer rather than
    by the client driver.

  Description:
    This is the application event handler.  It is called when the stack has
    an event that needs to be handled by the application layer rather than
    by the client driver.  If the application is able to handle the event, it
    returns true.  Otherwise, it returns false.

  Precondition:
    None

  Parameters:
    uint8_t address    - Address of device where event occurred
    USB_EVENT event - Identifies the event that occured
    void *data      - Pointer to event-specific data
    uint32_t size      - Size of the event-specific data

  Return Values:
    true    - The event was handled
    false   - The event was not handled

  Remarks:
    The application may also implement an event handling routine if it
    requires knowledge of events.  To do so, it must implement a routine that
    matches this function signature and define the USB_HOST_APP_EVENT_HANDLER
    macro as the name of that function.
  ***************************************************************************/
bool USB_HOST_APP_EVENT_HANDLER ( uint8_t address, USB_EVENT event, void *data, uint32_t size )
{
    switch( (int)event )
    {
        /* Standard USB host events ******************************************/
        case EVENT_VBUS_REQUEST_POWER:
        case EVENT_VBUS_RELEASE_POWER:
        case EVENT_UNSUPPORTED_DEVICE:
        case EVENT_CANNOT_ENUMERATE:
        case EVENT_CLIENT_INIT_ERROR:
        case EVENT_OUT_OF_MEMORY:
        case EVENT_UNSPECIFIED_ERROR:
            return true;
            break;

        /* HUB Class Specific Events ******************************************/
        case EVENT_HUB_ATTACH:
            return true;
            break;

        /* HID Class Specific Events ******************************************/
        case EVENT_HID_RPT_DESC_PARSED:
            if(APP_HostHIDKeyboardReportParser() == true)
            {
                return true;
            }
            break;

        default:
            break;
    }

    return false;

}

