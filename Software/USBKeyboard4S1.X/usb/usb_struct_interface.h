/** @file usb_struct_interface.h
 *
 *  @author Sasaji
 *  @date 2023/04/12
 *
 *  @brief Treat a list of interfaces and endpoints
 *  @note These function was moved from usb_host.c by Sasaji
*/

#ifndef USB_STRUCT_INTERFACE_H
#define	USB_STRUCT_INTERFACE_H

#include "common.h"
#include "usb_common.h"
#include "usb_ch9.h"

// *****************************************************************************
/* Useful Data Structure
 */
typedef union _USB_ENDPOINT_DATA
{
    SETUP_PKT                   setup;
    USB_STRING_DESCRIPTOR       d;
    USB_DEVICE_DESCRIPTOR       dd;
    USB_CONFIGURATION_DESCRIPTOR cd;
    USB_INTERFACE_DESCRIPTOR    id;
    USB_ENDPOINT_DESCRIPTOR     ed;
    uint8_t                     b[8];
    uint64_t                    l;
} USB_ENDPOINT_DATA;

// *****************************************************************************
/* Endpoint Information Node

This structure contains all the needed information about an endpoint.  Multiple
endpoints form a linked list.
*/
typedef struct _USB_ENDPOINT_INFO
{
    struct _USB_ENDPOINT_INFO   *next;                  // Pointer to the next node in the list.

    volatile union
    {
        struct
        {
            uint16_t        bfErrorCount            : 5;    // Not used for isochronous.
            uint16_t        bfStalled               : 1;    // Received a STALL.  Requires host interaction to clear.
            uint16_t        bfError                 : 1;    // Error count excessive. Must be cleared by the application.
            uint16_t        bfUserAbort             : 1;    // User terminated transfer.

            uint16_t        bfTransferSuccessful    : 1;    // Received an ACK.
            uint16_t        bfTransferComplete      : 1;    // Transfer done, status obtained.
            uint16_t        bfUseDTS                : 1;    // Use DTS error checking.
            uint16_t        bfNextDATA01            : 1;    // The value of DTS for the next transfer.
            uint16_t        bfLastTransferNAKd      : 1;    // The last transfer attempted NAK'd.
            uint16_t        bfNAKTimeoutEnabled     : 1;    // Endpoint will time out if too many NAKs are received.
            uint16_t        bfIntervalCountIsZero   : 1;    // Current interval count is zero
        };
        uint16_t            val;
    }                       status;
    uint16_t                wInterval;                      // Polling interval for interrupt and isochronous endpoints.
    volatile uint16_t       wIntervalCount;                 // Current interval count.
    uint16_t                wMaxPacketSize;                 // Endpoint packet size.
    uint32_t                dataCountMax;                   // Amount of data to transfer during the transfer. Not used for isochronous transfers.
    uint16_t                dataCountMaxSETUP;              // Amount of data in the SETUP packet (if applicable).
    volatile uint32_t       dataCount;                      // Count of bytes transferred.
    SETUP_PKT              *pUserDataSETUP;                 // Pointer to data for the SETUP packet (if applicable).
    uint8_t                *pUserData;                      // Pointer to data for the transfer.
    volatile uint8_t        transferState;                  // State of endpoint tranfer.
    uint8_t                 clientDriver;                   // Client driver index for events
    uint8_t                 bEndpointAddress;               // Endpoint address
    TRANSFER_ATTRIBUTES    bmAttributes;                    // Endpoint attributes, including transfer type.
    volatile uint8_t        bErrorCode;                     // If bfError is set, this indicates the reason
    volatile uint16_t       countNAKs;                      // Count of NAK's of current transaction.
    uint16_t                timeoutNAKs;                    // Count of NAK's for a timeout, if bfNAKTimeoutEnabled.
#ifdef DEBUG_ENABLE
    uint16_t                debugInfo;                      // for debug
#endif
} USB_ENDPOINT_INFO;


// *****************************************************************************
/* Interface Setting Information Structure

This structure contains information about one interface.
*/
typedef struct _USB_INTERFACE_SETTING_INFO
{
    struct _USB_INTERFACE_SETTING_INFO *next;    // Pointer to the next node in the list.

    uint8_t              interfaceAltSetting; // Alternate Interface setting
    USB_ENDPOINT_INFO   *pEndpointList;       // List of endpoints associated with this interface setting

} USB_INTERFACE_SETTING_INFO;


// *****************************************************************************
/* Interface Information Structure

This structure contains information about one interface.
*/
typedef struct _USB_INTERFACE_INFO
{
    struct _USB_INTERFACE_INFO  *next;        // Pointer to the next node in the list.

    USB_INTERFACE_SETTING_INFO  *pInterfaceSettings; // Pointer to the list of alternate settings.
    USB_INTERFACE_SETTING_INFO  *pCurrentSetting;    // Current Alternate Interface setting
    uint8_t                      interface;          // Interface number
    uint8_t                      clientDriver;       // Index into client driver table for this Interface

} USB_INTERFACE_INFO;


// *****************************************************************************
// Prototypes

void USB_InterfaceList_Clear( USB_INTERFACE_INFO **pInterfaceList );
USB_ENDPOINT_INFO *USB_InterfaceList_FindEndpointEx( USB_INTERFACE_INFO *pInterfaceList, uint8_t interface, uint8_t setting, uint8_t endpoint );
USB_ENDPOINT_INFO *USB_InterfaceList_FindEndpoint( USB_INTERFACE_INFO *pInterfaceList, uint8_t endpoint );
void USB_InterfaceList_SetData0( USB_INTERFACE_INFO *pInterfaceList );
void USB_InterfaceList_DecreaseInterval( USB_INTERFACE_INFO *pInterfaceList );
uint8_t USB_InterfaceList_CheckInterface( USB_INTERFACE_INFO *pInterfaceList, uint16_t wIndex, uint16_t wValue );

#endif	/* USB_STRUCT_INTERFACE_H */

