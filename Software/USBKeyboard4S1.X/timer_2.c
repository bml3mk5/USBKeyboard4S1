/** @file   timer_2.c
 *
 *  @author Sasaji
 *  @date   2018/02/10
 *
 * 	@brief  timer_2 
 */

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>
//#include <sys/attribs.h>

#include "timer_2.h"

#if 0
void Timer2_Init(void)
{
    // select RB4 as clock pin for timer2
    T2CKR = 0b0010; // RPB4

    // 256 (8bit) counter
    PR2 = 0x100;
    TMR2 = 0;

    // DIVIDE = (0x0 << _T2CON_TCKPS0_POSITION) |
    T2CON = _T2CON_ON_MASK |
            _T2CON_TCS_MASK;

//    IPC2bits.T2IP = 2;
//    IEC0bits.T2IE = 1;

}

void Timer2_Clear(uint8_t val)
{
//    IEC0bits.T2IE = 0;
    TMR2CLR = val;
//    IFS0bits.T2IF = 0;
//    IEC0bits.T2IE = 1;
}
#endif