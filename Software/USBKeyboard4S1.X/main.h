/** @file   main.h
 *
 *  @author Sasaji
 *  @date   2018/02/10
 *
 * 	@brief  main 
 */

#ifndef MAIN_H_
#define	MAIN_H_

#include <stdint.h>

#define PORT_HPP_IS_HIGH    ((PORTB & _PORTB_RB4_MASK) != 0)

#define PORT_KRES1_IS_SET   ((PORTA & _PORTA_RA0_MASK) == 0)
#define PORT_KRES2_IS_SET   ((PORTA & _PORTA_RA1_MASK) == 0)

#define PORT_LED_IS_LOW     ((PORTA & _PORTA_RA4_MASK) == 0)

//#define COUNTER_IS_ODD      ((TMR2 & 1) != 0)
//#define COUNTER_IS_EVEN     ((TMR2 & 1) == 0)

#define TRIS_Y_N            TRISBCLR = _TRISB_TRISB7_MASK
#define LAT_Y_N             LATBbits.LATB7
#define LAT_Y_N_MASK        _LATB_LATB7_MASK
#define LAT_Y_N_SET         LATBCLR = _LATB_LATB7_MASK
#define LAT_Y_N_CLR         LATBSET = _LATB_LATB7_MASK

#define TRIS_BREAK_N        TRISBCLR = _TRISB_TRISB8_MASK
#define LAT_BREAK_N_MASK    _LATB_LATB8_MASK
#define LAT_BREAK_N_SET     LATBCLR = _LATB_LATB8_MASK
#define LAT_BREAK_N_CLR     LATBSET = _LATB_LATB8_MASK

#define LAT_KATA_LED        LATBbits.LATB9
#define LAT_KATA_LED_MASK   _LATB_LATB9_MASK
#define LAT_KATA_LED_SET    LATBCLR = _LATB_LATB9_MASK
#define LAT_KATA_LED_CLR    LATBSET = _LATB_LATB9_MASK

#define LAT_HIRA_LED        LATBbits.LATB13
#define LAT_HIRA_LED_MASK   _LATB_LATB13_MASK
#define LAT_HIRA_LED_SET    LATBCLR = _LATB_LATB13_MASK
#define LAT_HIRA_LED_CLR    LATBSET = _LATB_LATB13_MASK

#define LAT_CAPS_LED        LATBbits.LATB15
#define LAT_CAPS_LED_MASK   _LATB_LATB15_MASK
#define LAT_CAPS_LED_SET    LATBCLR = _LATB_LATB15_MASK
#define LAT_CAPS_LED_CLR    LATBSET = _LATB_LATB15_MASK

extern uint8_t key_onoff_flags[20];
//extern bool key_pressed;
extern uint8_t led_hira_inv;

#endif	/* MAIN_H_ */

