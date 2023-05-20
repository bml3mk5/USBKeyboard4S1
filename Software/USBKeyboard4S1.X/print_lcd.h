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

#ifndef PRINT_LCD_H
#define PRINT_LCD_H

#include <stdbool.h>
//#include <lcd.h>

#if 1
#define USE_OUTPUT_DEVICE_IS 1  // UART
#include "uart.h"
#else
#define USE_OUTPUT_DEVICE_IS 0  // LCD
#endif

/* Type Definitions *************************************************/
//typedef enum
//{
//    PRINT_CONFIGURATION_LCD
//} PRINT_CONFIGURATION;

/*********************************************************************
* Function: bool PRINT_SetConfiguration(PRINT_CONFIGURATION led);
*
* Overview: Configures the print configuration
*
* PreCondition: none
*
* Input: configuration - the print configuration to use.  Some boards
*         may have more than one print configuration enabled
*
* Output: TRUE if successful, FALSE if LED not configured or doesn?t 
*         exist for this board.
*
********************************************************************/
//bool PRINT_SetConfiguration();
#if USE_OUTPUT_DEVICE_IS == 0
#define PRINT_SetConfiguration() LCD_Initialize()
#else
#define PRINT_SetConfiguration() UART_Initialize()
#endif
/*********************************************************************
* Function: void PRINT_String(char* string, uint16_t length)
*
* Overview: Prints a string until a null terminator is reached or the
*           specified string length is printed.
*
* PreCondition: none
*
* Input: char* string - the string to print.
*        uint16_t length - the length of the string.
*
* Output: None
*
********************************************************************/
//void PRINT_String(char* string, uint16_t length);
#if USE_OUTPUT_DEVICE_IS == 0
#define PRINT_String(string, length) LCD_PutString(string, length)
#else
#define PRINT_String(string, length) UART_PutString(string)
#endif

/*********************************************************************
* Function: void PRINT_Char(char charToPrint)
*
* Overview: Prints a character
*
* PreCondition: none
*
* Input: char charToPrint - the character to print
*
* Output: None
*
********************************************************************/
//void PRINT_Char(char charToPrint);
#if USE_OUTPUT_DEVICE_IS == 0
#define PRINT_Char(charToPrint) LCD_PutChar(charToPrint)
#else
#define PRINT_Char(charToPrint) UART_PutChar(charToPrint)
#endif

/*********************************************************************
* Function: void PRINT_ClearStreen()
*
* Overview: Clears the screen, if possible
*
* PreCondition: none
*
* Input: None
*
* Output: None
*
********************************************************************/
//void PRINT_ClearScreen(void);
#if USE_OUTPUT_DEVICE_IS == 0
#define PRINT_ClearScreen() LCD_ClearScreen()
#else
#define PRINT_ClearScreen()
#endif

/*********************************************************************
* Function: void PRINT_CursorEnable(bool enable)
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
//void PRINT_CursorEnable(bool enable);
#if USE_OUTPUT_DEVICE_IS == 0
#define PRINT_CursorEnable(enable) LCD_CursorEnable(enable)
#else
#define PRINT_CursorEnable(enable)
#endif

#endif //PRINT_LCD_H
