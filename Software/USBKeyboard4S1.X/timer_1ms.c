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
*******************************************************************************/
/**
 * Modified by Sasaji at 2018/02/10
 */

#include <xc.h>
#include "common.h"
#include <stdbool.h>
#include <stdint.h>
//#include <stddef.h>
#include <string.h>
#include <sys/attribs.h>
#include "uart.h"
#include "timer_1ms.h"

#ifndef SYSTEM_PERIPHERAL_CLOCK
#define SYSTEM_PERIPHERAL_CLOCK 8000000
#pragma message "This module requires a definition for the peripheral clock frequency.  Assuming 8MHz Fcy (8MHz Fosc).  Define value if this is not correct."
#endif

#define T1SetPriority IPC1bits.T1IP = TIMER_INTERRUPT_PRIORITY;
#define T1ClearInterruptFlag IFS0CLR = _IFS0_T1IF_MASK;
#define T1EnableInterrupt IEC0bits.T1IE = 1;
#define T1DisableInterrupt IEC0bits.T1IE = 0;

#define CLOCK_DIVIDER TIMER_PRESCALER_1
#define PR1_SETTING (SYSTEM_PERIPHERAL_CLOCK/1000/1)

#if (PR1_SETTING > 0xFFFF)
#undef CLOCK_DIVIDER
#undef PR1_SETTING
#define CLOCK_DIVIDER TIMER_PRESCALER_8
#define PR1_SETTING (SYSTEM_PERIPHERAL_CLOCK/1000/8)
#endif

#if (PR1_SETTING > 0xFFFF)
#undef CLOCK_DIVIDER
#undef PR1_SETTING
#define CLOCK_DIVIDER TIMER_PRESCALER_64
#define PR1_SETTING (SYSTEM_PERIPHERAL_CLOCK/1000/64)
#endif

#if (PR1_SETTING > 0xFFFF)
#undef CLOCK_DIVIDER
#undef PR1_SETTING
#define CLOCK_DIVIDER TIMER_PRESCALER_256
#define PR1_SETTING (SYSTEM_PERIPHERAL_CLOCK/1000/256)
#endif


/* Compiler checks and configuration *******************************/
#ifndef TIMER_MAX_1MS_CLIENTS
    #define TIMER_MAX_1MS_CLIENTS 3
#endif

/* Definitions *****************************************************/
#define STOP_TIMER_IN_IDLE_MODE     0x2000
#define TIMER_SOURCE_INTERNAL       0x0000
#define TIMER_SOURCE_EXTERNAL       0x0002
#define TIMER_ON                    0x8000
#define GATED_TIME_DISABLED         0x0000
#define TIMER_16BIT_MODE            0x0000

#define TIMER_PRESCALER_1           0x0000
#define TIMER_PRESCALER_8           0x0010
#define TIMER_PRESCALER_64          0x0020
#define TIMER_PRESCALER_256         0x0030
#define TIMER_INTERRUPT_PRIORITY    0x0001
#define TIMER_INTERRUPT_PRIORITY_7  0x0007

/* Type Definitions ************************************************/
typedef struct
{
    TICK_HANDLER handle;
    uint32_t rate;
    uint32_t count;
    int32_t loop_count;
} TICK_REQUEST;

/* Variables *******************************************************/
static TICK_REQUEST requests[TIMER_MAX_1MS_CLIENTS];
static bool configured = false;

/*********************************************************************
* Function: void TIMER_CancelTick(TICK_HANDLER handle)
*
* Overview: Cancels a tick request.
*
* PreCondition: None
*
* Input:  handle - the function that was handling the tick request
*
* Output: None
*
********************************************************************/
void TIMER_CancelTick(TICK_HANDLER handle)
{
    uint8_t i;

    for(i = 0; i < TIMER_MAX_1MS_CLIENTS; i++)
    {
        if(requests[i].handle == handle)
        {
            requests[i].handle = NULL;
        }
    }
}

/*********************************************************************
 * Function: bool TIMER_RequestTick(TICK_HANDLER handle, uint32_t rate)
 *
 * Overview: Requests to receive a periodic event.
 *
 * PreCondition: None
 *
 * Input:  handle - the function that will be called when the time event occurs
 *         rate - the number of ticks per event.
 *
 * Output: bool - true if successful, false if unsuccessful
 *
 ********************************************************************/
bool TIMER_RequestTick ( TICK_HANDLER handle , uint32_t rate, int32_t loop )
{
    uint8_t i;
	
    if(configured == false)
    {
        return false;
    }

    for(i = 0; i < TIMER_MAX_1MS_CLIENTS; i++)
    {
        if(requests[i].handle == NULL)
        {
            requests[i].handle = handle;
            requests[i].rate = rate;
            requests[i].count = 0;
            requests[i].loop_count = loop;

            return true;
        }
    }

    return false;
}

/*********************************************************************
 * Function: bool TIMER_SetConfiguration(TIMER_CONFIGURATIONS configuration)
 *
 * Overview: Initializes the timer.
 *
 * PreCondition: None
 *
 * Input:  None
 *
 * Output: bool - true if successful, false if unsuccessful
 *
 ********************************************************************/
bool TIMER_SetConfiguration ( TIMER_CONFIGURATIONS configuration )
{
    T1DisableInterrupt

    switch(configuration)
    {
        case TIMER_CONFIGURATION_1MS:
            memset(requests, 0, sizeof(requests));
            
//            IPC4bits.T1IP = TIMER_INTERRUPT_PRIORITY ;
            T1SetPriority
            T1ClearInterruptFlag

            TMR1 = 0 ;

            PR1 = PR1_SETTING ;
            T1CON = TIMER_ON |
                    TIMER_SOURCE_INTERNAL |
                    GATED_TIME_DISABLED |
                    TIMER_16BIT_MODE |
                    CLOCK_DIVIDER;

            T1EnableInterrupt

            configured = true;
            return true;

        case TIMER_CONFIGURATION_OFF:
            configured = false;
            return true;
    }

    return false;
}

/****************************************************************************
  Function:
    void __attribute__((__interrupt__, auto_psv)) _T3Interrupt(void)

  Description:
    Timer ISR, used to update application state. If no transfers are pending
    new input request is scheduled.
  Precondition:
    None

  Parameters:
    None

  Return Values:
    None

  Remarks:
    None
  ***************************************************************************/
void __ISR(_TIMER_1_VECTOR, IPL1SOFT) _T1Interrupt()
{
    uint8_t i;

    for(i = 0; i < TIMER_MAX_1MS_CLIENTS; i++)
    {
        if(requests[i].handle != NULL)
        {
            requests[i].count++;
            
            if(requests[i].count == requests[i].rate)
            {
                requests[i].handle();
                requests[i].count = 0;
                if (requests[i].loop_count > 0) {
                    requests[i].loop_count--;
                }
                if (requests[i].loop_count == 0) {
                    // end
                    requests[i].handle = NULL;
                }
            }
        }
    }

#ifdef DEBUG_ENABLE
    UART_Tasks();
#endif
    
//    IFS0CLR = _IFS0_T1IF_MASK; //Clear T1IF
    T1ClearInterruptFlag
}

//void __ISR(_CORE_TIMER_VECTOR, IPL1) _CTInterrupt()
//{
//    IFS0CLR = _IFS0_CTIF_MASK;
//}
