// DOM-IGNORE-BEGIN
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
//DOM-IGNORE-END
/**
 * @note Modified by Sasaji at 2018/02/10
 * @note These function was moved from usb_host.c by Sasaji
 */

#include "common.h"
#include "usb_config.h"

#include "usb_struct_queue.h"
#include "usb_common.h"
#include "usb_host.h"
#include "usb_host_local.h"
#include "uart.h"
#include <string.h>

// *****************************************************************************
/* Event Queue

This structure defines the queue of USB events that can be generated by the
ISR that need to be synchronized to the USB event tasks loop (see
USB_EVENT_DATA, above).  See "struct_queue.h" for usage and operations.
*/
#if defined( USB_ENABLE_TRANSFER_EVENT )
#ifndef USB_EVENT_QUEUE_DEPTH
    #define USB_EVENT_QUEUE_DEPTH   4       // Default depth of 4 events
#endif

typedef struct _usb_event_queue
{
    int             head;
    int             tail;
    int             count;
    USB_EVENT_DATA  buffer[USB_EVENT_QUEUE_DEPTH];

} USB_EVENT_QUEUE;

static USB_EVENT_QUEUE              usbEventQueue;                           // Queue of USB events used to synchronize ISR to main tasks loop.

/****************************************************************************/
void StructEventQueueInit(void)
{
    StructQueueInit(&usbEventQueue, USB_EVENT_QUEUE_DEPTH);
}

/****************************************************************************/
void StructEventQueueAdd(
    USB_EVENT               event,
    USB_DEVICE_INFO        *deviceInfo,         // Device Address
    USB_ENDPOINT_INFO      *endpointInfo,       // Endpoint information
    uint32_t                dataCount,          // Count of bytes transferred.
    uint8_t                *pUserData,          // Pointer to transfer data.
    uint8_t                 bEndpointAddress,   // Transfer endpoint.
    uint8_t                 bErrorCode,         // Transfer error code.
    TRANSFER_ATTRIBUTES     bmAttributes,       // INTERNAL USE ONLY - Endpoint transfer attributes.
    uint8_t                 clientDriver        // INTERNAL USE ONLY - Client driver index for sending the event.
)
{
    if (StructQueueIsNotFull(&usbEventQueue, USB_EVENT_QUEUE_DEPTH))
    {
        USB_EVENT_DATA *data;

        data = StructQueueAdd(&usbEventQueue, USB_EVENT_QUEUE_DEPTH);
        data->event = event;
        data->deviceInfo                    = (USB_DEVICE_INFO *)deviceInfo;
        data->TransferData.dataCount        = dataCount;
        data->TransferData.pUserData        = pUserData;
        data->TransferData.bErrorCode       = bErrorCode;
        data->TransferData.bEndpointAddress = bEndpointAddress;
        data->TransferData.bmAttributes.val = bmAttributes.val;
        data->TransferData.clientDriver     = clientDriver;
    }
    else
    {
        endpointInfo->bmAttributes.val = USB_EVENT_QUEUE_FULL;
    }
}

/****************************************************************************/
void StructEventQueueAdd_Success(USB_DEVICE_INFO *deviceInfo, USB_ENDPOINT_INFO *endpointInfo)
{
    StructEventQueueAdd(
        EVENT_TRANSFER,
        deviceInfo,
        endpointInfo,
        endpointInfo->dataCount,
        endpointInfo->pUserData,
        endpointInfo->bEndpointAddress,
        USB_SUCCESS,
        endpointInfo->bmAttributes,
        endpointInfo->clientDriver
    );
}

/****************************************************************************/
void StructEventQueueAdd_Error(USB_DEVICE_INFO *deviceInfo, USB_ENDPOINT_INFO *endpointInfo)
{
    StructEventQueueAdd(
        EVENT_BUS_ERROR,
        deviceInfo,
        endpointInfo,
        0,
        NULL,
        endpointInfo->bEndpointAddress,
        endpointInfo->bErrorCode,
        endpointInfo->bmAttributes,
        endpointInfo->clientDriver
    );
}

/****************************************************************************/
void StructEventQueueProcess(void)
{
    USB_EVENT_DATA *item;
#if defined( __PIC32__ )
    uint32_t interrupt_mask;
#else
    #error "Cannot save interrupt status"
#endif

    while (StructQueueIsNotEmpty(&usbEventQueue, USB_EVENT_QUEUE_DEPTH))
    {
        item = StructQueuePeekTail(&usbEventQueue, USB_EVENT_QUEUE_DEPTH);

        switch(item->event)
        {
            case EVENT_TRANSFER:
            case EVENT_BUS_ERROR:
                _USB_NotifyClients( item->deviceInfo, item->event, &item->TransferData, sizeof(HOST_TRANSFER_DATA) );
                break;
            default:
                break;
        }

        // Guard against USB interrupts
        interrupt_mask = U1IE;
        U1IE = 0;

        item = StructQueueRemove(&usbEventQueue, USB_EVENT_QUEUE_DEPTH);

        // Re-enable USB interrupts
        U1IE = interrupt_mask;
    }
}
#endif /* USB_ENABLE_TRANSFER_EVENT */

