/******************************************************************************
Software License Agreement

The software supplied herewith by Microchip Technology Incorporated
(the "Company") for its PIC(R) Microcontroller is intended and
supplied to you, the Company's customer, for use solely and
exclusively on Microchip PICmicro Microcontroller products. The
software is owned by the Company and/or its supplier, and is
protected under applicable copyright laws. All rights are reserved.
Any use in violation of the foregoing restrictions may subject the
user to criminal sanctions under applicable laws, as well as to
civil liability for the breach of the terms and conditions of this
license.

THIS SOFTWARE IS PROVIDED IN AN "AS IS" CONDITION. NO WARRANTIES,
WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT NOT LIMITED
TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. THE COMPANY SHALL NOT,
IN ANY CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL OR
CONSEQUENTIAL DAMAGES, FOR ANY REASON WHATSOEVER.
*******************************************************************************/
/**
 * Modified by Sasaji at 2018/02/10
 */

#include "common.h"

#ifdef DEBUG_ENABLE

#include <xc.h>
#include "uart.h"
#include <stdint.h>
#include <stdio.h>

/* Private Definitions ***********************************************/
// UART1
#define SelectIOPin {\
    U1RXR = 0b0100; /* RB2 */ \
    RPB3R = 0b0001; /* U1TX */ \
}
#define TXLAT  LATBbits.LATB3
#define TXTRIS TRISBbits.TRISB3
#ifndef Simulator
#define SetPreScaler { \
    U1MODEbits.BRGH = 1; /* pre PBCLK / 4 */ \
/*    U1BRG = 1249; /* 9600 */ \
    U1BRG = 624; /* 19200 */ \
}
#else
#define SetPreScaler { \
    U1MODEbits.BRGH = 1; /* pre PBCLK / 4 */ \
    U1BRG = 15; \
}
#endif

/* Private Functions *************************************************/

/* Private variables ************************************************/
static uint8_t rpos;
static uint8_t wpos;
static char buffer[256];

/*********************************************************************
* Function: bool UART_Initialize(void);
*
* Overview: Initializes the UART.  
*
* PreCondition: none
*
* Input: None
*
* Output: true if initialized, false otherwise
*
********************************************************************/
bool UART_Initialize(void)
{
    U1MODE = 0; //Reset to default
    U1STA = 0; //Reset to default (bits that default to 1 are read-only)

    TXLAT = 1;    //Make TX pin high by default (idle)
    TXTRIS = 0;  //Make TX pin output

    SelectIOPin
    
    SetPreScaler

    rpos = 0;
    wpos = 0;

//    IPC8bits.U1IP = 5;  // Interrupt Level
//    IPC8bits.U1IS = 0;
//    U1STAbits.UTXISEL = 2;  //Interrput on if TX buffer is empty
    U1STAbits.UTXEN = 1; //Enable TX mode
    U1MODEbits.ON = 1; //Enable module
   
    return true;
}

/*********************************************************************
* Function: void UART_PutString(char* inputString, uint16_t length);
*
* Overview: Puts a string on the UART screen.  Unsupported characters will be
*           discarded.  May block or throw away characters is UART is not ready
*           or buffer space is not available.  Will terminate when either a
*           null terminator character (0x00) is reached or the length number
*           of characters is printed, which ever comes first.
*
* PreCondition: already initialized via UART_Initialize()
*
* Input: char* - string to print
*        uint16_t - length of string to print
*
* Output: None
*
********************************************************************/
void UART_PutStringL(char* inputString, uint16_t length)
{
    while(length--)
    {
        switch(*inputString)
        {
            case 0x00:
                return;
                
            default:
                UART_PutChar(*inputString++);
                break;
        }
    }
}

void UART_PutString(char* inputString)
{
    UART_PutStringL(inputString, 256);
}

void UART_PutStringDirect(char* inputString)
{
    uint16_t length = 256;
    while(length--)
    {
        switch(*inputString)
        {
            case 0x00:
                return;
                
            default:
                UART_PutCharDirect(*inputString++);
                break;
        }
    }
}

