/*******************************************************************************
Copyright 2015 Microchip Technology Inc. (www.microchip.com)

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
 * @note Modified by Sasaji at 2018/02/10
 * @note These function was moved from usb_host.c by Sasaji
 * @attention Bulk and Isochronous transfer are not maintained.
 */

#ifndef __USBHOST_TRANS_H__
#define __USBHOST_TRANS_H__

// The USB specification states that transactions should be tried three times
// if there is a bus error.  We will allow that number to be configurable. The
// maximum value is 31.
#define USB_TRANSACTION_RETRY_ATTEMPTS  20

// *****************************************************************************
/* USB Bus Information

This structure is used to hold information about the USB bus status.
*/
typedef struct _USB_BUS_INFO
{
    volatile union
    {
        struct
        {
            uint16_t       bfControlTransfersDone      : 1;    // All control transfers in the current frame are complete.
            uint16_t       bfInterruptTransfersDone    : 1;    // All interrupt transfers in the current frame are complete.
            uint16_t       bfIsochronousTransfersDone  : 1;    // All isochronous transfers in the current frame are complete.
            uint16_t       bfBulkTransfersDone         : 1;    // All bulk transfers in the current frame are complete.
            uint16_t       bfControlRequestProcessing     : 1;    // A request transfers are processing (usually process a few frames).
            uint16_t       bfInterruptRequestProcessing   : 1;    // A request transfers are processing.
            uint16_t       bfIsochronousRequestProcessing : 1;    // A request transfers are processing.
            uint16_t       bfBulkRequestProcessing        : 1;    // A request transfers are processing.
            uint16_t       bfTokenAlreadyWritten       : 1;    // A token has already been written to the USB module
            uint16_t       bfPingPongIn                : 1;    // Ping-pong status of IN buffers (default = 0).
            uint16_t       bfPingPongOut               : 1;    // Ping-pong status of OUT buffers (default = 0).
        };
        uint16_t           val;                                //
    }                      flags;                              //
//  volatile uint32_t      dBytesSentInFrame;                  // The number of bytes sent during the current frame. Isochronous use only.
    volatile uint8_t       lastBulkTransaction;                // The last bulk transaction sent.
    volatile uint8_t       countBulkTransactions;              // The number of active bulk transactions.
} USB_BUS_INFO;

// *****************************************************************************

void USBTrans_Init( void );
void USBTrans_ClearControlEndpointStatus( void );
void USBTrans_ClearInterruptEndpointStatus( void );

void USB_InitControlReadWrite( bool is_write, USB_DEVICE_INFO *deviceInfo, USB_ENDPOINT_INFO *pEndpoint
    , SETUP_PKT *pControlData, uint16_t controlSize, uint8_t *pData, uint16_t size );
void USB_InitReadWrite( bool is_write, USB_DEVICE_INFO *deviceInfo, USB_ENDPOINT_INFO *pEndpoint
    , uint8_t *pData, uint16_t size );

void USB_HostInterrupt_Transfer( void );
void USB_HostInterrupt_SOF( void );
void USB_HostInterrupt_Error( void );

void _USB_ClearBDT( void );
void _USB_SetBDT( USB_ENDPOINT_INFO *pEndpoint, uint8_t  direction );

void USB_FindNextToken( void );



#endif /* __USBHOST_TRANS_H__ */


