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

#include "../common.h"
#include "usb_common.h"
#include "usb_ch9.h"
#include "usb_config.h"
#include "usb_host_local.h"
#include "usb_hal_local.h"
#include "usb_host_trans.h"
#include "usb_struct_config_list.h"
#include "usb_struct_interface.h"
#include <string.h>

#include "../uart.h"

#define USB_HUB_SUPPORT_INCLUDED 1

#if defined( USB_ENABLE_TRANSFER_EVENT )
    #include "usb_struct_queue.h"
#endif

// *****************************************************************************
// Low Level Functionality Configurations.
// *****************************************************************************

// If we allow multiple control transactions during a frame and a NAK is
// generated, we don't get TRNIF.  So we will allow only one control transaction
// per frame.
#define ONE_CONTROL_TRANSACTION_PER_FRAME

//******************************************************************************
//******************************************************************************
// Section: Host Global Variables
//******************************************************************************
//******************************************************************************

// When using the PIC32, ping pong mode must be set to FULL.
#if defined (__PIC32__)
    #if (USB_PING_PONG_MODE != USB_PING_PONG__FULL_PING_PONG)
        #undef USB_PING_PONG_MODE
        #define USB_PING_PONG_MODE USB_PING_PONG__FULL_PING_PONG
    #endif
#endif

#if (USB_PING_PONG_MODE == USB_PING_PONG__NO_PING_PONG) || (USB_PING_PONG_MODE == USB_PING_PONG__ALL_BUT_EP0)
    #if !defined(USB_SUPPORT_OTG) && !defined(USB_SUPPORT_DEVICE)
    static BDT_ENTRY __attribute__ ((aligned(512)))    BDT[2];
    #endif
    #define BDT_IN                                  (&BDT[0])           // EP0 IN Buffer Descriptor
    #define BDT_OUT                                 (&BDT[1])           // EP0 OUT Buffer Descriptor
#elif (USB_PING_PONG_MODE == USB_PING_PONG__EP0_OUT_ONLY)
    #if !defined(USB_SUPPORT_OTG) && !defined(USB_SUPPORT_DEVICE)
    static BDT_ENTRY __attribute__ ((aligned(512)))    BDT[3];
    #endif
    #define BDT_IN                                  (&BDT[0])           // EP0 IN Buffer Descriptor
    #define BDT_OUT                                 (&BDT[1])           // EP0 OUT Even Buffer Descriptor
    #define BDT_OUT_ODD                             (&BDT[2])           // EP0 OUT Odd Buffer Descriptor
#elif (USB_PING_PONG_MODE == USB_PING_PONG__FULL_PING_PONG)
    #if !defined(USB_SUPPORT_OTG) && !defined(USB_SUPPORT_DEVICE)
    static BDT_ENTRY __attribute__ ((aligned(512)))    BDT[4];
    #endif
    #define BDT_IN                                  (&BDT[0])           // EP0 IN Even Buffer Descriptor
    #define BDT_IN_ODD                              (&BDT[1])           // EP0 IN Odd Buffer Descriptor
    #define BDT_OUT                                 (&BDT[2])           // EP0 OUT Even Buffer Descriptor
    #define BDT_OUT_ODD                             (&BDT[3])           // EP0 OUT Odd Buffer Descriptor
#endif

#if defined(USB_SUPPORT_OTG) || defined(USB_SUPPORT_DEVICE)
    extern BDT_ENTRY BDT[] __attribute__ ((aligned (512)));
#endif

USB_BUS_INFO                usbBusInfo;                         // Information about the USB bus.

static USB_ENDPOINT_INFO   *pTargetControlEndpointInfo = NULL;         // Pointer to the endpoint currently performing a transfer.
static USB_DEVICE_INFO     *pTargetControlDeviceInfo = NULL;           // Pointer to the endpoint currently performing a transfer.
static USB_ENDPOINT_INFO   *pTargetInterruptEndpointInfo = NULL;         // Pointer to the endpoint currently performing a transfer.
static USB_DEVICE_INFO     *pTargetInterruptDeviceInfo = NULL;           // Pointer to the endpoint currently performing a transfer.
static uint8_t currentTransferType = 0;

//int                                  delaySOFCount;


/****************************************************************************/
void _USB_SendToken( USB_DEVICE_INFO *pDeviceInfo, USB_ENDPOINT_INFO *pEndpointInfo, uint8_t tokenType );

#define _USB_SetDATA01(ep, x)             { ep->status.bfNextDATA01 = x; }
#define _USB_SetNextTransferState(ep)     { ep->transferState++; }

/****************************************************************************/
/****************************************************************************/
void USBTrans_Init( void )
{
    memset(&usbBusInfo, 0, sizeof(USB_BUS_INFO));
}

/****************************************************************************/
void USBTrans_ClearControlEndpointStatus( void )
{
    if (pTargetControlEndpointInfo) {
        pTargetControlEndpointInfo->status.bfError    = 0;
        pTargetControlEndpointInfo->status.bfStalled  = 0;
    }
}

/****************************************************************************/
void USBTrans_ClearInterruptEndpointStatus( void )
{
    if (pTargetInterruptEndpointInfo) {
        pTargetInterruptEndpointInfo->status.bfError    = 0;
        pTargetInterruptEndpointInfo->status.bfStalled  = 0;
    }
}

/****************************************************************************/
static __inline__ void _USB_FindNextToken_Control_SendToken( uint8_t token, uint8_t dts )
{
    if (dts < 0x80) {
        _USB_SetDATA01( pTargetControlEndpointInfo, dts );
    }
#ifdef DEBUG_ENABLE
    DEBUG_PutStringHexU8( "C", pTargetControlDeviceInfo->deviceAddress ); 
#endif
    _USB_SetBDT( pTargetControlEndpointInfo, token );
    _USB_SendToken( pTargetControlDeviceInfo, pTargetControlEndpointInfo, token );
#ifdef ONE_CONTROL_TRANSACTION_PER_FRAME
     usbBusInfo.flags.bfControlTransfersDone = 1; // Only one control transfer per frame.
#endif
}

/****************************************************************************/
static __inline__ void _USB_FindNextToken_Control_Complete( uint8_t transferState )
{
    pTargetControlEndpointInfo->transferState               = TSTATE_IDLE;
    pTargetControlEndpointInfo->status.bfTransferComplete   = 1;
    usbBusInfo.flags.bfControlRequestProcessing      = 0;
#if defined( USB_ENABLE_TRANSFER_EVENT )
    if (transferState != TSUBSTATE_ERROR) {
#ifdef DEBUG_ENABLE
        DEBUG_PutString("DONE\r\n");
#endif
        StructEventQueueAdd_Success(pTargetControlDeviceInfo, pTargetControlEndpointInfo);
    } else {
        StructEventQueueAdd_Error(pTargetControlDeviceInfo, pTargetControlEndpointInfo);
    }
#endif
}

/****************************************************************************/
static __inline__ bool _USB_FindNextToken_Control_NoData( void )
{
    uint8_t transferState = pTargetControlEndpointInfo->transferState & TSUBSTATE_MASK;
    switch (transferState)
    {
        case TSUBSTATE_CONTROL_NO_DATA_SETUP:
#ifdef DEBUG_ENABLE
            DEBUG_PutString(">SETUP\r\n");
#endif
            _USB_FindNextToken_Control_SendToken( USB_TOKEN_SETUP, DTS_DATA0 );
            return false;
            break;

        case TSUBSTATE_CONTROL_NO_DATA_ACK:
#ifdef DEBUG_ENABLE
            DEBUG_PutString("<ACK\r\n");
#endif
            pTargetControlEndpointInfo->dataCountMax = pTargetControlEndpointInfo->dataCount;
            _USB_FindNextToken_Control_SendToken( USB_TOKEN_IN, DTS_DATA1 );
            return false;
            break;

        case TSUBSTATE_CONTROL_NO_DATA_COMPLETE:
            _USB_FindNextToken_Control_Complete( transferState );
            break;

        default:
            _USB_FindNextToken_Control_Complete( TSUBSTATE_ERROR );
            break;
   }
    
    return true;
}

/****************************************************************************/
static __inline__ bool _USB_FindNextToken_Control_Read( void )
{
    uint8_t transferState = pTargetControlEndpointInfo->transferState & TSUBSTATE_MASK;
    switch (transferState)
    {
        case TSUBSTATE_CONTROL_READ_SETUP:
#ifdef DEBUG_ENABLE
            DEBUG_PutString(">SETUP\r\n");
#endif
            _USB_FindNextToken_Control_SendToken( USB_TOKEN_SETUP, DTS_DATA0 );
            return false;
            break;

        case TSUBSTATE_CONTROL_READ_DATA:
#ifdef DEBUG_ENABLE
            DEBUG_PutString("<DATA\r\n");
#endif
            _USB_FindNextToken_Control_SendToken( USB_TOKEN_IN, DTS_PREDATA );
            return false;
            break;

        case TSUBSTATE_CONTROL_READ_ACK:
#ifdef DEBUG_ENABLE
            DEBUG_PutString(">ACK\r\n");
#endif
            pTargetControlEndpointInfo->dataCountMax = pTargetControlEndpointInfo->dataCount;
            _USB_FindNextToken_Control_SendToken( USB_TOKEN_OUT, DTS_DATA1 );
            return false;
            break;

        case TSUBSTATE_CONTROL_READ_COMPLETE:
#ifdef DEBUG_ENABLE
            DEBUG_PutString("DONE\r\n");
#endif
            _USB_FindNextToken_Control_Complete( transferState );
            break;

        default:
            _USB_FindNextToken_Control_Complete( TSUBSTATE_ERROR );
            break;
    }

    return true;
}