/*********************************************************************
* Function: void UART_PutChar(char);
*
* Overview: Puts a character on the UART screen.  Unsupported characters will be
*           discarded.  May block or throw away characters is UART is not ready
*           or buffer space is not available.
*
* PreCondition: already initialized via UART_Initialize()
*
* Input: char - character to print
*
* Output: None
*
********************************************************************/
void UART_PutChar(char inputCharacter)
{
    buffer[wpos] = inputCharacter;
    wpos++;
//    IEC1bits.U1TXIE = 1;
}

void UART_PutCharDirect(char inputCharacter)
{
//    IEC1bits.U1TXIE = 0;

    while(U1STAbits.UTXBF)
    {
        __asm__("nop");
    }
    
    U1TXREG = inputCharacter;
}

void UART_Interrupt_Tasks(void)
{
    IEC1bits.U1TXIE = 0;
    IFS1bits.U1TXIF = 0;
    volatile int wrote = 0;
    while (!U1STAbits.UTXBF && wpos != rpos) {
        U1TXREG = buffer[rpos];
        rpos++;
        wrote = 1;
    }
    if (wrote) {
        IEC1bits.U1TXIE = 1;
    }
}
/*********************************************************************
* Function: void UART_Tasks(void);
*
* Overview: 
*
* PreCondition: 
*
* Input: None
*
* Output: None
*
********************************************************************/
void UART_Tasks(void)
{
    while (!U1STAbits.UTXBF && wpos != rpos) {
        U1TXREG = buffer[rpos];
        rpos++;
        __asm__("nop");
    }
}

void UART_Flush(void)
{
    while (wpos != rpos) {
        UART_PutCharDirect(buffer[rpos]);
        rpos++;
    }
}

/*********************************************************************
* Function: void UART_ClearScreen(void);
*
* Overview: Clears the screen, if possible.
*
* PreCondition: already initialized via UART_Initialize()
*
* Input: None
*
* Output: None
*
********************************************************************/
//void UART_ClearScreen(void)
//{
//}

/*********************************************************************
* Function: void UART_CursorEnable(bool enable)
*
* Overview: Enables/disables the cursor
*
* PreCondition: None
*
* Input: bool - specifies if the cursor should be on or off
*
* Output: None
*
********************************************************************/
//void UART_CursorEnable(bool enable)
//{
//}

static char UART_GetHexa(char c)
{
    if (0 <= c && c <= 9) c += 0x30;
    else if (10 <= c && c <= 15) c += (0x41 - 10);
    else c = '.';
    return c;
}

void UART_PutHexU8n(uint8_t val)
{
    char c;

    c = UART_GetHexa((val & 0xf0) >> 4);
    UART_PutChar(c);
    c = UART_GetHexa(val & 0xf);
    UART_PutChar(c);
}

void UART_PutHexString(uint8_t *vals, int size)
{
    int i;

    if (size >= 16) size = 16;
    for(i=0; i<size; i++) {
        UART_PutHexU8n(vals[i]);
    }
    if (size > 0) UART_PutStringL("\r\n", 3);
}

void UART_PutHex16String(uint16_t *vals, int size)
{
    int i;

    if (size >= 16) size = 16;
    for(i=0; i<size; i++) {
        UART_PutChar(' ');
        UART_PutHexU8n(vals[i] >> 8);
        UART_PutHexU8n(vals[i] & 0xff);
    }
    if (size > 0) UART_PutStringL("\r\n", 3);
}

void UART_PutHexU8(uint8_t val)
{
    UART_PutHexU8n(val);
    UART_PutStringL("\r\n", 3);
}

void UART_PutHexU16(uint16_t val)
{
    UART_PutHexU8n(val >> 8);
    UART_PutHexU8n(val & 0xff);
    UART_PutStringL("\r\n", 3);
}

void UART_PutStringHexU8(char *str, uint8_t val)
{
    UART_PutStringL(str, 256);
    UART_PutHexU8(val);
}

void UART_PutStringHexU16(char *str, uint16_t val)
{
    UART_PutStringL(str, 256);
    UART_PutHexU16(val);
}

void UART_PutHexaDirect(uint8_t *vals, int size)
{
    int i;
    char c;

    if (size >= 16) size = 16;
    for(i=0; i<size; i++) {
        c = UART_GetHexa((vals[i] & 0xf0) >> 4);
        UART_PutCharDirect(c);
        c = UART_GetHexa(vals[i] & 0xf);
        UART_PutCharDirect(c);
    }
    if (size > 0) UART_PutStringDirect("\r\n");
}

#endif