/** @file usb_host_hub.h
 *
 *  @author Sasaji
 *  @date   2023/04/07
 *
 * 	@brief  Treat HUB class
 */

#ifndef USB_HOST_HUB_H
#define	USB_HOST_HUB_H

#include <xc.h>
#include "usb_common.h"

// *****************************************************************************

#define DEVICE_CLASS_HUB             0x09   // HUB Interface Class Code 

#define USB_GET_HUB_DESCRIPTOR       0x2900

// *****************************************************************************

void USBHostHUB( void );

void USBHostHUBTasks( void );
bool USBHostHUBInitialize( uint8_t address, uint32_t flags, uint8_t clientDriverID );
bool USBHostHUBEventHandler( uint8_t address, USB_EVENT event, void *data, uint32_t size );

#endif	/* USB_HOST_HUB_H */