/****************************************************************************
  Returns:
    bool - transaction is done if true
****************************************************************************/
static __inline__ bool _USB_FindNextToken_Control_Write( void )
{
    uint8_t transferState = pTargetControlEndpointInfo->transferState & TSUBSTATE_MASK;
    switch (transferState)
    {
        case TSUBSTATE_CONTROL_WRITE_SETUP:
#ifdef DEBUG_ENABLE
            DEBUG_PutString(">SETUP\r\n");
#endif
            _USB_FindNextToken_Control_SendToken( USB_TOKEN_SETUP, DTS_DATA0 );
            return false;
            break;

        case TSUBSTATE_CONTROL_WRITE_DATA:
#ifdef DEBUG_ENABLE
            DEBUG_PutString(">DATA\r\n");
#endif
            _USB_FindNextToken_Control_SendToken( USB_TOKEN_OUT, DTS_PREDATA );
            return false;
            break;

        case TSUBSTATE_CONTROL_WRITE_ACK:
#ifdef DEBUG_ENABLE
            DEBUG_PutString("<ACK\r\n");
#endif
            pTargetControlEndpointInfo->dataCountMax = pTargetControlEndpointInfo->dataCount;
            _USB_FindNextToken_Control_SendToken( USB_TOKEN_IN, DTS_DATA1 );
            return false;
            break;

        case TSUBSTATE_CONTROL_WRITE_COMPLETE:
#ifdef DEBUG_ENABLE
            DEBUG_PutString("DONE\r\n");
#endif
            _USB_FindNextToken_Control_Complete( transferState );
            break;

        default:
            _USB_FindNextToken_Control_Complete( TSUBSTATE_ERROR );
            break;
    }
    
    return true;
}

/****************************************************************************/
static __inline__ bool _USB_FindNextToken_Control( void )
{
    USB_TRANSFER_DATA *item = NULL;

    if (usbBusInfo.flags.bfControlTransfersDone)
    {
        return true;
    }
//    if (usbBusInfo.flags.bfInterruptRequestProcessing | usbBusInfo.flags.bfBulkRequestProcessing | usbBusInfo.flags.bfIsochronousRequestProcessing)
//    {
//        return true;
//    }

    if (!usbBusInfo.flags.bfControlRequestProcessing) {
        item = StructTransferControlQueueGet();
        if (!item) {
            // no request found
            return true;
        }
        pTargetControlEndpointInfo = item->endpointInfo;
        pTargetControlDeviceInfo = item->deviceInfo;
        usbBusInfo.flags.bfControlRequestProcessing = 1;
#ifdef DEBUG_ENABLE
        DEBUG_PutString("New Ctrl Token\r\n");
#endif
    }
    // Look for any control transfers.
//  if (_USB_FindServiceEndpoint_ForControl())
//  {
        switch (pTargetControlEndpointInfo->transferState & TSTATE_MASK)
        {
            case TSTATE_CONTROL_NO_DATA:
                if (!_USB_FindNextToken_Control_NoData()) {
                    return false;
                }
                break;

            case TSTATE_CONTROL_READ:
                if (!_USB_FindNextToken_Control_Read()) {
                    return false;
                }
                break;

            case TSTATE_CONTROL_WRITE:
                if (!_USB_FindNextToken_Control_Write()) {
                    return false;
                }
                break;

            default:
                _USB_FindNextToken_Control_Complete( TSUBSTATE_ERROR );
                break;
        }
//  }

    // If we've gone through all the endpoints, we do not have any more control transfers.
    usbBusInfo.flags.bfControlTransfersDone = 1;
    
    return true;
}

