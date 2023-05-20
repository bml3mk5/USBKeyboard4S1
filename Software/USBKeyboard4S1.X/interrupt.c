/** @file   interrupt.c
 *
 *  @author Sasaji
 *  @date   2018/02/10
 *
 * 	@brief  interrupt 
 */

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/attribs.h>

#include "common.h"
#include "main.h"
#include "usb.h"
#include "interrupt.h"
#include "app_host_hid_keyboard.h"
#ifdef DEBUG_ENABLE
#include "uart.h"
#endif

static uint8_t hpp_counter;
static uint8_t led_status;
static uint8_t led_status_prev;

void INTR_Init(void)
{
    INT4R = 0b0010; // RB4 HPP rise
    INT3R = 0b0001; // RB5 HPP fall
//    INT2R = 0b0010; // RA4 LED
    INTCONbits.INT4EP = 1;  // rise
    INTCONbits.INT3EP = 0;  // fall
//    INTCONbits.INT2EP = 0;  // fall
    
    IPC4bits.INT4IP = 6;
    IPC4bits.INT4IS = 3;
    IPC3bits.INT3IP = 6;
    IPC3bits.INT3IS = 2;
//    IPC2bits.INT2IP = 6;
//    IPC2bits.INT2IS = 0;

    hpp_counter = 0;
    led_status = 0;
    led_status_prev = 1;
    
    IFS0bits.INT4IF = 0;
    IFS0bits.INT3IF = 0;
//    IFS0bits.INT2IF = 0;
    IEC0bits.INT4IE = 1;
    IEC0bits.INT3IE = 1;
//    IEC0bits.INT2IE = 1;
}

/// USB 
void __ISR(_USB_1_VECTOR, IPL4SOFT) _USB1Interrupt()
{
    USB_HostInterruptHandler();
}

// HPP signal rise up
void __ISR(_EXTERNAL_4_VECTOR, IPL6SOFT) _INT4Interrupt()
{
    uint32_t pos;
    uint32_t bits;
    uint8_t *key;

    IEC0bits.INT4IE = 0;
    IFS0bits.INT4IF = 0;
    
    hpp_counter++;

    // if reset signal settle, clear counter
    if (PORT_KRES1_IS_SET) {
        hpp_counter &= 0xf0;
    }
    if (PORT_KRES2_IS_SET) {
        hpp_counter &= 0x0f;
    }
    
    //
    if ((hpp_counter & 1) == 0 && PORT_HPP_IS_HIGH) {
        // counter even
        pos = (hpp_counter >> 1);
        bits = (1 << (pos & 7));
        pos = ((pos >> 3) & 15);

        key = &key_onoff_flags[pos];
        
        if (*key & bits) {
            // key pressed
            LAT_Y_N_SET;
        } else {
            LAT_Y_N_CLR;
        }
    } else {
        // counter odd
        LAT_Y_N_CLR;
    }

    IEC0bits.INT4IE = 1;

#ifdef Simulator
    __asm__("nop");
#endif
}

// HPP signal fall down
void __ISR(_EXTERNAL_3_VECTOR, IPL6SOFT) _INT3Interrupt()
{
    IEC0bits.INT3IE = 0;
    IFS0bits.INT3IF = 0;

    LAT_Y_N_CLR;

    if (PORT_LED_IS_LOW) {
        led_status = ((hpp_counter & 0xe) ^ led_hira_inv);
        if (led_status != led_status_prev) {
            LAT_KATA_LED = (led_status & 2 ? 0 : 1);
            LAT_HIRA_LED = (led_status & 4 ? 0 : 1);
            LAT_CAPS_LED = (led_status & 8 ? 1 : 0);
            APP_HostHIDUpdateLED(led_status);
            led_status_prev = led_status; 
        }
    }

    IEC0bits.INT3IE = 1;

#ifdef Simulator
    __asm__("nop");
#endif
}

#if 0
// LED signal fall down
void __ISR(_EXTERNAL_2_VECTOR, IPL6SOFT) _INT2Interrupt()
{
    uint32_t bits_set;

    IEC0bits.INT2IE = 0;
    IFS0bits.INT2IF = 0;
    
    bits_set = 0;

    LAT_KATA_LED = (TMR2 & 2 ? 0 : 1);
    LAT_HIRA_LED = (TMR2 & 4 ? 0 : 1);
    LAT_CAPS_LED = (TMR2 & 8 ? 1 : 0);

    IEC0bits.INT2IE = 1;
    
#ifdef Simulator
    __asm__("nop");
#endif
}
#endif

#ifdef DEBUG_ENABLE
// UART interrupt
void __ISR(_UART_1_VECTOR, IPL5SOFT) _UART1Interrupt()
{
    UART_Interrupt_Tasks();
}
#endif