// *****************************************************************************
/* Transfer Queue

 * This structure defines the queue in order to transfer data.
 * The queue are processed in the SOF interrput task.
*/
#ifndef USB_TRANSFER_QUEUE_DEPTH
    #define USB_TRANSFER_QUEUE_DEPTH   4       // Default depth of 4 events
#endif

typedef struct _usb_transfer_control_queue
{
    int                head;
    int                tail;
    int                count;
    USB_TRANSFER_DATA  buffer[USB_TRANSFER_QUEUE_DEPTH];

} USB_TRANSFER_CONTROL_QUEUE;

static USB_TRANSFER_CONTROL_QUEUE usbTransferControlQueue;

/****************************************************************************/
void StructTransferControlQueueInit(void)
{
    StructQueueInit(&usbTransferControlQueue, USB_TRANSFER_QUEUE_DEPTH);
}

/****************************************************************************/
void StructTransferControlQueueAdd(
    USB_DEVICE_INFO             *deviceInfo,        // Device information
    USB_ENDPOINT_INFO           *endpointInfo       // Endpoint information
)
{
    if (StructQueueIsNotFull(&usbTransferControlQueue, USB_TRANSFER_QUEUE_DEPTH))
    {
        USB_TRANSFER_DATA *data;

        data = StructQueueAdd(&usbTransferControlQueue, USB_TRANSFER_QUEUE_DEPTH);
        data->deviceInfo        = (USB_DEVICE_INFO *)deviceInfo;
        data->endpointInfo      = endpointInfo;
    }
#ifdef DEBUG_ENABLE
        DEBUG_PutStringHexU8("CQAdd:", usbTransferControlQueue.count );
#endif
}

/****************************************************************************/
USB_TRANSFER_DATA *StructTransferControlQueueGet(void)
{
    USB_TRANSFER_DATA *item = NULL;

    if (StructQueueIsNotEmpty(&usbTransferControlQueue, USB_TRANSFER_QUEUE_DEPTH))
    {
        item = StructQueueRemove(&usbTransferControlQueue, USB_TRANSFER_QUEUE_DEPTH);
#ifdef DEBUG_ENABLE
        DEBUG_PutStringHexU8("CQGet:", usbTransferControlQueue.count );
#endif
    }
    return item;
}

// *****************************************************************************
typedef struct _usb_transfer_interrupt_queue
{
    int                valid;
    USB_TRANSFER_DATA  buffer;

} USB_TRANSFER_INTERRUPT_QUEUE;

static USB_TRANSFER_INTERRUPT_QUEUE usbTransferInterruptQueue[USB_TRANSFER_QUEUE_DEPTH];

/****************************************************************************/
void StructTransferInterruptQueueInit(void)
{
    memset(&usbTransferInterruptQueue, 0, sizeof(USB_TRANSFER_INTERRUPT_QUEUE));
}

/****************************************************************************/
void StructTransferInterruptQueueAdd(
    USB_DEVICE_INFO             *deviceInfo,        // Device information
    USB_ENDPOINT_INFO           *endpointInfo       // Endpoint information
)
{
    for(int i=0; i<USB_TRANSFER_QUEUE_DEPTH; i++) {
        USB_TRANSFER_INTERRUPT_QUEUE *p = &usbTransferInterruptQueue[i];
        if (!p->valid) {
            p->buffer.deviceInfo = (USB_DEVICE_INFO *)deviceInfo;
            p->buffer.endpointInfo = endpointInfo;
            p->valid = 1;
            break;
        }
    }
}

/****************************************************************************/
USB_TRANSFER_DATA *StructTransferInterruptQueueGet(void)
{
    USB_TRANSFER_DATA *item = NULL;

    for(int i=0; i<USB_TRANSFER_QUEUE_DEPTH; i++) {
        USB_TRANSFER_INTERRUPT_QUEUE *p = &usbTransferInterruptQueue[i];
        if (!p->valid) continue;
        if (p->buffer.endpointInfo->status.bfIntervalCountIsZero) {
            item = &p->buffer;
            p->valid = 0;
            break;
        }
    }
    return item;
}

/*************************************************************************
 * EOF usb_struct_queue.c
 */