#ifdef USB_SUPPORT_ISOCHRONOUS_TRANSFERS
/****************************************************************************/
static __inline__ bool _USB_FindNextToken_Isochronous_Read(bool *illegalState)
{
    switch (pTargetEndpointInfo->transferState & TSUBSTATE_MASK)
    {
        case TSUBSTATE_ISOCHRONOUS_READ_DATA:
            // Don't overwrite data the user has not yet processed.  We will skip this interval.    
            if (((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->buffers[((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->currentBufferUSB].bfDataLengthValid)
            {
                // We have buffer overflow.
            }
            else
            {
                // Initialize the data buffer.
                ((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->buffers[((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->currentBufferUSB].bfDataLengthValid = 0;
                pTargetEndpointInfo->dataCount = 0;

                _USB_SetDATA01( DTS_DATA0 );    // Always DATA0 for isochronous
                _USB_SetBDT( USB_TOKEN_IN );
                _USB_SendToken( pTargetEndpointInfo->bEndpointAddress, USB_TOKEN_IN );
                return false;
            }    
            break;

        case TSUBSTATE_ISOCHRONOUS_READ_COMPLETE:
            // Isochronous transfers are continuous until the user stops them.
            // Send an event that there is new data, and reset for the next
            // interval.
            pTargetEndpointInfo->transferState     = TSTATE_ISOCHRONOUS_READ | TSUBSTATE_ISOCHRONOUS_READ_DATA;
            pTargetEndpointInfo->wIntervalCount    = pTargetEndpointInfo->wInterval;

            // Update the valid data length for this buffer.
            ((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->buffers[((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->currentBufferUSB].dataLength = pTargetEndpointInfo->dataCount;
            ((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->buffers[((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->currentBufferUSB].bfDataLengthValid = 1;
            #if defined( USB_ENABLE_ISOC_TRANSFER_EVENT )
                if (StructQueueIsNotFull(&usbEventQueue, USB_EVENT_QUEUE_DEPTH))
                {
                    USB_EVENT_DATA *data;

                    data = StructQueueAdd(&usbEventQueue, USB_EVENT_QUEUE_DEPTH);
                    data->event = EVENT_TRANSFER;
                    data->TransferData.dataCount        = pTargetEndpointInfo->dataCount;
                    data->TransferData.pUserData        = ((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->buffers[((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->currentBufferUSB].pBuffer;
                    data->TransferData.bErrorCode       = USB_SUCCESS;
                    data->TransferData.bEndpointAddress = pTargetEndpointInfo->bEndpointAddress;
                    data->TransferData.bmAttributes.val = pTargetEndpointInfo->bmAttributes.val;
                    data->TransferData.clientDriver     = pTargetEndpointInfo->clientDriver;
                }
                else
                {
                    pTargetEndpointInfo->bmAttributes.val = USB_EVENT_QUEUE_FULL;
                }
            #endif

            // If the user wants an event from the interrupt handler to handle the data as quickly as
            // possible, send up the event.  Then mark the packet as used.
            #ifdef USB_HOST_APP_DATA_EVENT_HANDLER
            USBClientDriver_DataEventHandler(pTargetEndpointInfo->clientDriver, usbDeviceInfo.deviceAddress, EVENT_DATA_ISOC_READ, ((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->buffers[((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->currentBufferUSB].pBuffer, pTargetEndpointInfo->dataCount);
            ((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->buffers[((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->currentBufferUSB].bfDataLengthValid = 0;
            #endif

            // Move to the next data buffer.
            ((ISOCHRONOUS_DATA *)pTargetEndpointInfo->pUserData)->currentBufferUSB++;
            if (((ISOCHRONOUS_DATA *)pTargetEndpointInfo->pUserData)->currentBufferUSB >= ((ISOCHRONOUS_DATA *)pTargetEndpointInfo->pUserData)->totalBuffers)
            {
                ((ISOCHRONOUS_DATA *)pTargetEndpointInfo->pUserData)->currentBufferUSB = 0;
            }
            break;

        case TSUBSTATE_ERROR:
            // Isochronous transfers are continuous until the user stops them.
            // Send an event that there is an error, and reset for the next
            // interval.
            pTargetEndpointInfo->transferState     = TSTATE_ISOCHRONOUS_READ | TSUBSTATE_ISOCHRONOUS_READ_DATA;
            pTargetEndpointInfo->wIntervalCount    = pTargetEndpointInfo->wInterval;
            #if defined( USB_ENABLE_TRANSFER_EVENT )
                if (StructQueueIsNotFull(&usbEventQueue, USB_EVENT_QUEUE_DEPTH))
                {
                    USB_EVENT_DATA *data;

                    data = StructQueueAdd(&usbEventQueue, USB_EVENT_QUEUE_DEPTH);
                    data->event = EVENT_BUS_ERROR;
                    data->TransferData.dataCount        = 0;
                    data->TransferData.pUserData        = NULL;
                    data->TransferData.bEndpointAddress = pTargetEndpointInfo->bEndpointAddress;
                    data->TransferData.bErrorCode       = pTargetEndpointInfo->bErrorCode;
                    data->TransferData.bmAttributes.val = pTargetEndpointInfo->bmAttributes.val;
                }
                else
                {
                    pTargetEndpointInfo->bmAttributes.val = USB_EVENT_QUEUE_FULL;
                }
            #endif
            break;

        default:
            *illegalState = true;
            break;
    }
    return true;
}

/****************************************************************************/
static __inline__ bool _USB_FindNextToken_Isochronous_Write(bool *illegalState)
{
    switch (pTargetEndpointInfo->transferState & TSUBSTATE_MASK)
    {
        case TSUBSTATE_ISOCHRONOUS_WRITE_DATA:
            if (!((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->buffers[((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->currentBufferUSB].bfDataLengthValid)
            {
                // We have buffer underrun.
            }
            else
            {
                pTargetEndpointInfo->dataCount = ((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->buffers[((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->currentBufferUSB].dataLength;

                _USB_SetDATA01( DTS_DATA0 );    // Always DATA0 for isochronous
                _USB_SetBDT( USB_TOKEN_OUT );
                _USB_SendToken( pTargetEndpointInfo->bEndpointAddress, USB_TOKEN_OUT );
                return false;
            }    
            break;

        case TSUBSTATE_ISOCHRONOUS_WRITE_COMPLETE:
            // Isochronous transfers are continuous until the user stops them.
            // Send an event that data has been sent, and reset for the next
            // interval.
            pTargetEndpointInfo->wIntervalCount    = pTargetEndpointInfo->wInterval;
            pTargetEndpointInfo->transferState     = TSTATE_ISOCHRONOUS_WRITE | TSUBSTATE_ISOCHRONOUS_WRITE_DATA;

            // Update the valid data length for this buffer.
            ((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->buffers[((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->currentBufferUSB].bfDataLengthValid = 0;
            #if defined( USB_ENABLE_ISOC_TRANSFER_EVENT )
                if (StructQueueIsNotFull(&usbEventQueue, USB_EVENT_QUEUE_DEPTH))
                {
                    USB_EVENT_DATA *data;

                    data = StructQueueAdd(&usbEventQueue, USB_EVENT_QUEUE_DEPTH);
                    data->event = EVENT_TRANSFER;
                    data->TransferData.dataCount        = pTargetEndpointInfo->dataCount;
                    data->TransferData.pUserData        = ((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->buffers[((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->currentBufferUSB].pBuffer;
                    data->TransferData.bErrorCode       = USB_SUCCESS;
                    data->TransferData.bEndpointAddress = pTargetEndpointInfo->bEndpointAddress;
                    data->TransferData.bmAttributes.val = pTargetEndpointInfo->bmAttributes.val;
                    data->TransferData.clientDriver     = pTargetEndpointInfo->clientDriver;
                }
                else
                {
                    pTargetEndpointInfo->bmAttributes.val = USB_EVENT_QUEUE_FULL;
                }
            #endif

            // If the user wants an event from the interrupt handler to handle the data as quickly as
            // possible, send up the event.
            #ifdef USB_HOST_APP_DATA_EVENT_HANDLER
            USBClientDriver_DataEventHandler(pTargetEndpointInfo->clientDriver,  usbDeviceInfo.deviceAddress, EVENT_DATA_ISOC_WRITE, ((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->buffers[((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->currentBufferUSB].pBuffer, pTargetEndpointInfo->dataCount);
            #endif

            // Move to the next data buffer.
            ((ISOCHRONOUS_DATA *)pTargetEndpointInfo->pUserData)->currentBufferUSB++;
            if (((ISOCHRONOUS_DATA *)pTargetEndpointInfo->pUserData)->currentBufferUSB >= ((ISOCHRONOUS_DATA *)pTargetEndpointInfo->pUserData)->totalBuffers)
            {
                ((ISOCHRONOUS_DATA *)pTargetEndpointInfo->pUserData)->currentBufferUSB = 0;
            }
            ((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->buffers[((ISOCHRONOUS_DATA *)(pTargetEndpointInfo->pUserData))->currentBufferUSB].bfDataLengthValid = 1;                                
            break;

        case TSUBSTATE_ERROR:
            // Isochronous transfers are continuous until the user stops them.
            // Send an event that there is an error, and reset for the next
            // interval.
            pTargetEndpointInfo->transferState     = TSTATE_ISOCHRONOUS_WRITE | TSUBSTATE_ISOCHRONOUS_WRITE_DATA;
            pTargetEndpointInfo->wIntervalCount    = pTargetEndpointInfo->wInterval;

            #if defined( USB_ENABLE_TRANSFER_EVENT )
                if (StructQueueIsNotFull(&usbEventQueue, USB_EVENT_QUEUE_DEPTH))
                {
                    USB_EVENT_DATA *data;

                    data = StructQueueAdd(&usbEventQueue, USB_EVENT_QUEUE_DEPTH);
                    data->event = EVENT_BUS_ERROR;
                    data->TransferData.dataCount        = 0;
                    data->TransferData.pUserData        = NULL;
                    data->TransferData.bErrorCode       = pTargetEndpointInfo->bErrorCode;
                    data->TransferData.bEndpointAddress = pTargetEndpointInfo->bEndpointAddress;
                    data->TransferData.bmAttributes.val = pTargetEndpointInfo->bmAttributes.val;
                    data->TransferData.clientDriver     = pTargetEndpointInfo->clientDriver;
                }
                else
                {
                    pTargetEndpointInfo->bmAttributes.val = USB_EVENT_QUEUE_FULL;
                }
            #endif
            break;

        default:
            *illegalState = true;
            break;
    }
    return true;
}

/****************************************************************************/
static __inline__ bool _USB_FindNextToken_Isochronous(bool *illegalState)
{
    if (!usbBusInfo.flags.bfIsochronousTransfersDone)
    {
        // Look for any isochronous operations.
        while (_USB_FindServiceEndpoint( USB_TRANSFER_TYPE_ISOCHRONOUS ))
        {
            switch (pTargetEndpointInfo->transferState & TSTATE_MASK)
            {
                case TSTATE_ISOCHRONOUS_READ:
                    if (!_USB_FindNextToken_Isochronous_Read(illegalState)) {
                        return false;
                    }
                    break;

                case TSTATE_ISOCHRONOUS_WRITE:
                    if (!_USB_FindNextToken_Isochronous_Write(illegalState)) {
                        return false;
                    }
                    break;

                default:
                    *illegalState = true;
                    break;
            }

            if (*illegalState)
            {
                // We should never use this, but in case we do, put the endpoint
                // in a recoverable state.
                pTargetEndpointInfo->transferState             = TSTATE_IDLE;
                pTargetEndpointInfo->wIntervalCount            = pTargetEndpointInfo->wInterval;
                pTargetEndpointInfo->status.bfTransferComplete = 1;
            }
        }

        // If we've gone through all the endpoints, we do not have any more isochronous transfers.
        usbBusInfo.flags.bfIsochronousTransfersDone = 1;
    }
    return true;
}
#endif

/****************************************************************************/
static __inline__ void _USB_FindNextToken_Interrupt_RW_Data( uint8_t token )
{
#ifdef DEBUG_ENABLE
    DEBUG_PutStringHexU8( "I", pTargetInterruptDeviceInfo->deviceAddress ); 
#endif
    _USB_SetBDT( pTargetInterruptEndpointInfo, token );
    _USB_SendToken( pTargetInterruptDeviceInfo, pTargetInterruptEndpointInfo, token );
}

/****************************************************************************/
static __inline__ void _USB_FindNextToken_Interrupt_Complete( uint8_t transferState )
{
    pTargetInterruptEndpointInfo->transferState             = TSTATE_IDLE;
//  pTargetEndpointInfo->wIntervalCount            = pTargetEndpointInfo->wInterval;
    pTargetInterruptEndpointInfo->status.bfTransferComplete = 1;
    usbBusInfo.flags.bfInterruptRequestProcessing  = 0;
#if defined( USB_ENABLE_TRANSFER_EVENT )
    if (transferState != TSUBSTATE_ERROR) {
        StructEventQueueAdd_Success(pTargetInterruptDeviceInfo, pTargetInterruptEndpointInfo);
    } else {
        StructEventQueueAdd_Error(pTargetInterruptDeviceInfo, pTargetInterruptEndpointInfo);
    }
#endif
}

/****************************************************************************/
/* token : Read:USB_TOKEN_IN  Write:USB_TOKEN_OUT */
static __inline__ bool _USB_FindNextToken_Interrupt_ReadWrite( uint8_t token )
{
    uint8_t transferState = pTargetInterruptEndpointInfo->transferState & TSUBSTATE_MASK;
    switch (transferState)
    {
        case TSUBSTATE_INTERRUPT_RW_DATA:
            _USB_FindNextToken_Interrupt_RW_Data( token );
            return false;
            break;

        case TSUBSTATE_INTERRUPT_RW_COMPLETE:
#ifdef DEBUG_ENABLE
            DEBUG_PutStringHexU8( "Interrupt Complete: ", transferState ); 
#endif
            _USB_FindNextToken_Interrupt_Complete( transferState );
            break;

        default:
            _USB_FindNextToken_Interrupt_Complete( TSUBSTATE_ERROR );
            break;
    }
    
    return true;
}

/****************************************************************************/
static __inline__ bool _USB_FindNextToken_Interrupt( void )
{
    USB_TRANSFER_DATA *item = NULL;

    if (usbBusInfo.flags.bfInterruptTransfersDone)
    {
        return true;
    }
//    if (usbBusInfo.flags.bfControlRequestProcessing | usbBusInfo.flags.bfBulkRequestProcessing | usbBusInfo.flags.bfIsochronousRequestProcessing)
//    {
//        return true;
//    }

    if (!usbBusInfo.flags.bfInterruptRequestProcessing) {
        item = StructTransferInterruptQueueGet();
        if (!item) {
            // no request found
            return true;
        }
        pTargetInterruptEndpointInfo = item->endpointInfo;
        pTargetInterruptDeviceInfo = item->deviceInfo;
        usbBusInfo.flags.bfInterruptRequestProcessing = 1;
#ifdef DEBUG_ENABLE
        DEBUG_PutStringHexU8( "New Interrupt EPAddr: ", item->endpointInfo->bEndpointAddress ); 
        DEBUG_PutStringHexU8( "dataSize: ", item->endpointInfo->dataCountMax ); 
#endif
    }

    // Look for any interrupt operations.
    if (pTargetInterruptEndpointInfo->status.bfIntervalCountIsZero != 0)
    {
        switch (pTargetInterruptEndpointInfo->transferState & TSTATE_MASK)
        {
            case TSTATE_INTERRUPT_READ:
                if (!_USB_FindNextToken_Interrupt_ReadWrite( USB_TOKEN_IN )) {
                    return false;
                }
                break;

            case TSTATE_INTERRUPT_WRITE:
                if (!_USB_FindNextToken_Interrupt_ReadWrite( USB_TOKEN_OUT )) {
                    return false;
                }
                break;

            default:
                _USB_FindNextToken_Interrupt_Complete( TSUBSTATE_ERROR );
                break;
        }
    }

    // If we've gone through all the endpoints, we do not have any more interrupt transfers.
    usbBusInfo.flags.bfInterruptTransfersDone = 1;

    return true;
}

#ifdef USB_SUPPORT_BULK_TRANSFERS
/****************************************************************************/
static __inline__ bool _USB_FindNextToken_Bulk_Read(bool *illegalState)
{
    switch (pTargetEndpointInfo->transferState & TSUBSTATE_MASK)
    {
        case TSUBSTATE_BULK_READ_DATA:
            _USB_SetBDT( USB_TOKEN_IN );
            _USB_SendToken( pTargetEndpointInfo->bEndpointAddress, USB_TOKEN_IN );
            return false;
            break;

        case TSUBSTATE_BULK_READ_COMPLETE:
            pTargetEndpointInfo->transferState               = TSTATE_IDLE;
            pTargetEndpointInfo->status.bfTransferComplete   = 1;
            #if defined( USB_ENABLE_TRANSFER_EVENT )
                if (StructQueueIsNotFull(&usbEventQueue, USB_EVENT_QUEUE_DEPTH))
                {
                    USB_EVENT_DATA *data;

                    data = StructQueueAdd(&usbEventQueue, USB_EVENT_QUEUE_DEPTH);
                    data->event = EVENT_TRANSFER;
                    data->TransferData.dataCount        = pTargetEndpointInfo->dataCount;
                    data->TransferData.pUserData        = pTargetEndpointInfo->pUserData;
                    data->TransferData.bErrorCode       = USB_SUCCESS;
                    data->TransferData.bEndpointAddress = pTargetEndpointInfo->bEndpointAddress;
                    data->TransferData.bmAttributes.val = pTargetEndpointInfo->bmAttributes.val;
                    data->TransferData.clientDriver     = pTargetEndpointInfo->clientDriver;
                }
                else
                {
                    pTargetEndpointInfo->bmAttributes.val = USB_EVENT_QUEUE_FULL;
                }
            #endif
            break;

        case TSUBSTATE_ERROR:
            pTargetEndpointInfo->transferState               = TSTATE_IDLE;
            pTargetEndpointInfo->status.bfTransferComplete   = 1;
            #if defined( USB_ENABLE_TRANSFER_EVENT )
                if (StructQueueIsNotFull(&usbEventQueue, USB_EVENT_QUEUE_DEPTH))
                {
                    USB_EVENT_DATA *data;

                    data = StructQueueAdd(&usbEventQueue, USB_EVENT_QUEUE_DEPTH);
                    data->event = EVENT_BUS_ERROR;
                    data->TransferData.dataCount        = 0;
                    data->TransferData.pUserData        = NULL;
                    data->TransferData.bErrorCode       = pTargetEndpointInfo->bErrorCode;
                    data->TransferData.bEndpointAddress = pTargetEndpointInfo->bEndpointAddress;
                    data->TransferData.bmAttributes.val = pTargetEndpointInfo->bmAttributes.val;
                    data->TransferData.clientDriver     = pTargetEndpointInfo->clientDriver;
                }
                else
                {
                    pTargetEndpointInfo->bmAttributes.val = USB_EVENT_QUEUE_FULL;
                }
            #endif
            break;

        default:
            *illegalState = true;
            break;
    }
    return true;
}

/****************************************************************************/
static __inline__ bool _USB_FindNextToken_Bulk_Write(bool *illegalState)
{
    switch (pTargetEndpointInfo->transferState & TSUBSTATE_MASK)
    {
        case TSUBSTATE_BULK_WRITE_DATA:
            _USB_SetBDT( USB_TOKEN_OUT );
            _USB_SendToken( pTargetEndpointInfo->bEndpointAddress, USB_TOKEN_OUT );
            return false;
            break;

        case TSUBSTATE_BULK_WRITE_COMPLETE:
            pTargetEndpointInfo->transferState               = TSTATE_IDLE;
            pTargetEndpointInfo->status.bfTransferComplete   = 1;
            #if defined( USB_ENABLE_TRANSFER_EVENT )
                if (StructQueueIsNotFull(&usbEventQueue, USB_EVENT_QUEUE_DEPTH))
                {
                    USB_EVENT_DATA *data;

                    data = StructQueueAdd(&usbEventQueue, USB_EVENT_QUEUE_DEPTH);
                    data->event = EVENT_TRANSFER;
                    data->TransferData.dataCount        = pTargetEndpointInfo->dataCount;
                    data->TransferData.pUserData        = pTargetEndpointInfo->pUserData;
                    data->TransferData.bErrorCode       = USB_SUCCESS;
                    data->TransferData.bEndpointAddress = pTargetEndpointInfo->bEndpointAddress;
                    data->TransferData.bmAttributes.val = pTargetEndpointInfo->bmAttributes.val;
                    data->TransferData.clientDriver     = pTargetEndpointInfo->clientDriver;
                }
                else
                {
                    pTargetEndpointInfo->bmAttributes.val = USB_EVENT_QUEUE_FULL;
                }
            #endif
            break;

        case TSUBSTATE_ERROR:
            pTargetEndpointInfo->transferState               = TSTATE_IDLE;
            pTargetEndpointInfo->status.bfTransferComplete   = 1;
            #if defined( USB_ENABLE_TRANSFER_EVENT )
                if (StructQueueIsNotFull(&usbEventQueue, USB_EVENT_QUEUE_DEPTH))
                {
                    USB_EVENT_DATA *data;

                    data = StructQueueAdd(&usbEventQueue, USB_EVENT_QUEUE_DEPTH);
                    data->event = EVENT_BUS_ERROR;
                    data->TransferData.dataCount        = 0;
                    data->TransferData.pUserData        = NULL;
                    data->TransferData.bErrorCode       = pTargetEndpointInfo->bErrorCode;
                    data->TransferData.bEndpointAddress = pTargetEndpointInfo->bEndpointAddress;
                    data->TransferData.bmAttributes.val = pTargetEndpointInfo->bmAttributes.val;
                    data->TransferData.clientDriver     = pTargetEndpointInfo->clientDriver;
                }
                else
                {
                    pTargetEndpointInfo->bmAttributes.val = USB_EVENT_QUEUE_FULL;
                }
            #endif
            break;

        default:
            *illegalState = true;
            break;
    }
    return true;
}

/****************************************************************************/
static __inline__ bool _USB_FindNextToken_Bulk(bool *illegalState)
{
#ifdef ALLOW_MULTIPLE_BULK_TRANSACTIONS_PER_FRAME
TryBulk:
#endif

    if (!usbBusInfo.flags.bfBulkTransfersDone)
    {
#ifndef ALLOW_MULTIPLE_BULK_TRANSACTIONS_PER_FRAME
        // Only go through this section once if we are not allowing multiple transactions
        // per frame.
        usbBusInfo.flags.bfBulkTransfersDone = 1;
#endif

        // Look for any bulk operations.  Try to service all pending requests within the frame.
        if (_USB_FindServiceEndpoint( USB_TRANSFER_TYPE_BULK ))
        {
            switch (pTargetEndpointInfo->transferState & TSTATE_MASK)
            {
                case TSTATE_BULK_READ:
                    if (!_USB_FindNextToken_Bulk_Read(illegalState)) {
                        return false;
                    }
                    break;

                case TSTATE_BULK_WRITE:
                    if (!_USB_FindNextToken_Bulk_Write(illegalState)) {
                        return false;
                    }
                    break;

                default:
                    *illegalState = true;
                    break;
            }

            if (*illegalState)
            {
                // We should never use this, but in case we do, put the endpoint
                // in a recoverable state.
                pTargetEndpointInfo->transferState               = TSTATE_IDLE;
                pTargetEndpointInfo->status.bfTransferComplete   = 1;
            }
        }

        // We've gone through all the bulk transactions, but we have time for more.
        // If we have any bulk transactions, go back to the beginning of the list
        // and start over.
#ifdef ALLOW_MULTIPLE_BULK_TRANSACTIONS_PER_FRAME
        if (usbBusInfo.countBulkTransactions)
        {
            usbBusInfo.lastBulkTransaction = 0;
            goto TryBulk;

        }
#endif

        // If we've gone through all the endpoints, we do not have any more bulk transfers.
        usbBusInfo.flags.bfBulkTransfersDone = 1;

    }
    return true;
}
#endif

/****************************************************************************
  Function:
    void USB_FindNextToken( )

  Description:
    This function determines the next token to send of all current pending
    transfers.

  Precondition:
    None

  Parameters:
    None

  Return Values:
    true    - A token was sent
    false   - No token was found to send, so the routine can be called again.

  Remarks:
    This routine is only called from an interrupt handler, either SOFIF or
    TRNIF.
  ***************************************************************************/

void USB_FindNextToken( void )
{
//    bool    illegalState = false;

    // If the device is suspended or resuming, do not send any tokens.  We will
    // send the next token on an SOF interrupt after the resume recovery time
    // has expired.
#ifdef USE_USB_DEVICE_SUSPEND
    if ((usbHostState & (SUBSTATE_MASK | SUBSUBSTATE_MASK)) == (STATE_RUNNING | SUBSTATE_SUSPEND_AND_RESUME))
    {
        return;
    }
#endif

    // If we are currently sending a token, we cannot do anything.  We will come
    // back in here when we get either the Token Done or the Start of Frame interrupt.
    if (usbBusInfo.flags.bfTokenAlreadyWritten) //(U1CONbits.TOKBUSY)
    {
        return;
    }

    // We will handle control transfers first.  We only allow one control
    // transfer per frame.
    if (!_USB_FindNextToken_Control())
    {
        return;
    }

#ifdef USB_SUPPORT_ISOCHRONOUS_TRANSFERS
    // Next, we will handle isochronous transfers.  We must be careful with
    // these.  The maximum packet size for an isochronous transfer is 1023
    // bytes, so we cannot use the threshold register (U1SOF) to ensure that
    // we do not write too many tokens during a frame.  Instead, we must count
    // the number of bytes we are sending and stop sending isochronous
    // transfers when we reach that limit.

    // MCHP: Implement scheduling by using usbBusInfo.dBytesSentInFrame

    // Current Limitation:  The stack currently supports only one attached
    // device.  We will make the assumption that the control, isochronous, and
    // interrupt transfers requested by a single device will not exceed one
    // frame, and defer the scheduler.

    // Due to the nature of isochronous transfers, transfer events must be used.
    #if !defined( USB_ENABLE_TRANSFER_EVENT )
        #warning Transfer events are required for isochronous transfers
    #endif

    if (!_USB_FindNextToken_Isochronous()) {
        return;
    }
#endif

#ifdef USB_SUPPORT_INTERRUPT_TRANSFERS
    if (!_USB_FindNextToken_Interrupt())
    {
        return;
    }
#endif

#ifdef USB_SUPPORT_BULK_TRANSFERS
    if (!_USB_FindNextToken_Bulk())
    {
        return;
    }
#endif

    return;
}

/****************************************************************************
  Function:
    void USB_InitControlReadWrite( )

  Description:
    This function sets up the endpoint information for a control (SETUP)
    transfer that will write information.

  Precondition:
    All error checking must be done prior to calling this function.

  Parameters:
    USB_DEVICE_INFO *deviceInfo - Points to the desired device info
    USB_ENDPOINT_INFO *pEndpoint    - Points to the desired endpoint
                                                      in the endpoint information list.
    uint8_t *pControlData              - Points to the SETUP message.
    uint16_t controlSize                - Size of the SETUP message.
    uint8_t *pData                     - Points to where the write data
                                                      is to be stored.
    uint16_t size                       - Number of data bytes to write.

  Returns:
    None

  Remarks:
    Since endpoint servicing is interrupt driven, the bfTransferComplete
    flag must be set last.
  ***************************************************************************/

void USB_InitControlReadWrite( bool is_write
    , USB_DEVICE_INFO *deviceInfo
    , USB_ENDPOINT_INFO *pEndpoint
    , SETUP_PKT *pControlData, uint16_t controlSize
    , uint8_t *pData, uint16_t size )
{
    pEndpoint->status.bfStalled             = 0;
    pEndpoint->status.bfError               = 0;
    pEndpoint->status.bfUserAbort           = 0;
    pEndpoint->status.bfTransferSuccessful  = 0;
    pEndpoint->status.bfErrorCount          = 0;
    pEndpoint->status.bfLastTransferNAKd    = 0;
    pEndpoint->pUserData                    = pData;
    pEndpoint->dataCount                    = 0;
    pEndpoint->dataCountMax                 = size;
    pEndpoint->countNAKs                    = 0;

    pEndpoint->pUserDataSETUP               = pControlData;
    pEndpoint->dataCountMaxSETUP            = controlSize;

    if (is_write)
    {
        if (size == 0)
        {
            pEndpoint->transferState    = TSTATE_CONTROL_NO_DATA;
        }
        else
        {
            pEndpoint->transferState    = TSTATE_CONTROL_WRITE;
        }
    }
    else
    {
        pEndpoint->transferState    = TSTATE_CONTROL_READ;
    }

    StructTransferControlQueueAdd( deviceInfo, pEndpoint );

    // Set the flag last so all the parameters are set for an interrupt.
    pEndpoint->status.bfTransferComplete    = 0;
}

/****************************************************************************/

#if (defined(USB_SUPPORT_INTERRUPT_TRANSFERS) && defined(USB_SUPPORT_ISOCHRONOUS_TRANSFERS))
 || (defined(USB_SUPPORT_INTERRUPT_TRANSFERS) && defined(USB_SUPPORT_BULK_TRANSFERS))
 || (defined(USB_SUPPORT_ISOCHRONOUS_TRANSFERS) && defined(USB_SUPPORT_ISOCHRONOUS_TRANSFERS))
#define USB_TRANSFERS_SWITCH(x) switch(x)
#define USB_TRANSFERS_CASE(x)   case (x):
#define USB_TRANSFERS_BREAK     break
#else
#define USB_TRANSFERS_SWITCH(x)
#define USB_TRANSFERS_CASE(x)
#define USB_TRANSFERS_BREAK
#endif

/****************************************************************************
  Function:
    void _USB_InitReadWrite( )

  Description:
    This function sets up the endpoint information for an interrupt,
    isochronous, or bulk read.  If the transfer is isochronous, the pData
    and size parameters have different meaning.

  Precondition:
    All error checking must be done prior to calling this function.

  Parameters:
    bool is_write                 - true if transfer to client
    USB_DEVICE_INFO *deviceInfo   - Points to the desired device info
    USB_ENDPOINT_INFO *pEndpoint  - Points to the desired endpoint in the
                                    endpoint information list.
    uint8_t *pData                   - Points to where the data is to be
                                    stored.  If the endpoint is isochronous,
                                    this points to an ISOCHRONOUS_DATA_BUFFERS
                                    structure.
    uint16_t size                     - Number of data bytes to read. If the
                                    endpoint is isochronous, this is the number
                                    of data buffer pointers pointed to by
                                    pData.

  Returns:
    None

  Remarks:
    * Control reads should use the routine _USB_InitControlRead().  Since
        endpoint servicing is interrupt driven, the bfTransferComplete flag
        must be set last.

    * For interrupt and isochronous endpoints, we let the interval count
        free run.  The transaction will begin when the interval count
        reaches 0.
  ***************************************************************************/

void USB_InitReadWrite( bool is_write
    , USB_DEVICE_INFO *deviceInfo
    , USB_ENDPOINT_INFO *pEndpoint
    , uint8_t *pData, uint16_t size )
{
    pEndpoint->status.bfUserAbort           = 0;
    pEndpoint->status.bfTransferSuccessful  = 0;
    pEndpoint->status.bfErrorCount          = 0;
    pEndpoint->status.bfLastTransferNAKd    = 0;
    pEndpoint->pUserData                    = pData;
    pEndpoint->dataCount                    = 0;
    pEndpoint->dataCountMax                 = size; // Not used for isochronous.
    pEndpoint->countNAKs                    = 0;

    USB_TRANSFERS_SWITCH(pEndpoint->bmAttributes.bfTransferType)
    {
#ifdef USB_SUPPORT_INTERRUPT_TRANSFERS
        USB_TRANSFERS_CASE(USB_TRANSFER_TYPE_INTERRUPT)
            pEndpoint->transferState            = TSTATE_INTERRUPT_READ;
            StructTransferInterruptQueueAdd( deviceInfo, pEndpoint );
            USB_TRANSFERS_BREAK;
#endif
#ifdef USB_SUPPORT_ISOCHRONOUS_TRANSFERS
        USB_TRANSFERS_CASE(USB_TRANSFER_TYPE_ISOCHRONOUS)
            pEndpoint->transferState            = TSTATE_ISOCHRONOUS_READ;
            ((ISOCHRONOUS_DATA *)pEndpoint->pUserData)->currentBufferUSB = 0;
            StructTransferIsochronousQueueAdd( deviceInfo, pEndpoint );
            USB_TRANSFERS_BREAK;
#endif
#ifdef USB_SUPPORT_BULK_TRANSFERS
        USB_TRANSFERS_CASE(USB_TRANSFER_TYPE_BULK)
            pEndpoint->transferState            = TSTATE_BULK_READ;
            StructTransferBulkQueueAdd( deviceInfo, pEndpoint );
            USB_TRANSFERS_BREAK;
#endif
    }
	if (is_write) {
        pEndpoint->transferState |= 0x0010;
	}

    // Set the flag last so all the parameters are set for an interrupt.
    pEndpoint->status.bfTransferComplete    = 0;
}

/****************************************************************************
  Function:
    void _USB_SendToken( )

  Description:
    This function sets up the endpoint control register and sends the token.

  Precondition:
    None

  Parameters:
    uint8_t endpoint   - Endpoint number
    uint8_t tokenType  - Token to send

  Returns:
    None

  Remarks:
    If the device is low speed, the transfer must be set to low speed.  If
    the endpoint is isochronous, handshaking must be disabled.
  ***************************************************************************/

void _USB_SendToken( USB_DEVICE_INFO *pDeviceInfo, USB_ENDPOINT_INFO *pEndpointInfo, uint8_t tokenType)
{
    uint8_t    temp;

    // Disable retries, disable control transfers, enable Rx and Tx and handshaking.
    temp = 0x5D;

    // Enable low speed transfer if the device is low speed.
    temp |= (pDeviceInfo->deviceSpeed & 0x80);   // Set LSPD

    // Enable control transfers if necessary.
    if (pEndpointInfo->bmAttributes.bfTransferType == USB_TRANSFER_TYPE_CONTROL)
    {
        temp &= 0xEF;   // Clear EPCONDIS
    }

#ifdef USB_SUPPORT_ISOCHRONOUS_TRANSFERS
    // Disable handshaking for isochronous endpoints.
    if (pTargetEndpointInfo->bmAttributes.bfTransferType == USB_TRANSFER_TYPE_ISOCHRONOUS)
    {
        temp &= 0xFE;   // Clear EPHSHK
    }
#endif

//#ifdef DEBUG_ENABLE
//    DEBUG_PutHexU8(pTargetDeviceInfo->deviceAddress | pTargetDeviceInfo->deviceSpeed);
//#endif    
    
    // Start transaction
    U1EP0 = temp;
    U1ADDR = (pDeviceInfo->deviceAddress | (pDeviceInfo->deviceSpeed << 1));
    U1TOK = (tokenType << 4) | (pEndpointInfo->bEndpointAddress & 0xF);
#ifdef DEBUG_ENABLE
    if (pEndpointInfo->bmAttributes.bfTransferType == USB_TRANSFER_TYPE_CONTROL) {
        DEBUG_PutStringHexU8("SendEP0:", U1EP0);
        DEBUG_PutStringHexU8("Addr :", U1ADDR);
        DEBUG_PutStringHexU8("Token:", U1TOK);
    }
#endif    
    // Set current transfer type
    currentTransferType = pEndpointInfo->bmAttributes.bfTransferType;

    // Lock out anyone from writing another token until this one has finished.
//    U1CONbits.TOKBUSY = 1;
    usbBusInfo.flags.bfTokenAlreadyWritten = 1;
}

/****************************************************************************
  Function:
    void _USB_ClearBDT( )

  Description:
    This function initialize a pointer of the BDT for the transfer.

  Precondition:
    None

  Parameters:
    None

  Returns:
    None

  Remarks:
    None
  ***************************************************************************/

void _USB_ClearBDT(void)
{
#if defined(__PIC32__)
   U1BDTP1 = ((uint32_t)KVA_TO_PA(&BDT) & 0x0000FF00) >> 8;
   U1BDTP2 = ((uint32_t)KVA_TO_PA(&BDT) & 0x00FF0000) >> 16;
   U1BDTP3 = ((uint32_t)KVA_TO_PA(&BDT) & 0xFF000000) >> 24;
#else
    #error "Cannot set up the Buffer Descriptor Table pointer."
#endif
}

/****************************************************************************
  Function:
    void _USB_SetBDT( uint8_t token )

  Description:
    This function sets up the BDT for the transfer.  The function handles the
    different ping-pong modes.

  Precondition:
    pTargetEndpointInfo must point to the current endpoint being serviced.

  Parameters:
    uint8_t token  - Token for the transfer.  That way we can tell which
                    ping-pong buffer and which data pointer to use.  Valid
                    values are:
                        * USB_TOKEN_SETUP
                        * USB_TOKEN_IN
                        * USB_TOKEN_OUT

  Returns:
    None

  Remarks:
    None
  ***************************************************************************/

void _USB_SetBDT( USB_ENDPOINT_INFO *pEndpointInfo, uint8_t token )
{
    uint16_t             currentPacketSize;
    BDT_ENTRY           *pBDT;

    if (token == USB_TOKEN_IN)
    {
        // Find the BDT we need to use.
        #if (USB_PING_PONG_MODE == USB_PING_PONG__FULL_PING_PONG)
            if (usbBusInfo.flags.bfPingPongIn)
            {
                pBDT = BDT_IN_ODD;
#ifdef DEBUG_ENABLE
                DEBUG_PutString("Set: BDT_IN_ODD\r\n");
#endif
            } else {
                pBDT = BDT_IN;
#ifdef DEBUG_ENABLE
                DEBUG_PutString("Set: BDT_IN\r\n");
#endif
            }
        #else
            pBDT = BDT_IN;
        #endif

        // Set up ping-pong for the next transfer
        #if (USB_PING_PONG_MODE == USB_PING_PONG__FULL_PING_PONG)
            usbBusInfo.flags.bfPingPongIn = ~usbBusInfo.flags.bfPingPongIn;
        #endif
    }
    else  // USB_TOKEN_OUT or USB_TOKEN_SETUP
    {
        // Find the BDT we need to use.
        #if (USB_PING_PONG_MODE == USB_PING_PONG__FULL_PING_PONG) || (USB_PING_PONG_MODE == USB_PING_PONG__EP0_OUT_ONLY)
            if (usbBusInfo.flags.bfPingPongOut)
            {
                pBDT = BDT_OUT_ODD;
#ifdef DEBUG_ENABLE
                DEBUG_PutString("Set: BDT_OUT_ODD\r\n");
#endif
            } else {
                pBDT = BDT_OUT;
#ifdef DEBUG_ENABLE
                DEBUG_PutString("Set: BDT_OUT\r\n");
#endif
            }
        #else
            pBDT = BDT_OUT;
        #endif

        // Set up ping-pong for the next transfer
        #if (USB_PING_PONG_MODE == USB_PING_PONG__FULL_PING_PONG) || (USB_PING_PONG_MODE == USB_PING_PONG__EP0_OUT_ONLY)
            usbBusInfo.flags.bfPingPongOut = ~usbBusInfo.flags.bfPingPongOut;
        #endif
    }

    // Determine how much data we'll transfer in this packet.
    if (token == USB_TOKEN_SETUP)
    {
        if ((pEndpointInfo->dataCountMaxSETUP - pEndpointInfo->dataCount) > pEndpointInfo->wMaxPacketSize)
        {
            currentPacketSize = pEndpointInfo->wMaxPacketSize;
        }
        else
        {
            currentPacketSize = pEndpointInfo->dataCountMaxSETUP - pEndpointInfo->dataCount;
        }
    }
    else
    {
#ifdef USB_SUPPORT_ISOCHRONOUS_TRANSFERS
        if (pEndpointInfo->bmAttributes.bfTransferType == USB_TRANSFER_TYPE_ISOCHRONOUS)
        {
            if (token == USB_TOKEN_IN)
            {
                /* For an IN token, get the maximum packet size. */
                currentPacketSize = pEndpointInfo->wMaxPacketSize;
            }
            else
            {
                /* For an OUT token, send the amount of data that the user has
                 * provided. */
                currentPacketSize = pEndpointInfo->dataCount;
            }
        }
        else
#endif
        {
            if ((pEndpointInfo->dataCountMax - pEndpointInfo->dataCount) > pEndpointInfo->wMaxPacketSize)
            {
                currentPacketSize = pEndpointInfo->wMaxPacketSize;
            }
            else
            {
                currentPacketSize = pEndpointInfo->dataCountMax - pEndpointInfo->dataCount;
            }
        }
    }

    // Load up the BDT address.
    if (token == USB_TOKEN_SETUP)
    {
        #if defined(__C30__) || defined(__PIC32__) || defined __XC16__
            pBDT->ADR  = ConvertToPhysicalAddress(pEndpointInfo->pUserDataSETUP);
        #else
            #error "Cannot set BDT address."
        #endif
    }
    else
    {
        #if defined(__C30__) || defined __XC16__
#ifdef USB_SUPPORT_ISOCHRONOUS_TRANSFERS
            if (pEndpointInfo->bmAttributes.bfTransferType == USB_TRANSFER_TYPE_ISOCHRONOUS)
            {
                pBDT->ADR  = ConvertToPhysicalAddress(((ISOCHRONOUS_DATA *)(pEndpointInfo->pUserData))->buffers[((ISOCHRONOUS_DATA *)(pEndpointInfo->pUserData))->currentBufferUSB].pBuffer);
            }
            else
#endif
            {
                pBDT->ADR  = ConvertToPhysicalAddress((uint16_t)pEndpointInfo->pUserData + (uint16_t)pEndpointInfo->dataCount);
            }
        #elif defined(__PIC32__)
#ifdef USB_SUPPORT_ISOCHRONOUS_TRANSFERS
            if (pTargetEndpointInfo->bmAttributes.bfTransferType == USB_TRANSFER_TYPE_ISOCHRONOUS)
            {
                pBDT->ADR  = ConvertToPhysicalAddress(((ISOCHRONOUS_DATA *)(pEndpointInfo->pUserData))->buffers[((ISOCHRONOUS_DATA *)(pEndpointInfo->pUserData))->currentBufferUSB].pBuffer);
            }
            else
#endif
            {
                pBDT->ADR  = ConvertToPhysicalAddress((uint32_t)pEndpointInfo->pUserData + (uint32_t)pEndpointInfo->dataCount);
            }
        #else
            #error "Cannot set BDT address."
        #endif
    }

    // Load up the BDT status register.
    pBDT->STAT.Val      = 0;
    pBDT->count         = currentPacketSize;
    pBDT->STAT.DTS      = pEndpointInfo->status.bfNextDATA01;
    pBDT->STAT.DTSEN    = pEndpointInfo->status.bfUseDTS;

    // Transfer the BD to the USB OTG module.
    pBDT->STAT.UOWN     = 1;
}


/****************************************************************************/
static __inline__ void USB_Host_ToggleNextData01(USB_ENDPOINT_INFO *pEndpointInfo)
{
    // Set the NAK retries for the next transaction;
    pEndpointInfo->countNAKs = 0;

    // Toggle DTS for the next transfer.
    pEndpointInfo->status.bfNextDATA01 ^= 0x01;
}

/****************************************************************************/
static void USB_Host_CanNextTransferState(USB_ENDPOINT_INFO *pEndpointInfo, BDT_ENTRY *pBDT)
{
    uint16_t packetSize;

    // Update the count of bytes tranferred.  (If there was an error, this count will be 0.)
    // The Byte Count is NOT 0 if a NAK occurs.  Therefore, we can only update the
    // count when an ACK, DATA0, or DATA1 is received.
    packetSize                  = pBDT->count;
    pEndpointInfo->dataCount += packetSize;

    // We've written all the data when we send a short packet or we have
    // transferred all the data.  If it's an isochronous transfer, this
    // portion is complete, so go to the next state, so we can tell the
    // next higher layer that a batch of data has been transferred.
    if (
#ifdef USB_SUPPORT_ISOCHRONOUS_TRANSFERS
        (pTargetEndpointInfo->bmAttributes.bfTransferType == USB_TRANSFER_TYPE_ISOCHRONOUS) ||
#endif
        (packetSize < pEndpointInfo->wMaxPacketSize) ||
        (pEndpointInfo->dataCount >= pEndpointInfo->dataCountMax))
    {
        // We've written all the data. Proceed to the next step.
        pEndpointInfo->status.bfTransferSuccessful = 1;
        _USB_SetNextTransferState(pEndpointInfo);
    }
    else
    {
        // We need to process more data.  Keep this endpoint in its current
        // transfer state.
    }
}

/****************************************************************************/
static void USB_HostInterrupt_Transfer_ACK(USB_ENDPOINT_INFO *pEndpointInfo, BDT_ENTRY *pBDT)
{
#if defined (DEBUG_ENABLE)
    DEBUG_PutString( "*A\r\n" );
#endif

    // We will only get this PID from an OUT or SETUP packet.

    // The Byte Count is NOT 0 if a NAK occurs.  Therefore, we can only update the
    // count when an ACK, DATA0, or DATA1 is received.
    USB_Host_ToggleNextData01(pEndpointInfo);

    if ((pEndpointInfo->transferState == (TSTATE_CONTROL_NO_DATA | TSUBSTATE_CONTROL_NO_DATA_SETUP)) ||
        (pEndpointInfo->transferState == (TSTATE_CONTROL_READ    | TSUBSTATE_CONTROL_READ_SETUP)) ||
        (pEndpointInfo->transferState == (TSTATE_CONTROL_WRITE   | TSUBSTATE_CONTROL_WRITE_SETUP)))
    {
        // Update the count of bytes tranferred.  (If there was an error, this count will be 0.)
        pEndpointInfo->dataCount += pBDT->count;

        // We are doing SETUP transfers. See if we are done with the SETUP portion.
        if (pEndpointInfo->dataCount >= pEndpointInfo->dataCountMaxSETUP)
        {
            // We are done with the SETUP.  Reset the byte count and
            // proceed to the next token.
            pEndpointInfo->dataCount = 0;
            _USB_SetNextTransferState(pEndpointInfo);
        }
        else {
        }
    }
    else
    {
        // We are doing OUT transfers.  See if we've written all the data.
        USB_Host_CanNextTransferState(pEndpointInfo, pBDT);
    }
}

/****************************************************************************/
static void USB_HostInterrupt_Transfer_DATA0(USB_ENDPOINT_INFO *pEndpointInfo, BDT_ENTRY *pBDT)
{
#if defined (DEBUG_ENABLE)
    DEBUG_PutString( "*DATA\r\n" );
#endif
    // We will only get these PID's from an IN packet.

    // Update the count of bytes tranferred.  (If there was an error, this count will be 0.)
    // The Byte Count is NOT 0 if a NAK occurs.  Therefore, we can only update the
    // count when an ACK, DATA0, or DATA1 is received.
    USB_Host_ToggleNextData01(pEndpointInfo);

    // We are doing IN transfers.  See if we've received all the data.
    USB_Host_CanNextTransferState(pEndpointInfo, pBDT);
}

/****************************************************************************/
static void USB_HostInterrupt_Transfer_NAK_Timeout(USB_ENDPOINT_INFO *pEndpointInfo)
{
    if (pEndpointInfo->status.bfNAKTimeoutEnabled &&
        (pEndpointInfo->countNAKs > pEndpointInfo->timeoutNAKs))
    {
        pEndpointInfo->status.bfError    = 1;
        pEndpointInfo->bErrorCode        = USB_ENDPOINT_NAK_TIMEOUT;
        _USB_SetTransferErrorState( pEndpointInfo );
    }
}

/****************************************************************************/
static void USB_HostInterrupt_Transfer_NAK(USB_ENDPOINT_INFO *pEndpointInfo, BDT_ENTRY *pBDT)
{
#if defined (DEBUG_ENABLE)
    DEBUG_PutString( "*NAK\r\n" );
#endif

#ifndef ALLOW_MULTIPLE_NAKS_PER_FRAME
    pEndpointInfo->status.bfLastTransferNAKd = 1;
#endif

    pEndpointInfo->countNAKs ++;

    switch( pEndpointInfo->bmAttributes.bfTransferType )
    {
#ifdef USB_SUPPORT_BULK_TRANSFERS
        case USB_TRANSFER_TYPE_BULK:
            // Bulk IN and OUT transfers are allowed to retry NAK'd
            // transactions until a timeout (if enabled) or indefinitely
                // (if NAK timeouts disabled).
#endif
        case USB_TRANSFER_TYPE_CONTROL:
            // Devices should not NAK the SETUP portion.  If they NAK
            // the DATA portion, they are allowed to retry a fixed
            // number of times.
            USB_HostInterrupt_Transfer_NAK_Timeout(pEndpointInfo);
            break;

#ifdef USB_SUPPORT_INTERRUPT_TRANSFERS
        case USB_TRANSFER_TYPE_INTERRUPT:
            if ((pEndpointInfo->bEndpointAddress & 0x80) == 0x00)
            {
                // Interrupt OUT transfers are allowed to retry NAK'd
                // transactions until a timeout (if enabled) or indefinitely
                // (if NAK timeouts disabled).
                USB_HostInterrupt_Transfer_NAK_Timeout(pEndpointInfo);
            }
            else
            {
                // Interrupt IN transfers terminate with no error.
                pEndpointInfo->status.bfTransferSuccessful = 1;
                _USB_SetNextTransferState(pEndpointInfo);
            }
            break;
#endif
#ifdef USB_SUPPORT_ISOCHRONOUS_TRANSFERS
        case USB_TRANSFER_TYPE_ISOCHRONOUS:
            // Isochronous transfers terminate with no error.
            pTargetEndpointInfo->status.bfTransferSuccessful = 1;
            _USB_SetNextTransferState(pEndpointInfo);
            break;
#endif
    }
}

/****************************************************************************/
static void USB_HostInterrupt_Transfer_STALL(USB_ENDPOINT_INFO *pEndpointInfo, BDT_ENTRY *pBDT)
{
    // Device is stalled.  Stop the transfer, and indicate the stall.
    // The application must clear this if not a control endpoint.
    // A stall on a control endpoint does not indicate that the
    // endpoint is halted.
#if defined (DEBUG_ENABLE)
    UART_PutString( "*STALL\r\n" );
#endif

    pEndpointInfo->status.bfStalled = 1;
    pEndpointInfo->bErrorCode       = USB_ENDPOINT_STALLED;
    _USB_SetTransferErrorState( pEndpointInfo );
}

/****************************************************************************/
static void USB_HostInterrupt_Transfer_Error(USB_ENDPOINT_INFO *pEndpointInfo, BDT_ENTRY *pBDT)
{
#if defined (DEBUG_ENABLE)
    UART_PutStringHexU8( "*E ", pBDT->STAT.Val & 0xff );
#endif
    // Module-defined PID - Bus Timeout (0x0) or Data Error (0x0F).  Increment the error count.
    // NOTE: If DTS is enabled and the packet has the wrong DTS value, a PID of 0x0F is
    // returned.  The hardware, however, acknowledges the packet, so the device thinks
    // that the host has received it.  But the data is not actually received, and the application
    // layer is not informed of the packet.
    pEndpointInfo->status.bfErrorCount++;

    if (pEndpointInfo->status.bfErrorCount >= USB_TRANSACTION_RETRY_ATTEMPTS)
    {
        // We have too many errors.

        // Stop the transfer and indicate an error.
        // The application must clear this.
        pEndpointInfo->status.bfError    = 1;
        pEndpointInfo->bErrorCode        = USB_ENDPOINT_ERROR_ILLEGAL_PID;
        _USB_SetTransferErrorState( pEndpointInfo );

        // Avoid the error interrupt code, because we are going to
        // find another token to send.
        U1EIR = 0xFF;
        U1IR  = U1IE_INTERRUPT_ERROR;
    }
    else
    {
        // Fall through.  This will automatically cause the transfer
        // to be retried.
    }
}
/****************************************************************************/
static USB_ENDPOINT_INFO *_USBTrans_GetEndpointByCurrentTransferType( void )
{
    USB_ENDPOINT_INFO *pEndpointInfo;
    switch(currentTransferType) {
        case USB_TRANSFER_TYPE_CONTROL:
            pEndpointInfo = pTargetControlEndpointInfo;
            break;
        case USB_TRANSFER_TYPE_INTERRUPT:
            pEndpointInfo = pTargetInterruptEndpointInfo;
            break;
        default:
            pEndpointInfo = NULL;
    }
    return pEndpointInfo;
}

/****************************************************************************/
void USB_HostInterrupt_Transfer( void )
{
#if defined(__PIC32__)
    __U1STATbits_t      copyU1STATbits;
#else
    #error "Need structure name for copyU1STATbits."
#endif
//    uint16_t            packetSize;
    BDT_ENTRY           *pBDT;

#if defined (DEBUG_ENABLE)
    DEBUG_PutChar( '!' );
#endif
    USB_ENDPOINT_INFO *pEndpointInfo = _USBTrans_GetEndpointByCurrentTransferType();

    // The previous token has finished, so clear the way for writing a new one.
    usbBusInfo.flags.bfTokenAlreadyWritten = 0;

#if defined (DEBUG_ENABLE)
    DEBUG_PutStringHexU8("U1STAT:", (uint8_t)U1STAT);
    DEBUG_Flush();
#endif

    copyU1STATbits = U1STATbits;    // Read the status register before clearing the flag.

    U1IR = U1IE_INTERRUPT_TRANSFER;  // Clear the interrupt by writing a '1' to the flag.

#ifdef DEBUG_ENABLE
    uint8_t bdt_type = 0;
#endif
    // In host mode, U1STAT does NOT reflect the endpoint.  It is really the last updated
    // BDT, which, in host mode, is always 0.  To get the endpoint, we either need to look
    // at U1TOK, or trust that pTargetEndpointInfo is still accurate.
    if ((pEndpointInfo->bEndpointAddress & 0x0F) == (U1TOK & 0x0F))
    {
        if (copyU1STATbits.DIR)     // TX
        {
            // We are processing OUT or SETUP packets.
            // Set up the BDT pointer for the transaction we just received.
            #if (USB_PING_PONG_MODE == USB_PING_PONG__EP0_OUT_ONLY) || (USB_PING_PONG_MODE == USB_PING_PONG__FULL_PING_PONG)
                if (copyU1STATbits.PPBI) // Odd
                {
                    pBDT = BDT_OUT_ODD;
#ifdef DEBUG_ENABLE
                    bdt_type = 0x02;
#endif
                } else {
                    pBDT = BDT_OUT;
#ifdef DEBUG_ENABLE
                    bdt_type = 0x01;
#endif
                }
            #elif (USB_PING_PONG_MODE == USB_PING_PONG__NO_PING_PONG) || (USB_PING_PONG_MODE == USB_PING_PONG__ALL_BUT_EP0)
                pBDT = BDT_OUT;
            #endif
        }
        else
        {
            // We are processing IN packets.
            // Set up the BDT pointer for the transaction we just received.
            #if (USB_PING_PONG_MODE == USB_PING_PONG__FULL_PING_PONG)
                if (copyU1STATbits.PPBI) // Odd
                {
                    pBDT = BDT_IN_ODD;
#ifdef DEBUG_ENABLE
                    bdt_type = 0x82;
#endif
                } else {
                    pBDT = BDT_IN;
#ifdef DEBUG_ENABLE
                    bdt_type = 0x81;
#endif
                }
            #else
                pBDT = BDT_IN;
            #endif
        }

#ifdef DEBUG_ENABLE
        DEBUG_PutStringHexU8("BDT:", bdt_type);
        DEBUG_PutStringHexU8(">:", pBDT->STAT.PID);
#endif
        if (pBDT->STAT.PID == PID_ACK)
        {
            USB_HostInterrupt_Transfer_ACK(pEndpointInfo, pBDT);
        }
        else if ((pBDT->STAT.PID == PID_DATA0) || (pBDT->STAT.PID == PID_DATA1))
        {
            USB_HostInterrupt_Transfer_DATA0(pEndpointInfo, pBDT);
        }
        else if (pBDT->STAT.PID == PID_NAK)
        {
            USB_HostInterrupt_Transfer_NAK(pEndpointInfo, pBDT);
        }
        else if (pBDT->STAT.PID == PID_STALL)
        {
            USB_HostInterrupt_Transfer_STALL(pEndpointInfo, pBDT);
        }
        else
        {
            USB_HostInterrupt_Transfer_Error(pEndpointInfo, pBDT);
        }
    }
    else
    {
        // We have a mismatch between the endpoint we were expecting and the one that we got.
        // The user may be trying to select a new configuration.  Discard the transaction.
    }

    USB_FindNextToken();
}

/****************************************************************************/
void USB_HostInterrupt_SOF( void )
{
#if defined(USB_ENABLE_SOF_EVENT) && defined(USB_HOST_APP_DATA_EVENT_HANDLER)
    //Notify ping all client drivers of SOF event (address, event, data, sizeof_data)
    _USB_NotifyDataClients(0, EVENT_SOF, NULL, 0);
#endif

    U1IR = U1IE_INTERRUPT_SOF; // Clear the interrupt by writing a '1' to the flag.

//    if (delaySOFCount > 0) {
//        delaySOFCount--;
//        return;
//    }

    // decrease interval timer
    USBHost_DecreaseInterval();

    usbBusInfo.flags.bfControlTransfersDone     = 0;
    usbBusInfo.flags.bfInterruptTransfersDone   = 0;
    usbBusInfo.flags.bfIsochronousTransfersDone = 0;
    usbBusInfo.flags.bfBulkTransfersDone        = 0;
    //usbBusInfo.dBytesSentInFrame                = 0;
    usbBusInfo.lastBulkTransaction              = 0;

    USB_FindNextToken();
}

/****************************************************************************/
void USB_HostInterrupt_Error( void )
{
#if defined (DEBUG_ENABLE)
    DEBUG_PutString("#E:");
    DEBUG_PutHexU8( U1EIR );
#endif
    USB_ENDPOINT_INFO *pEndpointInfo = _USBTrans_GetEndpointByCurrentTransferType();

    // The previous token has finished, so clear the way for writing a new one.
    usbBusInfo.flags.bfTokenAlreadyWritten = 0;

    // If we are doing isochronous transfers, ignore the error.
#ifdef USB_SUPPORT_ISOCHRONOUS_TRANSFERS
    if (pEndpointInfo->bmAttributes.bfTransferType == USB_TRANSFER_TYPE_ISOCHRONOUS)
    {
//       pEndpointInfo->status.bfTransferSuccessful = 1;
//       _USB_SetNextTransferState(pEndpointInfo);
    }
    else
#endif
    {
        // Increment the error count.
        pEndpointInfo->status.bfErrorCount++;

        if (pEndpointInfo->status.bfErrorCount >= USB_TRANSACTION_RETRY_ATTEMPTS)
        {
            // We have too many errors.

            // Check U1EIR for the appropriate error codes to return
            if (U1EIRbits.BTSEF)
                pEndpointInfo->bErrorCode = USB_ENDPOINT_ERROR_BIT_STUFF;
            if (U1EIRbits.DMAEF)
                pEndpointInfo->bErrorCode = USB_ENDPOINT_ERROR_DMA;
            if (U1EIRbits.BTOEF)
                pEndpointInfo->bErrorCode = USB_ENDPOINT_ERROR_TIMEOUT;
            if (U1EIRbits.DFN8EF)
                pEndpointInfo->bErrorCode = USB_ENDPOINT_ERROR_DATA_FIELD;
            if (U1EIRbits.CRC16EF)
                pEndpointInfo->bErrorCode = USB_ENDPOINT_ERROR_CRC16;
            if (U1EIRbits.EOFEF)
                pEndpointInfo->bErrorCode = USB_ENDPOINT_ERROR_END_OF_FRAME;
            if (U1EIRbits.PIDEF)
                pEndpointInfo->bErrorCode = USB_ENDPOINT_ERROR_PID_CHECK;
#if defined(__PIC32__)
            if (U1EIRbits.BMXEF)
                pEndpointInfo->bErrorCode = USB_ENDPOINT_ERROR_BMX;
#endif

            pEndpointInfo->status.bfError    = 1;

            _USB_SetTransferErrorState( pEndpointInfo );
        }
    }

    U1EIR = 0xFF;   // Clear the interrupts by writing '1' to the flags.
    U1IR = U1IE_INTERRUPT_ERROR; // Clear the interrupt by writing a '1' to the flag.
}

/*************************************************************************
 * EOF usb_host_trans.c
 */

