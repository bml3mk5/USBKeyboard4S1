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

#ifndef UART_H
#define	UART_H

#include <stdint.h>
#include <stdbool.h>

/*********************************************************************
* Function: bool UART_Initialize(void);
*
* Overview: Initializes the LCD screen.  Can take several hundred
*           milliseconds.
*
* PreCondition: none
*
* Input: None
*
* Output: true if successful, false otherwise
*
********************************************************************/
bool UART_Initialize(void);

/*********************************************************************
* Function: void UART_PutString(char* inputString, uint16_t length);
*
* Overview: Puts a string on the LCD screen.  Unsupported characters will be
*           discarded.  May block or throw away characters is LCD is not ready
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
void UART_PutString(char* inputString);

void UART_PutStringL(char* inputString, uint16_t length);

void UART_PutStringDirect(char* inputString);

/*********************************************************************
* Function: void UART_PutChar(char);
*
* Overview: Puts a character on the LCD screen.  Unsupported characters will be
*           discarded.  May block or throw away characters is LCD is not ready
*           or buffer space is not available.
*
* PreCondition: already initialized via UART_Initialize()
*
* Input: char - character to print
*
* Output: None
*
********************************************************************/
void UART_PutChar(char);

void UART_PutCharDirect(char);

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
void UART_Tasks(void);

void UART_Flush(void);

void UART_Interrupt_Tasks(void);

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
//void UART_ClearScreen(void);

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
//void UART_CursorEnable(bool enable);

void UART_PutHexString(uint8_t *vals, int size);
void UART_PutHex16String(uint16_t *vals, int size);
void UART_PutHexU8(uint8_t val);
void UART_PutStringHexU8(char *str, uint8_t val);
void UART_PutStringHexU16(char *str, uint16_t val);
void UART_PutHexaDirect(uint8_t *vals, int size);

#endif /* UART_H */

