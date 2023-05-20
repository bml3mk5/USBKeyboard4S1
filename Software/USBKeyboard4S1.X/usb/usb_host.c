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
 *
 * @attention Bulk and Isochronous transfer are not maintained.
 */

#include "../common.h"
#include "usb_config.h"
#include <stdlib.h>
#include <string.h>
#include "usb.h"
#include "usb_host_local.h"
#include "usb_hal_local.h"
#include "usb_host_trans.h"
#include "usb_host_hub.h"
#include "usb_struct_config_list.h"
#include "usb_struct_interface.h"

#include "../uart.h"

#define USB_HUB_SUPPORT_INCLUDED 1

#if defined( USB_ENABLE_TRANSFER_EVENT )
    #include "usb_struct_queue.h"
#endif

// *****************************************************************************
// Low Level Functionality Configurations.

// If the TPL includes an entry specifying a VID of 0xFFFF and a PID of 0xFFFF,
// the specified client driver will be used for any device that attaches.  This
// can be useful for debugging or for providing generic charging functionality.
#define ALLOW_GLOBAL_VID_AND_PID

// This definition allow Bulk transfers to take all of the remaining bandwidth
// of a frame.
#define ALLOW_MULTIPLE_BULK_TRANSACTIONS_PER_FRAME

// If this is defined, then we will repeat a NAK'd request in the same frame.
// Otherwise, we will wait until the next frame to repeat the request.  Some
// mass storage devices require the host to wait until the next frame to
// repeat the request.
//#define ALLOW_MULTIPLE_NAKS_PER_FRAME

//#define USE_MANUAL_DETACH_DETECT

#define USB_EP0DATA_DEFAULT_SIZE            64
#define USB_DEVICE_DESCRIPTOR_DEFAULT_SIZE  18

//******************************************************************************
//******************************************************************************
// Section: Host Global Variables
//******************************************************************************
//******************************************************************************
typedef struct ST_USB_HOST_TIMER {
    uint16_t                    numTimerInterrupts;                         // The number of milliseconds elapsed during the current waiting period.
    USB_TIMER_HANDLER           handler;
} USB_HOST_TIMER;

static USB_HOST_TIMER           usbHostTimer;

// These should all be moved into the USB_HOST_INFO structure.
static uint8_t                  numCommandTries;                            // The number of times the current command has been tried.
static uint8_t                  numEnumerationTries;                        // The number of times enumeration has been attempted on the attached device.
static volatile uint16_t        usbHostState;                               // State machine state of the attached device.
volatile uint16_t               usbOverrideHostState;                       // Next state machine state, when set by interrupt processing.

#ifdef ENABLE_STATE_TRACE   // Debug trace support
    static uint16_t prevHostState;
#endif
    
static USB_HOST_INFO            usbHostInfo;                                // A collection of information about the attached device.
#ifdef USE_USB_VBUS_POWER
static USB_ROOT_HUB_INFO        usbRootHubInfo;                             // Information about a specific port.
#endif

#if defined(USB_ENABLE_1MS_EVENT) && defined(USB_HOST_APP_DATA_EVENT_HANDLER)
static volatile uint16_t msec_count = 0;                                    // The current millisecond count.
#endif

// index 0 is used in before decide address
// index 3 is used in storing a device information temporality
#define USB_DEVICE_MAX_INFOS    4
static USB_DEVICE_INFO          usbDeviceInfos[USB_DEVICE_MAX_INFOS];


// *****************************************************************************
// *****************************************************************************
// Section: Application Callable Functions
// *****************************************************************************
// *****************************************************************************

/****************************************************************************/
static void _USBHost_DeviceConfigureingError( USB_HOST_INFO *infoHost, uint8_t hubAddress, uint8_t deviceAddress, uint8_t errorCode )
{
    if (hubAddress == 0) {
        // root device
        // wait detach the device
        _USB_SetErrorCode( *infoHost, errorCode );
        _USB_SetHoldState();
    } else {
        // the device on the root hub
        // detach the device
        USBHost_DetachDeviceOnHUB(hubAddress, deviceAddress);
        _USB_SetRunningState();
    }
}

/****************************************************************************/
static bool USBClientDriver_Initialize( uint8_t address, int num )
{
    return usbClientDrvTable[num].Initialize(address, usbClientDrvTable[num].flags, num);
}

/****************************************************************************/
static bool USBClientDriver_EventHandler( uint8_t num, uint8_t address, USB_EVENT event, void *data, uint32_t size )
{
    return usbClientDrvTable[num].EventHandler(address, event, data, size);
}

/****************************************************************************/
static void USBClientDriver_DataEventHandler( uint8_t num, uint8_t address, USB_EVENT event, void *data, uint32_t size )
{
#ifdef USE_CLIENT_DRIVER_DATA_EVENT_HANDLER
    if (usbClientDrvTable[num].DataEventHandler != NULL)
    {
        usbClientDrvTable[num].DataEventHandler(address, event, data, size);
    }
#endif
}

/****************************************************************************/
void USBHostDeviceInfos_Initialize(void)
{
    memset(&usbDeviceInfos, 0, sizeof(usbDeviceInfos));
    usbHostInfo.pCurrentDeviceInfo = &usbDeviceInfos[0];
}

/****************************************************************************/
void USBHostDeviceInfos_Clear( uint8_t deviceAddress )
{
    USB_DEVICE_INFO *p = &usbDeviceInfos[deviceAddress];

    USB_FREE_AND_CLEAR(p->deviceDescriptor);
    USBStructConfigList_Clear(&p->pConfigurationDescriptorList);
    USB_InterfaceList_Clear(&p->pInterfaceList);
    
    memset(p, 0, sizeof(USB_DEVICE_INFO));
}

/****************************************************************************/
void USBHostDeviceInfos_ClearAll(void)
{
    for(int i = 0; i < USB_DEVICE_MAX_INFOS; i++) {
        USB_DEVICE_INFO *p = &usbDeviceInfos[i];

        USB_FREE_AND_CLEAR(p->deviceDescriptor);
        USBStructConfigList_Clear(&p->pConfigurationDescriptorList);
        USB_InterfaceList_Clear(&p->pInterfaceList);
    }
    USBHostDeviceInfos_Initialize();
}

/****************************************************************************/
uint8_t USBHostDeviceInfos_FindAddress(void)
{
    uint8_t address = 0;
    for(int i = 1; i < USB_DEVICE_MAX_INFOS; i++) {
        if (usbDeviceInfos[i].deviceAddress == 0) {
            address = i;
            break;
        }
    }
    return address;
}

/****************************************************************************/
void USBHostDeviceInfos_DetachAll(void)
{
    for(int i = USB_DEVICE_MAX_INFOS - 1; i > 0; i--) {
        if (usbDeviceInfos[i].deviceAddress > 0) {
#ifdef USE_USB_VBUS_POWER
            USB_VBUS_POWER_EVENT_DATA   powerRequest;

            powerRequest.port = 0;  // Currently was have only one port.
            powerRequest.current = usbDeviceInfo.currentConfigurationPower;

            USB_HOST_APP_EVENT_HANDLER( usbDeviceInfo.deviceAddress,
                                        EVENT_VBUS_RELEASE_POWER,
                                        &powerRequest,
                                        sizeof(USB_VBUS_POWER_EVENT_DATA)
                                      );
#endif            
            _USB_NotifyClients( &usbDeviceInfos[i],
                EVENT_DETACH,
                &usbDeviceInfos[i].deviceAddress,
                sizeof(uint8_t)
            );
        }
    }
}

/****************************************************************************/
void *USBHost_GetDeviceInfo(uint8_t address)
{
    USB_DEVICE_INFO *match = NULL;
    for(int i = 1; i < USB_DEVICE_MAX_INFOS; i++) {
        if (usbDeviceInfos[i].deviceAddress == address) {
            match = &usbDeviceInfos[i];
            break;
        }
    }
    return match;
}

/***************************************************************************/
void USBHost_DecreaseInterval( void )
{
    for(int i=1; i<USB_DEVICE_MAX_INFOS; i++) {
        // Decrease interval count
        USB_DEVICE_INFO *p = &usbDeviceInfos[i];
        USB_InterfaceList_DecreaseInterval(p->pInterfaceList);
    }
}

/****************************************************************************/
static void _USBHost_ClearTimer()
{
    usbHostTimer.numTimerInterrupts = 0;
    usbHostTimer.handler = NULL;
    U1OTGIECLR = U1OTGIE_INTERRUPT_T1MSECIF;
    U1OTGIR = U1OTGIE_INTERRUPT_T1MSECIF;
}

/****************************************************************************/
void USBHost_StartTimer(uint8_t ms, USB_TIMER_HANDLER handler)
{
    usbHostTimer.numTimerInterrupts = ms;
    usbHostTimer.handler = handler;
    U1OTGIR                 = U1OTGIE_INTERRUPT_T1MSECIF; // The interrupt is cleared by writing a '1' to the flag.
    U1OTGIESET              = U1OTGIE_INTERRUPT_T1MSECIF;
}

/****************************************************************************/
void USBHost_SetError( uint8_t errorCode )
{
    _USB_SetErrorCode( usbHostInfo, errorCode );
    _USB_SetHoldState();
}

/****************************************************************************
  Function:
    uint8_t USBHostClearEndpointErrors( )

  Summary:
    This function clears an endpoint's internal error condition.

  Description:
    This function is called to clear the internal error condition of a device's
    endpoint.  It should be called after the application has dealt with the
    error condition on the device.  This routine clears internal status only;
    it does not interact with the device.

  Precondition:
    None

  Parameters:
    uint8_t deviceAddress  - Address of device
    uint8_t endpoint       - Endpoint to clear error condition

  Return Values:
    USB_SUCCESS             - Errors cleared
    USB_UNKNOWN_DEVICE      - Device not found
    USB_ENDPOINT_NOT_FOUND  - Specified endpoint not found

  Remarks:
    None
  ***************************************************************************/

uint8_t USBHostClearEndpointErrors( USB_DEVICE_INFO *deviceInfo, USB_ENDPOINT_INFO *endpointInfo )
{
//    USB_ENDPOINT_INFO *endpointInfo;

    // Find the required device
    if (!deviceInfo)
    {
        return USB_UNKNOWN_DEVICE;
    }

//    endpointInfo = _USB_FindEndpoint( deviceInfo, endpoint );
    if (!endpointInfo) {
        return USB_ENDPOINT_NOT_FOUND;
    }

    endpointInfo->status.bfStalled    = 0;
    endpointInfo->status.bfError      = 0;

    return USB_SUCCESS;
}

/****************************************************************************
  Function:
    bool    USBHostDeviceSpecificClientDriver( uint8_t deviceAddress )

  Summary:
    This function indicates if the specified device has explicit client
    driver support specified in the TPL.

  Description:
    This function indicates if the specified device has explicit client
    driver support specified in the TPL.  It is used in client drivers'
    USB_CLIENT_INIT routines to indicate that the client driver should be
    used even though the class, subclass, and protocol values may not match
    those normally required by the class.  For example, some printing devices
    do not fulfill all of the requirements of the printer class, so their
    class, subclass, and protocol fields indicate a custom driver rather than
    the printer class.  But the printer class driver can still be used, with
    minor limitations.

  Precondition:
    None

  Parameters:
    uint8_t deviceAddress  - Address of device

  Return Values:
    true    - This device is listed in the TPL by VID andPID, and has explicit
                client driver support.
    false   - This device is not listed in the TPL by VID and PID.

  Remarks:
    This function is used so client drivers can allow certain
    devices to enumerate.  For example, some printer devices indicate a custom
    class rather than the printer class, even though the device has only minor
    limitations from the full printer class.   The printer client driver will
    fail to initialize the device if it does not indicate printer class support
    in its interface descriptor.  The printer client driver could allow any
    device with an interface that matches the printer class endpoint
    configuration, but both printer and mass storage devices utilize one bulk
    IN and one bulk OUT endpoint.  So a mass storage device would be
    erroneously initialized as a printer device.  This function allows a
    client driver to know that the client driver support was specified
    explicitly in the TPL, so for this particular device only, the class,
    subclass, and protocol fields can be safely ignored.
  ***************************************************************************/

bool    USBHostDeviceSpecificClientDriver( uint8_t deviceAddress )
{
    return usbDeviceInfos[deviceAddress].flags.bfUseDeviceClientDriver;
}


/****************************************************************************
  Function:
    uint8_t USBHostDeviceStatus( )

  Summary:
    This function returns the current status of a device.

  Description:
    This function returns the current status of a device.  If the device is
    in a holding state due to an error, the error is returned.

  Precondition:
    None

  Parameters:
    uint8_t deviceAddress  - return attached device address

  Return Values:
    USB_DEVICE_ATTACHED                 - Device is attached and running
    USB_DEVICE_DETACHED                 - No device is attached
    USB_DEVICE_ENUMERATING              - Device is enumerating
    USB_HOLDING_OUT_OF_MEMORY           - Not enough heap space available
    USB_HOLDING_UNSUPPORTED_DEVICE      - Invalid configuration or
                                            unsupported class
    USB_HOLDING_UNSUPPORTED_HUB         - Hubs are not supported
    USB_HOLDING_INVALID_CONFIGURATION   - Invalid configuration requested
    USB_HOLDING_PROCESSING_CAPACITY     - Processing requirement excessive
    USB_HOLDING_POWER_REQUIREMENT       - Power requirement excessive
    USB_HOLDING_CLIENT_INIT_ERROR       - Client driver failed to initialize
    USB_DEVICE_SUSPENDED                - Device is suspended
    Other                               - Device is holding in an error
                                            state. The return value
                                            indicates the error.

  Remarks:
    None
  ***************************************************************************/

uint8_t USBHostDeviceStatus( uint8_t *deviceAddress )
{
    if (deviceAddress) {
        *deviceAddress = usbHostInfo.pCurrentDeviceInfo->deviceAddress;
    }
    
    if ((usbHostState & STATE_MASK) == STATE_DETACHED)
    {
        return USB_DEVICE_DETACHED;
    }

    if ((usbHostState & STATE_MASK) == STATE_RUNNING)
    {
#ifdef USE_USB_DEVICE_SUSPEND
        if ((usbHostState & SUBSTATE_MASK) == SUBSTATE_SUSPEND_AND_RESUME)
        {
            return USB_DEVICE_SUSPENDED;
        }
        else
#endif
        {
            return USB_DEVICE_ATTACHED;
        }
    }

    if ((usbHostState & STATE_MASK) == STATE_HOLDING)
    {
        return usbHostInfo.errorCode;
    }

    
    if ((usbHostState > STATE_ATTACHED) && 
        (usbHostState < STATE_RUNNING)
       )
    {
        return USB_DEVICE_ENUMERATING;
    }
    
    return USB_HOLDING_UNSUPPORTED_DEVICE;
}

/****************************************************************************
  Function:
    void USBHost(  )

  Summary:
    Constructor

  Description:
    Constructor

  Precondition:
    None

  Parameters:
    None

  Return Values:
    None

  Remarks:
    None
  ***************************************************************************/

void USBHost( void )
{
    memset(&usbHostInfo, 0, sizeof(usbHostInfo));
    
    USBHostInit(0);

    USBHostDeviceInfos_Initialize();
        
    USBHostHUB();
}

/****************************************************************************
  Function:
    bool USBHostInit(  unsigned long flags  )

  Summary:
    This function initializes the variables of the USB host stack.

  Description:
    This function initializes the variables of the USB host stack.  It does
    not initialize the hardware.  The peripheral itself is initialized in one
    of the state machine states.  Therefore, USBHostTasks() should be called
    soon after this function.

  Precondition:
    None

  Parameters:
    flags - reserved

  Return Values:
    true  - Initialization successful
    false - Could not allocate memory.

  Remarks:
    If the endpoint list is empty, an entry is created in the endpoint list
    for EP0.  If the list is not empty, free all allocated memory other than
    the EP0 node.  This allows the routine to be called multiple times by the
    application.
  ***************************************************************************/

bool USBHostInit(  unsigned long flags  )
{
    // Allocate space for Endpoint 0.  We will initialize it in the state machine,
    // so we can reinitialize when another device connects.  If the Endpoint 0
    // node already exists, free all other allocated memory.
    if (usbHostInfo.pEndpoint0 == NULL)
    {
        if ((usbHostInfo.pEndpoint0 = (USB_ENDPOINT_INFO *)USB_MALLOC( sizeof(USB_ENDPOINT_INFO) )) == NULL)
        {
#if defined (DEBUG_ENABLE)
            DEBUG_PutString( "HOST: Cannot allocate for endpoint 0.\r\n" );
#endif
            return false;
        }
        memset(usbHostInfo.pEndpoint0, 0, sizeof(USB_ENDPOINT_INFO));
    }
    else
    {
        _USB_FreeMemory();
    }

    // Initialize other variables.
//    pCurrentEndpoint                        = usbHostInfo.pEndpoint0;
    usbHostState                            = STATE_DETACHED;
    usbOverrideHostState                    = NO_STATE;
//    usbDeviceInfo.deviceAddressAndSpeed     = 0;
//    usbDeviceInfo.deviceAddress             = 0;
#ifdef USE_USB_VBUS_POWER
    usbRootHubInfo.flags.bPowerGoodPort0    = 1;
#endif
//    delaySOFCount = 0;

    // Initialize device address
    USBHostDeviceInfos_ClearAll();
    
    // Initialize event queue
#if defined( USB_ENABLE_TRANSFER_EVENT )
    StructEventQueueInit();
#endif
    StructTransferControlQueueInit();
    StructTransferInterruptQueueInit();

    return true;
}

#ifdef USB_SUPPORT_ISOCHRONOUS_TRANSFERS
/****************************************************************************
  Function:
    bool USBHostIsochronousBuffersCreate( ISOCHRONOUS_DATA * isocData, 
            uint8_t numberOfBuffers, uint16_t bufferSize )
    
  Description:
    This function initializes the isochronous data buffer information and
    allocates memory for each buffer.  This function will not allocate memory
    if the buffer pointer is not NULL.

  Precondition:
    None

  Parameters:
    None

  Return Values:
    true    - All buffers are allocated successfully.
    false   - Not enough heap space to allocate all buffers - adjust the 
                project to provide more heap space.

  Remarks:
    This function is available only if USB_SUPPORT_ISOCHRONOUS_TRANSFERS
    is defined in usb_config.h.
***************************************************************************/

bool USBHostIsochronousBuffersCreate( ISOCHRONOUS_DATA * isocData, uint8_t numberOfBuffers, uint16_t bufferSize )
{
    uint8_t i;
    uint8_t j;

    USBHostIsochronousBuffersReset( isocData, numberOfBuffers );
    for (i=0; i<numberOfBuffers; i++)
    {
        if (isocData->buffers[i].pBuffer == NULL)
        {
            isocData->buffers[i].pBuffer = USB_MALLOC( bufferSize );
            if (isocData->buffers[i].pBuffer == NULL)
            {
#if defined (DEBUG_ENABLE)
                DEBUG_PutString( "HOST:  Not enough memory for isoc buffers.\r\n" );
#endif

                // Release all previous buffers.
                for (j=0; j<i; j++)
                {
                    USB_FREE_AND_CLEAR( isocData->buffers[j].pBuffer );
                    isocData->buffers[j].pBuffer = NULL;
                }
                return false;
            }
        }
    }
    return true;
}
#endif

#ifdef USB_SUPPORT_ISOCHRONOUS_TRANSFERS
/****************************************************************************
  Function:
    void USBHostIsochronousBuffersDestroy( ISOCHRONOUS_DATA * isocData, uint8_t numberOfBuffers )
    
  Description:
    This function releases all of the memory allocated for the isochronous
    data buffers.  It also resets all other information about the buffers.

  Precondition:
    None

  Parameters:
    None

  Returns:
    None

  Remarks:
    This function is available only if USB_SUPPORT_ISOCHRONOUS_TRANSFERS
    is defined in usb_config.h.
***************************************************************************/

void USBHostIsochronousBuffersDestroy( ISOCHRONOUS_DATA * isocData, uint8_t numberOfBuffers )
{
    uint8_t i;

    USBHostIsochronousBuffersReset( isocData, numberOfBuffers );
    for (i=0; i<numberOfBuffers; i++)
    {
        if (isocData->buffers[i].pBuffer != NULL)
        {
            USB_FREE_AND_CLEAR( isocData->buffers[i].pBuffer );
        }
    }
}
#endif

#ifdef USB_SUPPORT_ISOCHRONOUS_TRANSFERS
/****************************************************************************
  Function:
    void USBHostIsochronousBuffersReset( ISOCHRONOUS_DATA * isocData, uint8_t numberOfBuffers )
    
  Description:
    This function resets all the isochronous data buffers.  It does not do 
    anything with the space allocated for the buffers.

  Precondition:
    None

  Parameters:
    None

  Returns:
    None

  Remarks:
    This function is available only if USB_SUPPORT_ISOCHRONOUS_TRANSFERS
    is defined in usb_config.h.
***************************************************************************/

void USBHostIsochronousBuffersReset( ISOCHRONOUS_DATA * isocData, uint8_t numberOfBuffers )
{
    uint8_t    i;

    for (i=0; i<numberOfBuffers; i++)
    {
        isocData->buffers[i].dataLength        = 0;
        isocData->buffers[i].bfDataLengthValid = 0;
    }

    isocData->totalBuffers         = numberOfBuffers;
    isocData->currentBufferUser    = 0;
    isocData->currentBufferUSB     = 0;
    isocData->pDataUser            = NULL;
}
#endif

/****************************************************************************
  Function:
    uint8_t USBHostIssueDeviceRequestEx( )

  Summary:
    This function sends a standard device request to the attached device.
    Control Transfer (SETUP)

  Description:
    This function sends a standard device request to the attached device.
    The user must pass in the parameters of the device request.  If there is
    input or output data associated with the request, a pointer to the data
    must be provided.  The direction of the associated data (input or output)
    must also be indicated.

    This function does no special processing in regards to the request except
    for three requests.  If SET INTERFACE is sent, then DTS is reset for all
    endpoints.  If CLEAR FEATURE (ENDPOINT HALT) is sent, then DTS is reset
    for that endpoint.  If SET CONFIGURATION is sent, the request is aborted
    with a failure.  The function USBHostSetDeviceConfiguration() must be
    called to change the device configuration, since endpoint definitions may
    change.

  Precondition:
    The host state machine should be in the running state, and no reads or
    writes to EP0 should be in progress.

  Parameters:
    uint8_t deviceInfo     - Device information
    uint8_t bmRequestType  - The request type as defined by the USB
                            specification.
    uint8_t bRequest       - The request as defined by the USB specification.
    uint16_t wValue         - The value for the request as defined by the USB
                            specification.
    uint16_t wIndex         - The index for the request as defined by the USB
                            specification.
    uint16_t wLength        - The data length for the request as defined by the
                            USB specification.
    uint8_t *data          - Pointer to the data for the request.
    uint8_t dataDirection  - USB_DEVICE_REQUEST_SET or USB_DEVICE_REQUEST_GET
    uint8_t clientDriverID - Client driver to send the event to.

  Return Values:
    USB_SUCCESS                 - Request processing started
    USB_UNKNOWN_DEVICE          - Device not found
    USB_INVALID_STATE           - The host must be in a normal running state
                                    to do this request
    USB_ENDPOINT_BUSY           - A read or write is already in progress
    USB_ILLEGAL_REQUEST         - SET CONFIGURATION cannot be performed with
                                    this function.

  Remarks:
    DTS reset is done before the command is issued.
  ***************************************************************************/

uint8_t USBHostIssueDeviceRequest( USB_DEVICE_INFO *deviceInfo
    , SETUP_PKT *setupPacket
    , uint8_t *data
    , uint8_t clientDriverID
#ifdef DEBUG_ENABLE
    , uint16_t debugInfo
#endif
)
{
    if (!deviceInfo)
    {
        return USB_UNKNOWN_DEVICE;
    }

    // If we are not in a normal user running state, we cannot do this.
    if ((usbHostState & STATE_MASK) != STATE_RUNNING)
    {
        return USB_INVALID_STATE;
    }

    // Make sure no other reads or writes on EP0 are in progress.
    if (!usbHostInfo.pEndpoint0->status.bfTransferComplete)
    {
        return USB_ENDPOINT_BUSY;
    }

    // We can't do a SET CONFIGURATION here.  Must use USBHostSetDeviceConfiguration().
    // ***** Some USB classes need to be able to do this, so we'll remove
    // the constraint.
//    if (bRequest == USB_REQUEST_SET_CONFIGURATION)
//    {
//        return USB_ILLEGAL_REQUEST;
//    }

    // If the user is doing a SET INTERFACE, we must reset DATA0 for all endpoints.
    if (setupPacket->bRequest == USB_REQUEST_SET_INTERFACE)
    {
        if (!USB_InterfaceList_CheckInterface(deviceInfo->pInterfaceList, setupPacket->wIndex, setupPacket->wValue))
        {
            return USB_ILLEGAL_REQUEST;
        }
    }

    // If the user is doing a CLEAR FEATURE(ENDPOINT_HALT), we must reset DATA0 for that endpoint.
    if ((setupPacket->bRequest == USB_REQUEST_CLEAR_FEATURE) && (setupPacket->wValue == USB_FEATURE_ENDPOINT_HALT))
    {
        switch(setupPacket->bmRequestType)
        {
            case 0x00:
            case 0x01:
            case 0x02:
                _USB_ResetDATA0( deviceInfo, (uint8_t)setupPacket->wIndex );
                break;
            default:
                break;
        }
    }

    // Set up the control packet.
    usbHostInfo.pEP0Data->setup = *setupPacket;
//    pkt->bmRequestType = bmRequestType;
//    pkt->bRequest = bRequest;
//    pkt->wValue = wValue;
//    pkt->wIndex = wIndex;
//    pkt->wLength = wLength;
//    usbHostInfo.pEP0Data.b[2] = wValue & 0xFF;
//    usbHostInfo.pEP0Data.b[3] = (wValue >> 8) & 0xFF;
//    usbHostInfo.pEP0Data.b[4] = wIndex & 0xFF;
//    usbHostInfo.pEP0Data.b[5] = (wIndex >> 8) & 0xFF;
//    usbHostInfo.pEP0Data.b[6] = wLength & 0xFF;
//    usbHostInfo.pEP0Data.b[7] = (wLength >> 8) & 0xFF;

    // Set up the client driver for the event.
    usbHostInfo.pEndpoint0->clientDriver = clientDriverID;
#ifdef DEBUG_ENABLE
    usbHostInfo.pEndpoint0->debugInfo = debugInfo;
#endif

    // Send request
    USB_InitControlReadWrite( (setupPacket->bmRequestType & USB_SETUP_DEVICE_TO_HOST) == USB_SETUP_HOST_TO_DEVICE
        , deviceInfo, usbHostInfo.pEndpoint0
        , &usbHostInfo.pEP0Data->setup, 8
        , data, setupPacket->wLength );

    return USB_SUCCESS;
}

/****************************************************************************
  Function:
    uint8_t USBHostIssueDeviceRequestEx( )

  Summary:
    This function is wrapper of  USBHostIssueDeviceRequest()

  Description:

  Precondition:

  Parameters:
    uint8_t deviceInfo     - Device information
    uint8_t bmRequestType  - The request type as defined by the USB
                            specification.
    uint8_t bRequest       - The request as defined by the USB specification.
    uint16_t wValue         - The value for the request as defined by the USB
                            specification.
    uint16_t wIndex         - The index for the request as defined by the USB
                            specification.
    uint16_t wLength        - The data length for the request as defined by the
                            USB specification.
    uint8_t *data          - Pointer to the data for the request.
    uint8_t dataDirection  - USB_DEVICE_REQUEST_SET or USB_DEVICE_REQUEST_GET
    uint8_t clientDriverID - Client driver to send the event to.

  Return Values:
    USB_SUCCESS                 - Request processing started
    USB_UNKNOWN_DEVICE          - Device not found
    USB_INVALID_STATE           - The host must be in a normal running state
                                    to do this request
    USB_ENDPOINT_BUSY           - A read or write is already in progress
    USB_ILLEGAL_REQUEST         - SET CONFIGURATION cannot be performed with
                                    this function.

  Remarks:
  ***************************************************************************/

uint8_t USBHostIssueDeviceRequestEx( USB_DEVICE_INFO *deviceInfo
    , uint8_t bmRequestType
    , uint8_t bRequest
    , uint16_t wValue
    , uint16_t wIndex
    , uint16_t wLength
    , uint8_t *data
    , uint8_t clientDriverID
#ifdef DEBUG_ENABLE
    , uint16_t debugInfo
#endif
)
{
    SETUP_PKT packet;
    packet.bmRequestType = bmRequestType;
    packet.bRequest = bRequest;
    packet.wValue = wValue;
    packet.wIndex = wIndex;
    packet.wLength = wLength;
    return USBHostIssueDeviceRequest( deviceInfo, &packet
        , data, clientDriverID
#ifdef DEBUG_ENABLE
        , debugInfo
#endif
    );
}

/****************************************************************************
  Function:
    uint8_t USBHostReadWrite( )
  Summary:
    This function initiates a read from the attached device.

  Description:
    This function initiates a read from the attached device.

    If the endpoint is isochronous, special conditions apply.  The pData and
    size parameters have slightly different meanings, since multiple buffers
    are required.  Once started, an isochronous transfer will continue with
    no upper layer intervention until USBHostTerminateTransfer() is called.
    The ISOCHRONOUS_DATA_BUFFERS structure should not be manipulated until
    the transfer is terminated.

    To clarify parameter usage and to simplify casting, use the macro
    USBHostReadIsochronous() when reading from an isochronous endpoint.

  Precondition:
    None

  Parameters:
    uint8_t deviceAddress  - Device address
    uint8_t endpoint       - Endpoint number
    uint8_t *pData         - Pointer to where to store the data. If the endpoint
                            is isochronous, this points to an
                            ISOCHRONOUS_DATA_BUFFERS structure, with multiple
                            data buffer pointers.
    uint32_t size          - Number of data bytes to read. If the endpoint is
                            isochronous, this is the number of data buffer
                            pointers pointed to by pData.

  Return Values:
    USB_SUCCESS                     - Read started successfully.
    USB_UNKNOWN_DEVICE              - Device with the specified address not found.
    USB_INVALID_STATE               - We are not in a normal running state.
    USB_ENDPOINT_ILLEGAL_TYPE       - Must use USBHostControlRead to read
                                        from a control endpoint.
    USB_ENDPOINT_ILLEGAL_DIRECTION  - Must read from an IN endpoint.
    USB_ENDPOINT_STALLED            - Endpoint is stalled.  Must be cleared
                                        by the application.
    USB_ENDPOINT_ERROR              - Endpoint has too many errors.  Must be
                                        cleared by the application.
    USB_ENDPOINT_BUSY               - A Read is already in progress.
    USB_ENDPOINT_NOT_FOUND          - Invalid endpoint.

  Remarks:
    None
  ***************************************************************************/

uint8_t USBHostReadWrite( bool is_write, USB_DEVICE_INFO *deviceInfo, USB_ENDPOINT_INFO *endpointInfo, uint8_t *pData, uint32_t size )
{
//    USB_ENDPOINT_INFO *endpointInfo;
    uint8_t epAddr = (is_write ? 0x80 : 0);

    // Find the required device
    if (!deviceInfo)
    {
        return USB_UNKNOWN_DEVICE;
    }

    // If we are not in a normal user running state, we cannot do this.
    if ((usbHostState & STATE_MASK) != STATE_RUNNING)
    {
        return USB_INVALID_STATE;
    }

//    endpointInfo = _USB_FindEndpoint( deviceInfo, endpoint );
    if (endpointInfo != NULL)
    {
        if (endpointInfo->bmAttributes.bfTransferType == USB_TRANSFER_TYPE_CONTROL)
        {
            // Must not be a control endpoint.
            return USB_ENDPOINT_ILLEGAL_TYPE;
        }

        if ((endpointInfo->bEndpointAddress & 0x80) == epAddr)
        {
            // Trying to do an IN with an OUT endpoint.
            return USB_ENDPOINT_ILLEGAL_DIRECTION;
        }

        if (endpointInfo->status.bfStalled)
        {
            // The endpoint is stalled.  It must be restarted before a write
            // can be performed.
            return USB_ENDPOINT_STALLED;
        }

        if (endpointInfo->status.bfError)
        {
            // The endpoint has errored.  The error must be cleared before a
            // write can be performed.
            return USB_ENDPOINT_ERROR;
        }

        if (!endpointInfo->status.bfTransferComplete)
        {
            // We are already processing a request for this endpoint.
            return USB_ENDPOINT_BUSY;
        }

        USB_InitReadWrite( is_write, deviceInfo, endpointInfo, pData, size );

        return USB_SUCCESS;
    }
    return USB_ENDPOINT_NOT_FOUND;   // Endpoint not found
}

#if 0
/****************************************************************************
  Function:
    uint8_t USBHostResetDevice( uint8_t deviceAddress )

  Summary:
    This function resets an attached device.

  Description:
    This function places the device back in the RESET state, to issue RESET
    signaling.  It can be called only if the state machine is not in the
    DETACHED state.

  Precondition:
    None

  Parameters:
    uint8_t deviceAddress  - Device address

  Return Values:
    USB_SUCCESS         - Success
    USB_UNKNOWN_DEVICE  - Device not found
    USB_ILLEGAL_REQUEST - Device cannot RESUME unless it is suspended

  Remarks:
    In order to do a full clean-up, the state is set back to STATE_DETACHED
    rather than a reset state.  The ATTACH interrupt will automatically be
    triggered when the module is re-enabled, and the proper reset will be
    performed.
  ***************************************************************************/
uint8_t USBHostResetDevice( uint8_t deviceAddress )
{
    // Find the required device
    if (deviceAddress != usbDeviceInfo.deviceAddress)
    {
        return USB_UNKNOWN_DEVICE;
    }

    if ((usbHostState & STATE_MASK) == STATE_DETACHED)
    {
        return USB_ILLEGAL_REQUEST;
    }

    usbHostState = STATE_DETACHED;

    return USB_SUCCESS;
}
#endif

#if 0
/****************************************************************************
  Function:
    uint8_t USBHostResumeDevice( uint8_t deviceAddress )

  Summary:
    This function issues a RESUME to the attached device.

  Description:
    This function issues a RESUME to the attached device.  It can called only
    if the state machine is in the suspend state.

  Precondition:
    None

  Parameters:
    uint8_t deviceAddress  - Device address

  Return Values:
    USB_SUCCESS         - Success
    USB_UNKNOWN_DEVICE  - Device not found
    USB_ILLEGAL_REQUEST - Device cannot RESUME unless it is suspended

  Remarks:
    None
  ***************************************************************************/

uint8_t USBHostResumeDevice( uint8_t deviceAddress )
{
    // Find the required device
    if (deviceAddress != usbDeviceInfo.deviceAddress)
    {
        return USB_UNKNOWN_DEVICE;
    }

    if (usbHostState != (STATE_RUNNING | SUBSTATE_SUSPEND_AND_RESUME | SUBSUBSTATE_SUSPEND))
    {
        return USB_ILLEGAL_REQUEST;
    }

    // Advance the state machine to issue resume signalling.
    _USB_SetNextSubSubState();

    return USB_SUCCESS;
}
#endif

#if 0
/****************************************************************************
  Function:
    uint8_t USBHostSetDeviceConfiguration( uint8_t deviceAddress, uint8_t configuration )

  Summary:
    This function changes the device's configuration.

  Description:
    This function is used by the application to change the device's
    Configuration.  This function must be used instead of
    USBHostIssueDeviceRequest(), because the endpoint definitions may change.

    To see when the reconfiguration is complete, use the USBHostDeviceStatus()
    function.  If configuration is still in progress, this function will
    return USB_DEVICE_ENUMERATING.

  Precondition:
    The host state machine should be in the running state, and no reads or
    writes should be in progress.

  Parameters:
    uint8_t deviceAddress  - Device address
    uint8_t configuration  - Index of the new configuration

  Return Values:
    USB_SUCCESS         - Process of changing the configuration was started
                            successfully.
    USB_UNKNOWN_DEVICE  - Device not found
    USB_INVALID_STATE   - This function cannot be called during enumeration
                            or while performing a device request.
    USB_BUSY            - No IN or OUT transfers may be in progress.

  Example:
    <code>
    rc = USBHostSetDeviceConfiguration( attachedDevice, configuration );
    if (rc)
    {
        // Error - cannot set configuration.
    }
    else
    {
        while (USBHostDeviceStatus( attachedDevice ) == USB_DEVICE_ENUMERATING)
        {
            USBHostTasks();
        }
    }
    if (USBHostDeviceStatus( attachedDevice ) != USB_DEVICE_ATTACHED)
    {
        // Error - cannot set configuration.
    }
    </code>

  Remarks:
    If an invalid configuration is specified, this function cannot return
    an error.  Instead, the event USB_UNSUPPORTED_DEVICE will the sent to the
    application layer and the device will be placed in a holding state with a
    USB_HOLDING_UNSUPPORTED_DEVICE error returned by USBHostDeviceStatus().
  ***************************************************************************/

uint8_t USBHostSetDeviceConfiguration( uint8_t deviceAddress, uint8_t configuration )
{
    USB_DEVICE_INFO *deviceInfo = usbHostInfo.pCurrentDeviceInfo;

    // Find the required device
    if (deviceAddress != deviceInfo->deviceAddress)
    {
        return USB_UNKNOWN_DEVICE;
    }

    // If we are not in a normal user running state, we cannot do this.
    if ((usbHostState & STATE_MASK) != STATE_RUNNING)
    {
        return USB_INVALID_STATE;
    }

    // Make sure no other reads or writes are in progress.
    if (_USB_TransferInProgress())
    {
        return USB_BUSY;
    }

    // Set the new device configuration.
    usbDeviceInfo.currentConfiguration = configuration;

    // We're going to be sending Endpoint 0 commands, so be sure the
    // client driver indicates the host driver, so we do not send events up
    // to a client driver.
    usbHostInfo.pEndpoint0->clientDriver = CLIENT_DRIVER_HOST;

    // Set the state back to configure the device.  This will destroy the
    // endpoint list and terminate any current transactions.  We already have
    // the configuration, so we can jump into the Select Configuration state.
    // If the configuration value is invalid, the state machine will error and
    // put the device into a holding state.
    usbHostState = STATE_CONFIGURING | SUBSTATE_SELECT_CONFIGURATION;

    return USB_SUCCESS;
}
#endif

#if 0
/****************************************************************************
  Function:
    uint8_t USBHostSetNAKTimeout( uint8_t deviceAddress, uint8_t endpoint, uint16_t flags,
                uint16_t timeoutCount )

  Summary:
    This function specifies NAK timeout capability.

  Description:
    This function is used to set whether or not an endpoint on a device
    should time out a transaction based on the number of NAKs received, and
    if so, how many NAKs are allowed before the timeout.

  Precondition:
    None

  Parameters:
    uint8_t deviceAddress  - Device address
    uint8_t endpoint       - Endpoint number to configure
    uint16_t flags          - Bit 0:
                            * 0 = disable NAK timeout
                            * 1 = enable NAK timeout
    uint16_t timeoutCount   - Number of NAKs allowed before a timeout

  Return Values:
    USB_SUCCESS             - NAK timeout was configured successfully.
    USB_UNKNOWN_DEVICE      - Device not found.
    USB_ENDPOINT_NOT_FOUND  - The specified endpoint was not found.

  Remarks:
    None
  ***************************************************************************/

uint8_t USBHostSetNAKTimeout( uint8_t deviceAddress, uint8_t endpoint, uint16_t flags, uint16_t timeoutCount )
{
    USB_DEVICE_INFO *deviceInfo;
    USB_ENDPOINT_INFO *ep;

    // Find the required device
    deviceInfo = USBHost_GetDeviceInfo( deviceAddress );
    if (!deviceInfo)
        return USB_UNKNOWN_DEVICE;
    }

    ep = _USB_FindEndpoint( deviceInfo, endpoint );
    if (ep)
    {
        ep->status.bfNAKTimeoutEnabled  = flags & 0x01;
        ep->timeoutNAKs                 = timeoutCount;

        return USB_SUCCESS;
    }
    return USB_ENDPOINT_NOT_FOUND;
}
#endif

/****************************************************************************
  Function:
    void USBHostShutdown( void )

  Description:
    This function turns off the USB module and frees all unnecessary memory.
    This routine can be called by the application layer to shut down all
    USB activity, which effectively detaches all devices.  The event
    EVENT_DETACH will be sent to the client drivers for the attached device,
    and the event EVENT_VBUS_RELEASE_POWER will be sent to the application
    layer.

  Precondition:
    None

  Parameters:
    None - None

  Returns:
    None

  Remarks:
    None
  ***************************************************************************/

void USBHostShutdown( void )
{
    // Shut off the power to the module first, in case we are in an
    // overcurrent situation.

    #ifdef  USB_SUPPORT_OTG
        if (!USBOTGHnpIsActive())
        {
            // If we currently have an attached device, notify the higher layers that
            // the device is being removed.
            if (usbDeviceInfo.deviceAddress)
            {
                USB_VBUS_POWER_EVENT_DATA   powerRequest;

                powerRequest.port = 0;  // Currently was have only one port.

                USB_HOST_APP_EVENT_HANDLER( usbDeviceInfo.deviceAddress, EVENT_VBUS_RELEASE_POWER,
                    &powerRequest, sizeof(USB_VBUS_POWER_EVENT_DATA) );
                _USB_NotifyClients(usbDeviceInfo.deviceAddress, EVENT_DETACH,
                    &usbDeviceInfo.deviceAddress, sizeof(uint8_t) );


            }
        }
    #else
        U1PWRC = U1PWRC_NORMAL_OPERATION | U1PWRC_DISABLED;  //MR - Turning off Module will cause unwanted Suspends in OTG

        // If we currently have an attached device, notify the higher layers that
        // the device is being removed.
        USBHostDeviceInfos_DetachAll();
    #endif

    // Free all extra allocated memory, initialize variables, and reset the
    // state machine.
    USBHostInit( 0 );
}

#ifdef USE_USB_DEVICE_SUSPEND
/****************************************************************************
  Function:
    uint8_t USBHostSuspendDevice( uint8_t deviceAddress )

  Summary:
    This function suspends a device.

  Description:
    This function put a device into an IDLE state.  It can only be called
    while the state machine is in normal running mode.  After 3ms, the
    attached device should go into SUSPEND mode.

  Precondition:
    None

  Parameters:
    uint8_t deviceAddress  - Device to suspend

  Return Values:
    USB_SUCCESS         - Success
    USB_UNKNOWN_DEVICE  - Device not found
    USB_ILLEGAL_REQUEST - Cannot suspend unless device is in normal run mode

  Remarks:
    None
  ***************************************************************************/

uint8_t USBHostSuspendDevice( uint8_t deviceAddress )
{
    // Find the required device
    if (deviceAddress != usbDeviceInfo.deviceAddress)
    {
        return USB_UNKNOWN_DEVICE;
    }

    if (usbHostState != (STATE_RUNNING | SUBSTATE_NORMAL_RUN))
    {
        return USB_ILLEGAL_REQUEST;
    }

    // Turn off SOF's, so the bus is idle.
    U1CONbits.SOFEN = 0;

    // Put the state machine in suspend mode.
    usbHostState = STATE_RUNNING | SUBSTATE_SUSPEND_AND_RESUME | SUBSUBSTATE_SUSPEND;

    return USB_SUCCESS;
}
#endif

/****************************************************************************/
static __inline__ void USBHostTask_Detached_Initialize(void)
{
    // We got here either from initialization or from the user
    // unplugging the device at any point in time.

    // Turn off the module and free up memory.
    USBHostShutdown();

#if defined (DEBUG_ENABLE)
    UART_PutString( "HOST: Detach.\r\n" );
#endif

    // Initialize Endpoint 0 attributes.
    // for Control Transfer
    usbHostInfo.pEndpoint0->next                         = NULL;
    usbHostInfo.pEndpoint0->status.val                   = 0x00;
    usbHostInfo.pEndpoint0->status.bfUseDTS              = 1;
    usbHostInfo.pEndpoint0->status.bfTransferComplete    = 1;    // Initialize to success to allow preprocessing loops.
    usbHostInfo.pEndpoint0->status.bfNAKTimeoutEnabled   = 1;    // So we can catch devices that NAK forever during enumeration
    usbHostInfo.pEndpoint0->timeoutNAKs                  = USB_NUM_CONTROL_NAKS;
    usbHostInfo.pEndpoint0->wMaxPacketSize               = 64;
    usbHostInfo.pEndpoint0->dataCount                    = 0;    // Initialize to 0 since we set bfTransferComplete.
    usbHostInfo.pEndpoint0->bEndpointAddress             = 0;
    usbHostInfo.pEndpoint0->transferState                = TSTATE_IDLE;
    usbHostInfo.pEndpoint0->bmAttributes.bfTransferType  = USB_TRANSFER_TYPE_CONTROL;
    usbHostInfo.pEndpoint0->clientDriver                 = CLIENT_DRIVER_HOST;

    // Initialize any device specific information.
    numEnumerationTries                 = USB_NUM_ENUMERATION_TRIES;
//    usbDeviceInfo.currentConfiguration  = 0; // Will be overwritten by config process or the user later
//    usbDeviceInfo.attributesOTG         = 0;
//    usbDeviceInfo.deviceAddressAndSpeed = 0;
    usbHostInfo.flags.val             = 0;
//    usbDeviceInfo.pInterfaceList        = NULL;
    USBTrans_Init();
//    usbBusInfo.flags.val                = 0;

    // Set up the hardware.
    U1IE                = 0;        // Clear and turn off interrupts.
    U1IR                = 0xFF;
    U1OTGIE             &= 0x8C;
    U1OTGIR             = 0x7D;
    U1EIE               = 0;
    U1EIR               = 0xFF;

    _USBHost_ClearTimer();
    
    // Initialize the Buffer Descriptor Table pointer.
    _USB_ClearBDT();

    // Configure the module
    U1CON           = U1CON_HOST_MODE_ENABLE | U1CON_SOF_DISABLE;                         // Turn of SOF's to cut down noise
    U1CON           = U1CON_HOST_MODE_ENABLE | U1CON_PINGPONG_RESET | U1CON_SOF_DISABLE;  // Reset the ping-pong buffers
    U1CON           = U1CON_HOST_MODE_ENABLE | U1CON_SOF_DISABLE;                         // Release the ping-pong buffers
    #ifdef  USB_SUPPORT_OTG
        U1OTGCON    = U1OTGCON_DPLUS_PULLDOWN_ENABLE | U1OTGCON_DMINUS_PULLDOWN_ENABLE | USB_OTG_ENABLE; // Pull down D+ and D-
    #else
        U1OTGCON    = U1OTGCON_DPLUS_PULLDOWN_ENABLE | U1OTGCON_DMINUS_PULLDOWN_ENABLE; // Pull down D+ and D-
    #endif

    #if defined(__PIC32__)
        U1OTGCON |= U1OTGCON_VBUS_ON;
    #endif

//  U1CNFG1             = USB_PING_PONG_MODE;
    U1ADDR              = 0;                        // Set default address and LSPDEN to 0
    U1EP0bits.LSPD      = 0;                        // Not Low-Speed
    U1SOF               = USB_SOF_THRESHOLD_64;     // Maximum EP0 packet size

    USBHostDeviceInfos_ClearAll();

    // Set the next substate.  We do this before we enable
    // interrupts in case the interrupt changes states.
    _USB_SetNextSubState();
}

/****************************************************************************/
static __inline__ void USBHostTask_Detached_WaitForPower(void)
{
    // We will wait here until the application tells us we can
    // turn on power.
#ifdef USE_USB_VBUS_POWER
    if (usbRootHubInfo.flags.bPowerGoodPort0) {
            _USB_SetNextSubState();
    }
#else
    static int wait_timer;
    switch (usbHostState & SUBSUBSTATE_MASK)
    {
        case SUBSUBSTATE_WAIT_FOR_POWER_START:
            wait_timer = 10000;
            _USB_SetNextSubSubState();
            break;
        case SUBSUBSTATE_WAIT_FOR_POWER_WAIT:
            if (wait_timer > 0) wait_timer--;
            if (wait_timer == 0)
                _USB_SetNextSubSubState();
            break;
        case SUBSUBSTATE_WAIT_FOR_POWER_DONE:
            _USB_SetNextSubState();
            break;
        default:
            // We shouldn't get here.
            break;
    }
#endif
}

/****************************************************************************/
static __inline__ void USBHostTask_Detached_TurnOnPower(void)
{
#ifdef USE_USB_VBUS_POWER
    USB_VBUS_POWER_EVENT_DATA   powerRequest;

    powerRequest.port       = 0;
    powerRequest.current    = USB_INITIAL_VBUS_CURRENT;
    if (USB_HOST_APP_EVENT_HANDLER( USB_ROOT_HUB, EVENT_VBUS_REQUEST_POWER,
            &powerRequest, sizeof(USB_VBUS_POWER_EVENT_DATA) ))
#endif
    {
        // Power on the module
        U1PWRC                = U1PWRC_NORMAL_OPERATION | U1PWRC_ENABLED;

    #if defined( __PIC32__ )
        // Enable the USB interrupt.
        _ClearUSBIF();
        // Set priority
        #if defined(_IPC11_USBIP_MASK)
            IPC11CLR        = _IPC11_USBIP_MASK | _IPC11_USBIS_MASK;
            IPC11SET        = _IPC11_USBIP_MASK & (USB_INTERRUPT_PRIORITY << _IPC11_USBIP_POSITION);
        #elif defined(_IPC7_USBIP_MASK)
            IPC7CLR        = _IPC7_USBIP_MASK | _IPC7_USBIS_MASK;
            IPC7SET        = _IPC7_USBIP_MASK & (USB_INTERRUPT_PRIORITY << _IPC7_USBIP_POSITION);
        #else
            #error "The selected PIC32 device is not currently supported by usb_host.c."
        #endif
        _SetUSBIE(); 
    #else
        #error "Cannot enable USB interrupt."
    #endif

        // Set the next substate.  We do this before we enable
        // interrupts in case the interrupt changes states.
        _USB_SetNextSubState();

        // Enable the ATTACH interrupt.
        U1IEbits.ATTACHIE = 1;

#if defined(USB_ENABLE_1MS_EVENT)
        U1OTGIR                 = USB_INTERRUPT_T1MSECIF; // The interrupt is cleared by writing a '1' to the flag.
        U1OTGIEbits.T1MSECIE    = 1;
#endif
    }
#ifdef USE_USB_VBUS_POWER
    else
    {
        usbRootHubInfo.flags.bPowerGoodPort0 = 0;
        usbHostState = STATE_DETACHED | SUBSTATE_WAIT_FOR_POWER;
    }
#endif
}

/****************************************************************************/
static __inline__ void USBHostTask_Detached_WaitForDevice(void)
{
// Wait here for the ATTACH interrupt.
#ifdef  USB_SUPPORT_OTG
    U1IEbits.ATTACHIE = 1;
#endif
}

/****************************************************************************/
static __inline__ void USBHostTask_Attached_Settle(void)
{
    // Wait 100ms for the insertion process to complete and power
    // at the device to be stable.
    switch (usbHostState & SUBSUBSTATE_MASK)
    {
        case SUBSUBSTATE_START_SETTLING_DELAY:
#if defined (DEBUG_ENABLE)
            UART_PutString( "HOST: Wait..." );
#endif
            // Clear and turn on the DETACH interrupt.
            U1IR                    = U1IE_INTERRUPT_DETACH;   // The interrupt is cleared by writing a '1' to the flag.
            U1IESET                 = U1IE_INTERRUPT_DETACH;

            // Configure and turn on the settling timer - 100ms.
            USBHost_StartTimer(USB_INSERT_TIME, NULL);

            _USB_SetNextSubSubState();
            break;

        case SUBSUBSTATE_WAIT_FOR_SETTLING:
            // Wait for the timer to finish in the background.
            break;

        case SUBSUBSTATE_SETTLING_DONE:
#if defined (DEBUG_ENABLE)
            UART_PutString( " Done.\r\n" );
#endif
            _USB_SetNextSubState();
            break;

        default:
            // We shouldn't get here.
            break;
    }
}

/****************************************************************************/
static bool _USBHost_AallocEP0Data( USB_HOST_INFO *hostInfo, uint16_t newsize )
{
    USB_FREE_AND_CLEAR(hostInfo->pEP0Data);
    hostInfo->wEP0DataSize = newsize;
    hostInfo->pEP0Data = (USB_ENDPOINT_DATA *)USB_MALLOC(hostInfo->wEP0DataSize * sizeof(uint8_t));
    if (!hostInfo->pEP0Data) {
        hostInfo->wEP0DataSize = 0;
        _USB_SetErrorCode( *hostInfo, USB_HOLDING_OUT_OF_MEMORY );
        _USB_SetHoldState();
        return false;
    }
    return true;
}

/****************************************************************************/
static __inline__ void USBHostTask_Attached_ResetDevice(void)
{
    // Reset the device.  We have to do the reset timing ourselves.
    switch (usbHostState & SUBSUBSTATE_MASK)
    {
        case SUBSUBSTATE_SET_RESET:
#if defined (DEBUG_ENABLE)
            UART_PutString( "HOST: Reset On..." );
#endif
            // Prepare a data buffer for us to use.  We'll make it 8 bytes for now,
            // which is the minimum wMaxPacketSize for EP0.
            if (!_USBHost_AallocEP0Data(&usbHostInfo, USB_EP0DATA_DEFAULT_SIZE))
            {
                break;
            }

            // Initialize the USB Device information
//            usbDeviceInfo.currentConfiguration      = 0;
//            usbDeviceInfo.attributesOTG             = 0;
            usbHostInfo.flags.val                 = 0;

            _USB_InitErrorCounters();

            // Disable all EP's except EP0.
            U1EP0  = USB_ENDPOINT_CONTROL_SETUP;
            
            U1EP1  = USB_DISABLE_ENDPOINT;
            U1EP2  = USB_DISABLE_ENDPOINT;
            U1EP3  = USB_DISABLE_ENDPOINT;
            U1EP4  = USB_DISABLE_ENDPOINT;
            U1EP5  = USB_DISABLE_ENDPOINT;
            U1EP6  = USB_DISABLE_ENDPOINT;
            U1EP7  = USB_DISABLE_ENDPOINT;
            U1EP8  = USB_DISABLE_ENDPOINT;
            U1EP9  = USB_DISABLE_ENDPOINT;
            U1EP10 = USB_DISABLE_ENDPOINT;
            U1EP11 = USB_DISABLE_ENDPOINT;
            U1EP12 = USB_DISABLE_ENDPOINT;
            U1EP13 = USB_DISABLE_ENDPOINT;
            U1EP14 = USB_DISABLE_ENDPOINT;
            U1EP15 = USB_DISABLE_ENDPOINT;

            // See if the device is low speed.
            if (!U1CONbits.JSTATE)
            {
#if defined (DEBUG_ENABLE)
                UART_PutString( " Low-speed" );
#endif
//              usbHostInfo.flags.bfIsLowSpeed    = 1;
                usbDeviceInfos[0].deviceSpeed     = 0xc0;
//              U1ADDR                              = 0x80;
//              U1EP0bits.LSPD                      = 1;
            } else {
#if defined (DEBUG_ENABLE)
                UART_PutString( " Full-speed" );
#endif
//              usbHostInfo.flags.bfIsLowSpeed    = 0;
                usbDeviceInfos[0].deviceSpeed     = 0;
//              U1ADDR                              = 0;
//              U1EP0bits.LSPD                      = 0;
                
            }

            // Reset all ping-pong buffers if they are being used.
            U1CONbits.PPBRST                    = 1;
            U1CONbits.PPBRST                    = 0;
            USBTrans_Init();
//            usbBusInfo.flags.bfPingPongIn       = 0;
//            usbBusInfo.flags.bfPingPongOut      = 0;

#ifdef  USB_SUPPORT_OTG
            //Disable HNP
            USBOTGDisableHnp();
            USBOTGDeactivateHnp();
#endif

            // Assert reset for 10ms.  Start a timer countdown.
            U1CONbits.USBRST                    = 1;
            USBHost_StartTimer(USB_RESET_TIME, NULL);

            _USB_SetNextSubSubState();
            break;

        case SUBSUBSTATE_RESET_WAIT:
            // Wait for the timer to finish in the background.
            break;

        case SUBSUBSTATE_RESET_RECOVERY:
#if defined (DEBUG_ENABLE)
            UART_PutString( " Off" );
#endif
            // Deassert reset.
            U1CONbits.USBRST        = 0;

            // Start sending SOF's.
            U1CONbits.SOFEN         = 1;

            // Wait for the reset recovery time.
            USBHost_StartTimer(USB_RESET_RECOVERY_TIME, NULL);

            _USB_SetNextSubSubState();
            break;

        case SUBSUBSTATE_RECOVERY_WAIT:
            // Wait for the timer to finish in the background.
            break;

        case SUBSUBSTATE_RESET_COMPLETE:
#if defined (DEBUG_ENABLE)
            UART_PutString( " Done.\r\n" );
#endif
            // Enable USB interrupts
            U1IE                    = U1IE_INTERRUPT_TRANSFER | U1IE_INTERRUPT_SOF | U1IE_INTERRUPT_ERROR | U1IE_INTERRUPT_DETACH;
            U1EIE                   = 0xFF;

            _USB_SetNextSubState();
            break;

        default:
            // We shouldn't get here.
            break;
    }
}

/****************************************************************************/
static void USBHostTask_WaitTransferCompleted(void)
{
    if (usbHostInfo.pEndpoint0->status.bfTransferComplete)
    {
        if (usbHostInfo.pEndpoint0->status.bfTransferSuccessful)
        {
#ifndef USB_HUB_SUPPORT_INCLUDED
            if (usbHostSubState == (STATE_ATTACHED | SUBSTATE_GET_DEVICE_DESCRIPTOR_SIZE)
                // See if a hub is attached.  Hubs are not supported.
                && pEP0Data[4] == USB_HUB_CLASSCODE)   // bDeviceClass
            {
                _USB_SetErrorCode( usbHostInfo, USB_HOLDING_UNSUPPORTED_HUB );
                _USB_SetHoldState();
            }
            else
            {
                _USB_SetNextSubSubState();
            }
#else
            _USB_SetNextSubSubState();
#endif
        }
        else
        {
            // We are here because of either a STALL or a NAK.  See if
            // we have retries left to try the command again or try to
            // enumerate again.
            _USB_CheckCommandAndEnumerationAttempts();
        }
    }
}


/****************************************************************************/
static void USBHostTask_GetSetRequest(void)
{
    // Send the GET DEVICE DESCRIPTOR command to get just the size
    // of the descriptor and the max packet size, so we can allocate
    // a large enough buffer for getting the whole thing and enough
    // buffer space for each piece.
    USB_DEVICE_INFO *deviceInfo = usbHostInfo.pCurrentDeviceInfo;

    uint16_t usbHostSubState = usbHostState & (STATE_MASK | SUBSTATE_MASK);

    switch (usbHostState & SUBSUBSTATE_MASK)
    {
        case SUBSUBSTATE_SEND_GET_DEVICE_DESCRIPTOR_SIZE:

            usbHostInfo.pEP0Data->l = 0;
            usbHostInfo.pEP0Data->setup.bmRequestType = USB_SETUP_DEVICE_TO_HOST | USB_SETUP_TYPE_STANDARD | USB_SETUP_RECIPIENT_DEVICE;
            usbHostInfo.pEP0Data->setup.bRequest = USB_REQUEST_GET_DESCRIPTOR;
//            pEP0Data[2] = 0; // Index
//            pEP0Data[3] = 0;
//            pEP0Data[4] = 0;
//            pEP0Data[5] = 0;
//            pEP0Data[6] = 0;
//            pEP0Data[7] = 0;

            if (usbHostSubState == (STATE_ATTACHED | SUBSTATE_GET_DEVICE_DESCRIPTOR_SIZE)) {
                // SUBSTATE_GET_DEVICE_DESCRIPTOR_SIZE
#if defined (DEBUG_ENABLE)
                UART_Flush();
                UART_PutString( "HOST: 11 Get device desc size.\r\n" );
#endif
                // Set up and send GET DEVICE DESCRIPTOR
                USB_FREE_AND_CLEAR( usbDeviceInfos[0].deviceDescriptor );

                usbHostInfo.pEP0Data->b[3] = USB_DESCRIPTOR_DEVICE; // Type
                usbHostInfo.pEP0Data->b[6] = 8;
                usbHostInfo.pEndpoint0->clientDriver = CLIENT_DRIVER_HOST;
                // always transmit to address 0
                USB_InitControlReadWrite( false, &usbDeviceInfos[0], usbHostInfo.pEndpoint0
                        , &usbHostInfo.pEP0Data->setup, 8
                        , usbHostInfo.pEP0Data->b, 8 );

            } else if (usbHostSubState == (STATE_ATTACHED | SUBSTATE_GET_DEVICE_DESCRIPTOR)) {
                // SUBSTATE_GET_DEVICE_DESCRIPTOR
#if defined (DEBUG_ENABLE)
                UART_Flush();
                UART_PutString( "HOST: 12 Get device desc.\r\n" );
#endif
                // If we are currently sending a token, we cannot do anything.
//                if (usbBusInfo.flags.bfTokenAlreadyWritten) {   //(U1CONbits.TOKBUSY)
//                    break;
//                }

                // Set up and send GET DEVICE DESCRIPTOR
                usbHostInfo.pEP0Data->b[3] = USB_DESCRIPTOR_DEVICE; // Type
                usbHostInfo.pEP0Data->b[6] = usbDeviceInfos[0].deviceDescriptor->bLength;   // Size
                // always transmit to address 0
                USB_InitControlReadWrite( false, &usbDeviceInfos[0], usbHostInfo.pEndpoint0
                        , &usbHostInfo.pEP0Data->setup, 8
                        , usbDeviceInfos[0].deviceDescriptor->b
                        , usbDeviceInfos[0].deviceDescriptor->bLength );

            } else if (usbHostSubState == (STATE_CONFIGURING | SUBSTATE_GET_CONFIG_DESCRIPTOR_SIZE)) {
                // SUBSTATE_GET_CONFIG_DESCRIPTOR_SIZE
#if defined (DEBUG_ENABLE)
                UART_Flush();
                UART_PutString( "HOST: 31 Get config desc size.\r\n" );
#endif
                // Set up and send GET CONFIGURATION (n) DESCRIPTOR with a length of 8
                usbHostInfo.pEP0Data->b[2] = usbHostInfo.tempCountConfigurations - 1;    // USB 2.0 - range is 0 - count-1
                usbHostInfo.pEP0Data->b[3] = USB_DESCRIPTOR_CONFIGURATION;
                usbHostInfo.pEP0Data->b[6] = 8;
                USB_InitControlReadWrite( false, deviceInfo, usbHostInfo.pEndpoint0
                        , &usbHostInfo.pEP0Data->setup, 8
                        , usbHostInfo.pEP0Data->b, 8 );

            } else if (usbHostSubState == (STATE_CONFIGURING | SUBSTATE_GET_CONFIG_DESCRIPTOR)) {
                // SUBSTATE_GET_CONFIG_DESCRIPTOR
#if defined (DEBUG_ENABLE)
                UART_Flush();
                UART_PutString( "HOST: 32 Get config desc.\r\n" );
#endif
                // Set up and send GET CONFIGURATION (n) DESCRIPTOR.
                usbHostInfo.pEP0Data->b[2] = usbHostInfo.tempCountConfigurations - 1;    // USB 2.0 - range is 0 - count-1
                usbHostInfo.pEP0Data->b[3] = USB_DESCRIPTOR_CONFIGURATION;
                usbHostInfo.pEP0Data->setup.wLength = ((USB_CONFIGURATION_DESCRIPTOR *)deviceInfo->pConfigurationDescriptorList->descriptor)->wTotalLength;    // wTotalLength
//                pEP0Data[7] = deviceInfo->pConfigurationDescriptorList->descriptor->wTotalLength;
                USB_InitControlReadWrite( false, deviceInfo, usbHostInfo.pEndpoint0
                        , &usbHostInfo.pEP0Data->setup, 8
                        , (uint8_t *)deviceInfo->pConfigurationDescriptorList->descriptor
                        , usbHostInfo.pEP0Data->setup.wLength );
            
            } else if (usbHostSubState == (STATE_CONFIGURING | SUBSTATE_SET_CONFIGURATION)) {
#if defined (DEBUG_ENABLE)
                UART_Flush();
                UART_PutString( "HOST: 33 Set Config.\r\n" );
#endif
                // SUBSTATE_SET_CONFIGURATION
                usbHostInfo.pEP0Data->b[0] = USB_SETUP_HOST_TO_DEVICE | USB_SETUP_TYPE_STANDARD | USB_SETUP_RECIPIENT_DEVICE;
                usbHostInfo.pEP0Data->b[1] = USB_REQUEST_SET_CONFIGURATION;
                usbHostInfo.pEP0Data->b[2] = deviceInfo->currentConfigurationNumber;
                USB_InitControlReadWrite( true, deviceInfo, usbHostInfo.pEndpoint0
                        , &usbHostInfo.pEP0Data->setup, 8
                        , NULL, 0 );

            }
            _USB_SetNextSubSubState();
            break;

        case SUBSUBSTATE_WAIT_FOR_GET_DEVICE_DESCRIPTOR_SIZE:
#if defined (DEBUG_ENABLE)
            UART_Flush();
#endif
            USBHostTask_WaitTransferCompleted();
            break;

        case SUBSUBSTATE_GET_DEVICE_DESCRIPTOR_SIZE_COMPLETE:
            if (usbHostSubState == (STATE_ATTACHED | SUBSTATE_GET_DEVICE_DESCRIPTOR_SIZE)) {
                // Received SUBSTATE_GET_DEVICE_DESCRIPTOR_SIZE
#if defined (DEBUG_ENABLE)
                UART_PutString( "HOST: 11 Received.\r\n" );
#endif
                // Allocate a buffer for the entire Device Descriptor
                if ((usbDeviceInfos[0].deviceDescriptor = (USB_DEVICE_DESCRIPTOR *)USB_MALLOC(usbHostInfo.pEP0Data->dd.bLength)) == NULL)
                {
                    // We cannot continue.  Freeze until the device is removed.
                    _USB_SetErrorCode( usbHostInfo, USB_HOLDING_OUT_OF_MEMORY );
                    _USB_SetHoldState();
                    break;
                }
                // Save the descriptor size in the descriptor (bLength)
                usbDeviceInfos[0].deviceDescriptor->bLength = usbHostInfo.pEP0Data->dd.bLength;

                // Set the EP0 packet size.
                usbHostInfo.pEndpoint0->wMaxPacketSize = usbHostInfo.pEP0Data->dd.bMaxPacketSize0;

                // Make our pEP0Data buffer the size of the max packet.
                if (usbHostInfo.pEndpoint0->wMaxPacketSize > usbHostInfo.wEP0DataSize) {
                    if (!_USBHost_AallocEP0Data(&usbHostInfo, usbHostInfo.pEndpoint0->wMaxPacketSize))
                    {
                        break;
                    }
                }
                _USB_SetNextSubState();

            } else if (usbHostSubState == (STATE_ATTACHED | SUBSTATE_GET_DEVICE_DESCRIPTOR)) {
                // Received SUBSTATE_GET_DEVICE_DESCRIPTOR
#if defined (DEBUG_ENABLE)
                UART_PutString( "HOST: 12 Received.\r\n" );
#endif
#if defined (DEBUG_ENABLE)
                UART_PutHexString( usbDeviceInfos[0].deviceDescriptor->b
                            , usbDeviceInfos[0].deviceDescriptor->bLength );
#endif
                // Nothing to do
                _USB_SetNextSubState();

            } else if (usbHostSubState == (STATE_CONFIGURING | SUBSTATE_GET_CONFIG_DESCRIPTOR_SIZE)) {
                // Received SUBSTATE_GET_CONFIG_DESCRIPTOR_SIZE
#if defined (DEBUG_ENABLE)
                UART_PutString( "HOST: 31 Received.\r\n" );
#endif
                // Allocate a buffer for an entry in the configuration descriptor list.
                if (!USBStructConfigList_PushFront(&deviceInfo->pConfigurationDescriptorList
                        , usbHostInfo.tempCountConfigurations
                        , usbHostInfo.pEP0Data->cd.wTotalLength
//                        , ((uint16_t)pEP0Data[3] << 8) + (uint16_t)pEP0Data[2]
                ))
                {
                    // We cannot continue.  Freeze until the device is removed.
                    _USB_SetErrorCode( usbHostInfo, USB_HOLDING_OUT_OF_MEMORY );
                    _USB_SetHoldState();
                    break;
                }

                // Save the configuration descriptor pointer and number
//                pTempConfigurationDescriptor = deviceInfo->pConfigurationDescriptorList->descriptor;
                deviceInfo->currentConfigurationDescriptor = deviceInfo->pConfigurationDescriptorList->descriptor;

                // Clean up and advance to the next state.
                _USB_SetNextSubState();

            } else if (usbHostSubState == (STATE_CONFIGURING | SUBSTATE_GET_CONFIG_DESCRIPTOR)) {
                // Received SUBSTATE_GET_CONFIG_DESCRIPTOR
#if defined (DEBUG_ENABLE)
                UART_PutString( "HOST: 32 Received.\r\n" );
#endif
                // Clean up and advance to the next state.  Keep the data for later use.
                usbHostInfo.tempCountConfigurations--;
                if (usbHostInfo.tempCountConfigurations)
                {
                    // There are more descriptors that we need to get.
                    usbHostState = STATE_CONFIGURING | SUBSTATE_GET_CONFIG_DESCRIPTOR_SIZE;
                }
                else
                {
                    // Start configuring the device.
                    _USB_SetNextSubState();
                }

            } else if (usbHostSubState == (STATE_CONFIGURING | SUBSTATE_SET_CONFIGURATION)) {
                // Received SUBSTATE_SET_CONFIGURATION
#if defined (DEBUG_ENABLE)
                UART_PutString( "HOST: 33 Received.\r\n" );
#endif
                // Nothing to do
                _USB_SetNextSubState();

            } else {
#if defined (DEBUG_ENABLE)
                DEBUG_PutString( "HOST: Received.\r\n" );
#endif
                // Nothing to do
                _USB_SetNextSubState();

            }

            // Clean up and advance to the next substate.
            _USB_InitErrorCounters();
            break;

        default:
            break;
    }
}

/****************************************************************************/
static __inline__ void USBHostTask_Attached_ValidateVidPid(void)
{
#if defined (DEBUG_ENABLE)
    DEBUG_PutString( "HOST: Validating VID and PID...\r\n" );
#endif

    // Search the TPL for the device's VID & PID.  If a client driver is
    // available for the over-all device, use it.  Otherwise, we'll search
    // again later for an appropriate class driver.
    _USB_FindDeviceLevelClientDriver( usbDeviceInfos[0].deviceDescriptor );

    // Advance to the next state to assign an address to the device.
    //
    // Note: We assign an address to all devices and hold later if
    // we can't find a supported configuration.
    _USB_SetNextState();
}

/****************************************************************************/
static __inline__ void USBHostTask_Addressing_SetDeviceAddress(void)
{
    // Send the SET ADDRESS command.  We can't set the device address
    // in hardware until the entire transaction is complete.
    USB_DEVICE_INFO *deviceInfo = NULL;

    switch (usbHostState & SUBSUBSTATE_MASK)
    {
        case SUBSUBSTATE_SEND_SET_DEVICE_ADDRESS:
            // Select an address for the device.  Store it so we can access it again
            // easily.  We'll put the low speed indicator on later.
            // This has been broken out so when we allow multiple devices, we have
            // a single interface point to allocate a new address.
//            usbDeviceInfo.deviceAddress = USB_SINGLE_DEVICE_ADDRESS;
            usbHostInfo.reservedAddress = USBHostDeviceInfos_FindAddress();
            
            // deivce is full
            if (usbHostInfo.reservedAddress == 0) {
                _USBHost_DeviceConfigureingError(&usbHostInfo, usbDeviceInfos[0].hubAddress, usbHostInfo.reservedAddress, USB_HOLDING_CLIENT_INIT_ERROR);
                return;
            }

#if defined (DEBUG_ENABLE)
            UART_Flush();
            UART_PutString( "HOST: 21 Setting an address: " );
#endif

            // Set up and send SET ADDRESS
            usbHostInfo.pEP0Data->l = 0;
            usbHostInfo.pEP0Data->setup.bmRequestType = USB_SETUP_HOST_TO_DEVICE | USB_SETUP_TYPE_STANDARD | USB_SETUP_RECIPIENT_DEVICE;
            usbHostInfo.pEP0Data->setup.bRequest = USB_REQUEST_SET_ADDRESS;
            usbHostInfo.pEP0Data->setup.wValue = usbHostInfo.reservedAddress;
//            pEP0Data[3] = 0;
//            pEP0Data[4] = 0;
//            pEP0Data[5] = 0;
//            pEP0Data[6] = 0;
//            pEP0Data[7] = 0;
#if defined (DEBUG_ENABLE)
            UART_PutHexU8( usbHostInfo.reservedAddress );
#endif
            // always transmit to address 0
            USB_InitControlReadWrite( true, &usbDeviceInfos[0], usbHostInfo.pEndpoint0
                    , &usbHostInfo.pEP0Data->setup, 8
                    , NULL, 0 );
            _USB_SetNextSubSubState();
            break;

        case SUBSUBSTATE_WAIT_FOR_SET_DEVICE_ADDRESS:
#if defined (DEBUG_ENABLE)
            UART_Flush();
#endif
            USBHostTask_WaitTransferCompleted();
            break;

        case SUBSUBSTATE_SET_DEVICE_ADDRESS_COMPLETE:
#if defined (DEBUG_ENABLE)
            UART_PutString( "HOST: 21 Address complete.\r\n" );
#endif
            // Set the device's address here.
            // Copy to structure on desided address from address 0
            deviceInfo = &usbDeviceInfos[usbHostInfo.reservedAddress];
            deviceInfo->deviceAddress = usbHostInfo.reservedAddress;

            deviceInfo->deviceSpeed = usbDeviceInfos[0].deviceSpeed;
            deviceInfo->hubAddress = usbDeviceInfos[0].hubAddress;
            deviceInfo->portNumber = usbDeviceInfos[0].portNumber;
            // And, copy the pointer of device descriptor to structure on index n(address number)
            deviceInfo->deviceDescriptor = usbDeviceInfos[0].deviceDescriptor;

            // Set as the target device
            usbHostInfo.pCurrentDeviceInfo = deviceInfo;

            // Clear the device information on index 0
            usbHostInfo.reservedAddress = 0;
            memset(&usbDeviceInfos[0], 0, sizeof(usbDeviceInfos[0]));

//            delaySOFCount = 10;
            
            // Clean up and advance to the next state.
            _USB_InitErrorCounters();
            _USB_SetNextState();
            break;

        default:
            break;
    }
}

/****************************************************************************/
static __inline__ void USBHostTask_Configuring_InitConfiguration(void)
{
//    uint8_t *pTemp;
    USB_DEVICE_INFO *deviceInfo = usbHostInfo.pCurrentDeviceInfo;

    // Delete the old list of configuration descriptors and
    // initialize the counter.  We will request the descriptors
    // from highest to lowest so the lowest will be first in
    // the list.
    usbHostInfo.tempCountConfigurations = deviceInfo->deviceDescriptor->bNumConfigurations;
    deviceInfo->countConfigurations = usbHostInfo.tempCountConfigurations;

#if defined (DEBUG_ENABLE)
    UART_Flush();
    UART_PutStringHexU8( "HOST: Num of Config: ", deviceInfo->countConfigurations );
#endif

    // Clear configuration list
    USBStructConfigList_Clear(&deviceInfo->pConfigurationDescriptorList);

    if(usbHostInfo.tempCountConfigurations == 0)
    {
        _USB_SetErrorCode( usbHostInfo, USB_HOLDING_CLIENT_INIT_ERROR );
        _USB_SetHoldState();
    }
    else
    {
        _USB_SetNextSubState();
    }
}

/****************************************************************************/
static __inline__ void USBHostTask_Configuring_SelectConfigration(void)
{
    USB_DEVICE_INFO *deviceInfo = usbHostInfo.pCurrentDeviceInfo;
    static USB_CONFIGURATION *pCurrentConfigurationNode;

    // Set the OTG configuration of the device
    switch (usbHostState & SUBSUBSTATE_MASK)
    {
        case SUBSUBSTATE_SELECT_CONFIGURATION:
#if defined (DEBUG_ENABLE)
            UART_Flush();
            UART_PutString( "HOST: Selecting Configure\r\n" );
#endif
            // Free the old configuration (if any)
            USB_InterfaceList_Clear(&deviceInfo->pInterfaceList);
//            pCurrentEndpoint = usbHostInfo.pEndpoint0;

            // If the configuration wasn't selected based on the VID & PID
            if (deviceInfo->currentConfigurationNumber == 0)
            {
                // Search for a supported class-specific configuration.
                pCurrentConfigurationNode = deviceInfo->pConfigurationDescriptorList;
                while (pCurrentConfigurationNode)
                {
                    if (_USB_ParseConfigurationDescriptor(pCurrentConfigurationNode->descriptor->b))
                    {
                        deviceInfo->currentConfigurationDescriptor = pCurrentConfigurationNode->descriptor;
                        break;
                    }
                    else
                    {
                        // Free the memory allocated and
                        // advance to  next configuration
                        USB_InterfaceList_Clear(&deviceInfo->pInterfaceList);
//                        pCurrentEndpoint = usbHostInfo.pEndpoint0;
                        pCurrentConfigurationNode = pCurrentConfigurationNode->next;
                    }
                }
            }
            else
            {
                // Configuration selected by VID & PID, initialize data structures
                pCurrentConfigurationNode = USBStructConfigList_FindItemByNumber(deviceInfo->pConfigurationDescriptorList, deviceInfo->currentConfigurationNumber);
                if (_USB_ParseConfigurationDescriptor(pCurrentConfigurationNode->descriptor->b))
                {
                    deviceInfo->currentConfigurationDescriptor = pCurrentConfigurationNode->descriptor;
                }
                else
                {
                    // Free the memory allocated, config attempt failed.
                    USB_InterfaceList_Clear(&deviceInfo->pInterfaceList);
//                    pCurrentEndpoint = usbHostInfo.pEndpoint0;
                    pCurrentConfigurationNode = NULL;
                }
            }

            //If No OTG Then
#ifdef USB_SUPPORT_OTG
            if (!usbDeviceInfo.flags.bfConfiguredOTG)
            {
                _USB_SetNextSubSubState();
            }
            else
#endif
            {
                // Did we fail to configure?
                if (pCurrentConfigurationNode == NULL)
                {
                    // Failed to find a supported configuration.
                    _USBHost_DeviceConfigureingError(&usbHostInfo, deviceInfo->hubAddress, deviceInfo->deviceAddress, USB_HOLDING_UNSUPPORTED_DEVICE);
                }
                else
                {
                    _USB_SetNextSubSubState();
                }
            }
            break;

        case SUBSUBSTATE_SEND_SET_OTG:
#ifdef USB_SUPPORT_OTG
#if defined (DEBUG_ENABLE)
            UART_PutString( "HOST: Determine OTG capability.\r\n" );
#endif
            // If the device does not support OTG, or
            // if the device has already been configured, bail.
            // Otherwise, send SET FEATURE to configure it.
            if (!usbDeviceInfo.flags.bfConfiguredOTG)
            {
#if defined (DEBUG_ENABLE)
                DEBUG_PutString( "HOST: ...OTG needs configuring.\r\n" );
#endif

                usbDeviceInfo.flags.bfConfiguredOTG = 1;

                // Send SET FEATURE
                pEP0Data[0] = USB_SETUP_HOST_TO_DEVICE | USB_SETUP_TYPE_STANDARD | USB_SETUP_RECIPIENT_DEVICE;
                pEP0Data[1] = USB_REQUEST_SET_FEATURE;
                if (usbDeviceInfo.flags.bfAllowHNP) // Needs to be set by the user
                {
                    pEP0Data[2] = OTG_FEATURE_B_HNP_ENABLE;
                }
                else
                {
                    pEP0Data[2] = OTG_FEATURE_A_HNP_SUPPORT;
                }
                pEP0Data[3] = 0;
                pEP0Data[4] = 0;
                pEP0Data[5] = 0;
                pEP0Data[6] = 0;
                pEP0Data[7] = 0;
                USB_InitControlReadWrite( true, pCurrentDeviceInfo, usbDeviceInfo.pEndpoint0, pEP0Data, 8, NULL, 0 );
                _USB_SetNextSubSubState();
            }
            else
#endif
            {
                _USB_SetNextSubState();
            }
            break;

#ifdef USB_SUPPORT_OTG
        case SUBSUBSTATE_WAIT_FOR_SET_OTG_DONE:
            if (usbDeviceInfo.pEndpoint0->status.bfTransferComplete)
            {
                if (usbDeviceInfo.pEndpoint0->status.bfTransferSuccessful)
                {
                    #ifdef  USB_SUPPORT_OTG
                        if (usbDeviceInfo.flags.bfAllowHNP)
                        {
                            USBOTGEnableHnp();
                        }
                     #endif
                    _USB_SetNextSubSubState();
                }
                else
                {
                    #ifdef  USB_SUPPORT_OTG
                        USBOTGDisableHnp();
                    #endif
                    // We are here because of either a STALL or a NAK.  See if
                    // we have retries left to try the command again or try to
                    // enumerate again.
                    _USB_CheckCommandAndEnumerationAttempts();

                    #if defined(USB_SUPPORT_OTG)
#if defined (DEBUG_ENABLE)
                        DEBUG_PutString( "\r\n***** USB OTG Error - Set Feature B_HNP_ENABLE Stalled - Device Not Responding *****\r\n" );
#endif
                    #endif

                }
            }
            break;

        case SUBSUBSTATE_SET_OTG_COMPLETE:
             // Clean up and advance to the next state.
           _USB_InitErrorCounters();

            //MR - Moved For OTG Set Feature Support For Unsupported Devices
            // Did we fail to configure?
            if (pCurrentConfigurationNode == NULL)
            {
                // Failed to find a supported configuration.
                _USB_SetErrorCode( usbHostInfo, USB_HOLDING_UNSUPPORTED_DEVICE );
                _USB_SetHoldState();
            }
            else
            {
                //_USB_SetNextSubSubState();
                _USB_InitErrorCounters();
                _USB_SetNextSubState();
            }
            break;
#endif /* USB_SUPPORT_OTG */

        default:
            break;
    }
}

/****************************************************************************/
static __inline__ void USBHostTask_Configuring_ApplicationConfigration(void)
{
    if ( USB_HOST_APP_EVENT_HANDLER( USB_ROOT_HUB, EVENT_HOLD_BEFORE_CONFIGURATION,
            NULL, usbHostInfo.pCurrentDeviceInfo->deviceAddress ) == false )
    {
        _USB_SetNextSubState();
    }
}

/****************************************************************************/
static __inline__ void USBHostTask_Configuring_InitClientDrivers(void)
{
    USB_DEVICE_INFO *deviceInfo = usbHostInfo.pCurrentDeviceInfo;
    USB_INTERFACE_INFO *pCurrentInterface;
    bool sts = true;

#if defined (DEBUG_ENABLE)
    UART_Flush();
#endif

    _USB_SetNextState();
    // Initialize client driver(s) for this configuration.
    if (deviceInfo->flags.bfUseDeviceClientDriver)
    {
#if defined (DEBUG_ENABLE)
        UART_PutStringHexU8( "HOST: Initializing client driver at addr ", deviceInfo->deviceAddress );
        UART_PutStringHexU8( "HOST: Client num: ", pCurrentInterface->clientDriver );
#endif
        // We have a device that requires only one client driver.  Make sure
        // that client driver can initialize this device.  If the client
        // driver initialization fails, we cannot enumerate this device.

        sts = USBClientDriver_Initialize(deviceInfo->deviceAddress, deviceInfo->deviceClientDriver);
    }
    else
    {
        // We have a device that requires multiple client drivers.  Make sure
        // every required client driver can initialize this device.  If any
        // client driver initialization fails, we cannot enumerate the device.
#if defined (DEBUG_ENABLE)
        UART_PutStringHexU8( "HOST: Initializing interface driver at addr ", deviceInfo->deviceAddress );
#endif

        pCurrentInterface = deviceInfo->pInterfaceList;
        while (pCurrentInterface && sts)
        {
#if defined (DEBUG_ENABLE)
            UART_PutStringHexU8( "HOST: Client num: ", pCurrentInterface->clientDriver );
#endif
            sts = USBClientDriver_Initialize(deviceInfo->deviceAddress, pCurrentInterface->clientDriver);
            pCurrentInterface = pCurrentInterface->next;
        }
    }

    //Load the EP0 driver, if there was any
    if(sts && deviceInfo->flags.bfUseEP0Driver)
    {
#if defined (DEBUG_ENABLE)
        UART_PutStringHexU8( "HOST: Initializing EP0 driver at addr ", deviceInfo->deviceAddress );
        UART_PutStringHexU8( "HOST: Client num: ", deviceInfo->deviceEP0Driver );
#endif
        sts = USBClientDriver_Initialize(deviceInfo->deviceAddress, deviceInfo->deviceEP0Driver);
    }

    if (!sts) {
#if defined (DEBUG_ENABLE)
        UART_PutString( "Error occured.\r\n" );
        UART_Flush();
#endif
        _USBHost_DeviceConfigureingError(&usbHostInfo, deviceInfo->hubAddress, deviceInfo->deviceAddress, USB_HOLDING_CLIENT_INIT_ERROR);
    } else {
#if defined (DEBUG_ENABLE)
        UART_PutString( "Done.\r\n" );
        UART_Flush();
#endif
    }
}

#ifdef USE_USB_DEVICE_SUSPEND
/****************************************************************************/
static __inline__ void USBHostTask_Running_SuspendAndResume(void)
{
    switch (usbHostState & SUBSUBSTATE_MASK)
    {
        case SUBSUBSTATE_SUSPEND:
            // The IDLE state has already been set.  We need to wait here
            // until the application decides to RESUME.
            break;

        case SUBSUBSTATE_RESUME:
            // Issue a RESUME.
            U1CONbits.RESUME = 1;

            // Wait for the RESUME time.
            _USBHost_StartTimer(USB_RESUME_TIME);

            _USB_SetNextSubSubState();
            break;

        case SUBSUBSTATE_RESUME_WAIT:
            // Wait here until the timer expires.
            break;

        case SUBSUBSTATE_RESUME_RECOVERY:
            // Turn off RESUME.
            U1CONbits.RESUME        = 0;

            // Start sending SOF's, so the device doesn't go back into the SUSPEND state.
            U1CONbits.SOFEN         = 1;

            // Wait for the RESUME recovery time.
            _USBHost_StartTimer(USB_RESUME_RECOVERY_TIME);

            _USB_SetNextSubSubState();
            break;

        case SUBSUBSTATE_RESUME_RECOVERY_WAIT:
            // Wait here until the timer expires.
            break;

        case SUBSUBSTATE_RESUME_COMPLETE:
            // Go back to normal running.
            usbHostState = STATE_RUNNING | SUBSTATE_NORMAL_RUN;
            break;
    }
}
#endif

/****************************************************************************/
static __inline__ void USBHostTask_Holding_HoldInit(void)
{
    USB_DEVICE_INFO *deviceInfo = usbHostInfo.pCurrentDeviceInfo;
    uint8_t temp;

    // We're here because we cannot communicate with the current device
    // that is plugged in.  Turn off SOF's and all interrupts except
    // the DETACH interrupt.
#if defined (DEBUG_ENABLE)
    DEBUG_PutString( "HOST: Holding: " );
#endif

    U1CON               = U1CON_HOST_MODE_ENABLE | U1CON_SOF_DISABLE;                       // Turn of SOF's to cut down noise
    U1IE                = 0;
    U1IR                = 0xFF;
    U1OTGIE             &= 0x8C;
    U1OTGIR             = 0x7D;
    U1EIE               = 0;
    U1EIR               = 0xFF;
    U1IEbits.DETACHIE   = 1;

#if defined(USB_ENABLE_1MS_EVENT)
    U1OTGIR                 = USB_INTERRUPT_T1MSECIF; // The interrupt is cleared by writing a '1' to the flag.
    U1OTGIEbits.T1MSECIE    = 1;
#endif

    switch ( usbHostInfo.errorCode )
    {
        case USB_HOLDING_UNSUPPORTED_HUB:
            temp = EVENT_HUB_ATTACH;
#if defined (DEBUG_ENABLE)
            DEBUG_PutString( "HUB Attach" );
#endif
            break;

        case USB_HOLDING_UNSUPPORTED_DEVICE:
            temp = EVENT_UNSUPPORTED_DEVICE;

            #ifdef  USB_SUPPORT_OTG
            //Abort HNP
            USB_OTGEventHandler (0, OTG_EVENT_HNP_ABORT , 0, 0 );
            #endif
#if defined (DEBUG_ENABLE)
            DEBUG_PutString( "Unsupported Device" );
#endif

            break;

        case USB_CANNOT_ENUMERATE:
            temp = EVENT_CANNOT_ENUMERATE;
#if defined (DEBUG_ENABLE)
            DEBUG_PutString( "Cannot Enumerate" );
#endif
            break;

        case USB_HOLDING_CLIENT_INIT_ERROR:
            temp = EVENT_CLIENT_INIT_ERROR;
#if defined (DEBUG_ENABLE)
            DEBUG_PutString( "Init Error" );
#endif
            break;

        case USB_HOLDING_OUT_OF_MEMORY:
            temp = EVENT_OUT_OF_MEMORY;
#if defined (DEBUG_ENABLE)
            DEBUG_PutString( "Out of Memory" );
#endif
            break;

        default:
            temp = EVENT_UNSPECIFIED_ERROR; // This should never occur
#if defined (DEBUG_ENABLE)
            DEBUG_PutString( "Unknown Error" );
#endif
            break;
    }

#if defined (DEBUG_ENABLE)
    DEBUG_PutString( "\r\n" );
#endif

    // Report the problem to the application.
    USB_HOST_APP_EVENT_HANDLER( deviceInfo->deviceAddress, temp, &deviceInfo->currentConfigurationPower , 1 );

    _USB_SetNextSubState();
}

/****************************************************************************
  Function:
    void USBHostTasks( void )

  Summary:
    This function executes the host tasks for USB host operation.

  Description:
    This function executes the host tasks for USB host operation.  It must be
    executed on a regular basis to keep everything functioning.

    The primary purpose of this function is to handle device attach/detach
    and enumeration.  It does not handle USB packet transmission or
    reception; that must be done in the USB interrupt handler to ensure
    timely operation.

    This routine should be called on a regular basis, but there is no
    specific time requirement.  Devices will still be able to attach,
    enumerate, and detach, but the operations will occur more slowly as the
    calling interval increases.

  Precondition:
    USBHostInit() has been called.

  Parameters:
    None

  Returns:
    None

  Remarks:
    None
  ***************************************************************************/

void USBHostTasks( void )
{
    // The PIC32MX detach interrupt is not reliable.  If we are not in one of
    // the detached states, we'll do a check here to see if we've detached.
    // If the ATTACH bit is 0, we have detached.
#ifdef __PIC32__
    #ifdef USE_MANUAL_DETACH_DETECT
        if (((usbHostState & STATE_MASK) != STATE_DETACHED) && !U1IRbits.ATTACHIF)
        {
#if defined (DEBUG_ENABLE)
            DEBUG_PutChar( '>' );
            DEBUG_PutChar( ']' );
#endif

            usbHostState = STATE_DETACHED;
        }
    #endif
#endif

    // Send any queued events to the client and application layers.
#if defined ( USB_ENABLE_TRANSFER_EVENT )
    StructEventQueueProcess();
#endif

    // See if we got an interrupt to change our state.
    if (usbOverrideHostState != NO_STATE)
    {
#if defined (DEBUG_ENABLE)
        DEBUG_PutChar('>');
#endif
        usbHostState = usbOverrideHostState;
        usbOverrideHostState = NO_STATE;
    }

    //-------------------------------------------------------------------------
    // Main State Machine

    switch (usbHostState & STATE_MASK)
    {
        case STATE_DETACHED:
            switch (usbHostState & SUBSTATE_MASK)
            {
                case SUBSTATE_INITIALIZE:
                    USBHostTask_Detached_Initialize();
                    break;

                case SUBSTATE_WAIT_FOR_POWER:
                    USBHostTask_Detached_WaitForPower();
                    break;

                case SUBSTATE_TURN_ON_POWER:
                    USBHostTask_Detached_TurnOnPower();
                    break;

                case SUBSTATE_WAIT_FOR_DEVICE:
                    USBHostTask_Detached_WaitForDevice();
                break;
            }
            break;

        case STATE_ATTACHED:
            switch (usbHostState & SUBSTATE_MASK)
            {
                case SUBSTATE_SETTLE:
                    USBHostTask_Attached_Settle();
                    break;

                case SUBSTATE_RESET_DEVICE:
                    USBHostTask_Attached_ResetDevice();
                    break;

                case SUBSTATE_GET_DEVICE_DESCRIPTOR_SIZE:
                case SUBSTATE_GET_DEVICE_DESCRIPTOR:
                    USBHostTask_GetSetRequest();
                    break;

                case SUBSTATE_VALIDATE_VID_PID:
                    USBHostTask_Attached_ValidateVidPid();
                    break;
            }
            break;

        case STATE_ADDRESSING:
            switch (usbHostState & SUBSTATE_MASK)
            {
                case SUBSTATE_SET_DEVICE_ADDRESS:
                    USBHostTask_Addressing_SetDeviceAddress();
                    break;
            }
            break;

        case STATE_CONFIGURING:
            switch (usbHostState & SUBSTATE_MASK)
            {
                case SUBSTATE_INIT_CONFIGURATION:
                    USBHostTask_Configuring_InitConfiguration();
                    break;

                case SUBSTATE_GET_CONFIG_DESCRIPTOR_SIZE:
                case SUBSTATE_GET_CONFIG_DESCRIPTOR:
                    USBHostTask_GetSetRequest();
                    break;

                case SUBSTATE_SELECT_CONFIGURATION:
                    USBHostTask_Configuring_SelectConfigration();
                    break;

                case SUBSTATE_APPLICATION_CONFIGURATION:
                    USBHostTask_Configuring_ApplicationConfigration();
                    break;

                case SUBSTATE_SET_CONFIGURATION:
                    USBHostTask_GetSetRequest();
                    break;

                case SUBSTATE_INIT_CLIENT_DRIVERS:
                    USBHostTask_Configuring_InitClientDrivers();
                    break;
            }
            break;

        case STATE_RUNNING:
#ifdef USE_USB_DEVICE_SUSPEND
            switch (usbHostState & SUBSTATE_MASK)
            {
                case SUBSTATE_NORMAL_RUN:
                    break;

                case SUBSTATE_SUSPEND_AND_RESUME:
                    USBHostTask_Running_SuspendAndResume();
            }
#endif
            break;

        case STATE_HOLDING:
            switch (usbHostState & SUBSTATE_MASK)
            {
                case SUBSTATE_HOLD_INIT:
                    USBHostTask_Holding_HoldInit();
                    break;

                case SUBSTATE_HOLD:
                    // Hold here until a DETACH interrupt frees us.
                    break;

                default:
                    break;
            }
            break;
    }

}

/****************************************************************************
  Function:
    void USBHostTerminateTransfer( )


  Summary:
    This function terminates the current transfer for the given endpoint.

  Description:
    This function terminates the current transfer for the given endpoint.  It
    can be used to terminate reads or writes that the device is not
    responding to.  It is also the only way to terminate an isochronous
    transfer.

  Precondition:
    None

  Parameters:
    uint8_t deviceAddress  - Device address
    uint8_t endpoint       - Endpoint number

  Returns:
    None

  Remarks:
    None
  ***************************************************************************/

uint8_t USBHostTerminateTransfer( USB_DEVICE_INFO *deviceInfo, uint8_t endpoint )
{
    USB_ENDPOINT_INFO *ep;

    // Find the required device
    if (!deviceInfo)
    {
        return USB_UNKNOWN_DEVICE;
    }

    ep = _USB_FindEndpoint( deviceInfo, endpoint );
    if (!ep)
    {
        return USB_ENDPOINT_ERROR;
    }

    ep->status.bfUserAbort          = 1;
    ep->status.bfTransferComplete   = 1;
    return USB_SUCCESS;
}

#ifdef USE_USB_VBUS_POWER
/****************************************************************************
  Function:
    uint8_t  USBHostVbusEvent( USB_EVENT vbusEvent, uint8_t hubAddress,
                                        uint8_t portNumber)

  Summary:
    This function handles Vbus events that are detected by the application.

  Description:
    This function handles Vbus events that are detected by the application.
    Since Vbus management is application dependent, the application is
    responsible for monitoring Vbus and detecting overcurrent conditions
    and removal of the overcurrent condition.  If the application detects
    an overcurrent condition, it should call this function with the event
    EVENT_VBUS_OVERCURRENT with the address of the hub and port number that
    has the condition.  When a port returns to normal operation, the
    application should call this function with the event
    EVENT_VBUS_POWER_AVAILABLE so the stack knows that it can allow devices
    to attach to that port.

  Precondition:
    None

  Parameters:
    USB_EVENT vbusEvent     - Vbus event that occured.  Valid events:
                                    * EVENT_VBUS_OVERCURRENT
                                    * EVENT_VBUS_POWER_AVAILABLE
    uint8_t hubAddress         - Address of the hub device (USB_ROOT_HUB for the
                                root hub)
    uint8_t portNumber         - Number of the physical port on the hub (0 - based)

  Return Values:
    USB_SUCCESS             - Event handled
    USB_ILLEGAL_REQUEST     - Invalid event, hub, or port

  Remarks:
    None
  ***************************************************************************/

uint8_t  USBHostVbusEvent(USB_EVENT vbusEvent, uint8_t hubAddress, uint8_t portNumber)
{
    if ((hubAddress == USB_ROOT_HUB) &&
        (portNumber == 0 ))
    {
        if (vbusEvent == EVENT_VBUS_OVERCURRENT)
        {
            USBHostShutdown();
            usbRootHubInfo.flags.bPowerGoodPort0 = 0;
            return USB_SUCCESS;
        }
        if (vbusEvent == EVENT_VBUS_POWER_AVAILABLE)
        {
            usbRootHubInfo.flags.bPowerGoodPort0 = 1;
            return USB_SUCCESS;
        }
    }

    return USB_ILLEGAL_REQUEST;
}
#endif

/****************************************************************************
  Function:
    void _USB_CheckCommandAndEnumerationAttempts( void )

  Summary:
    This function is called when we've received a STALL or a NAK when trying
    to enumerate.

  Description:
    This function is called when we've received a STALL or a NAK when trying
    to enumerate.  We allow so many attempts at each command, and so many
    attempts at enumeration.  If the command fails and there are more command
    attempts, we try the command again.  If the command fails and there are
    more enumeration attempts, we reset and try to enumerate again.
    Otherwise, we go to the holding state.

  Precondition:
    usbHostState != STATE_RUNNING

  Parameters:
    None - None

  Returns:
    None

  Remarks:
    None
  ***************************************************************************/

void _USB_CheckCommandAndEnumerationAttempts( void )
{
#if defined (DEBUG_ENABLE)
    DEBUG_PutChar( '=' );
#endif

    // Clear the error and stall flags.  A stall here does not require
    // host intervention to clear.
    USBTrans_ClearControlEndpointStatus();
//    pCurrentEndpoint->status.bfError    = 0;
//    pCurrentEndpoint->status.bfStalled  = 0;

    numCommandTries --;
    if (numCommandTries != 0)
    {
        // We still have retries left on this command.  Try again.
        usbHostState &= ~SUBSUBSTATE_MASK;
    }
    else
    {
        // This command has timed out.
        // We are enumerating.  See if we can try to enumerate again.
        numEnumerationTries --;
        if (numEnumerationTries != 0)
        {
            // We still have retries left to try to enumerate.  Reset and try again.
            usbHostState = STATE_ATTACHED | SUBSTATE_RESET_DEVICE;
        }
        else
        {
            // Give up.  The device is not responding properly.
            _USB_SetErrorCode( usbHostInfo, USB_CANNOT_ENUMERATE );
            _USB_SetHoldState();
        }
    }
}


/****************************************************************************
  Function:
    bool _USB_FindClassDriver( uint8_t bClass, uint8_t bSubClass, uint8_t bProtocol, uint8_t *pbClientDrv )

  Summary:


  Description:
    This routine scans the TPL table looking for the entry with
                the given class, subclass, and protocol values.

  Precondition:
    usbTPL must be define by the application.

  Parameters:
    bClass      - The class of the desired entry
    bSubClass   - The subclass of the desired entry
    bProtocol   - The protocol of the desired entry
    pbClientDrv - Returned index to the client driver in the client driver
                    table.

  Return Values:
    true    - A class driver was found.
    false   - A class driver was not found.

  Remarks:
    None
  ***************************************************************************/

bool _USB_FindClassDriver( uint8_t bClass, uint8_t bSubClass, uint8_t bProtocol, uint8_t *pbClientDrv )
{
    USB_OVERRIDE_CLIENT_DRIVER_EVENT_DATA   eventData;
    int                                     i;
    USB_DEVICE_DESCRIPTOR                   *pDesc = usbHostInfo.pCurrentDeviceInfo->deviceDescriptor;

    i = 0;
    while (i < NUM_TPL_ENTRIES)
    {
        if ((usbTPL[i].flags.bfIsClassDriver == 1        ) &&
            (((usbTPL[i].flags.bfIgnoreClass == 0) ? (usbTPL[i].device.bClass == bClass) : true)) &&
            (((usbTPL[i].flags.bfIgnoreSubClass == 0) ? (usbTPL[i].device.bSubClass == bSubClass) : true)) &&
            (((usbTPL[i].flags.bfIgnoreProtocol == 0) ? (usbTPL[i].device.bProtocol == bProtocol) : true))  )
        {
            // Make sure the application layer does not have a problem with the selection.
            // If the application layer returns false, which it should if the event is not
            // defined, then accept the selection.
            eventData.idVendor          = pDesc->idVendor;              
            eventData.idProduct         = pDesc->idProduct;             
            eventData.bDeviceClass      = bClass;          
            eventData.bDeviceSubClass   = bSubClass;       
            eventData.bDeviceProtocol   = bProtocol;       

            if (!USB_HOST_APP_EVENT_HANDLER( USB_ROOT_HUB, EVENT_OVERRIDE_CLIENT_DRIVER_SELECTION,
                            &eventData, sizeof(USB_OVERRIDE_CLIENT_DRIVER_EVENT_DATA) ))
            {
                *pbClientDrv = usbTPL[i].ClientDriver;

#if defined (DEBUG_ENABLE)
                DEBUG_PutString( "HOST: Client driver found.\r\n" );
#endif

                return true;
            }    
        }
        i++;
    }

#if defined (DEBUG_ENABLE)
    DEBUG_PutString( "HOST: Client driver NOT found.\r\n" );
#endif

    return false;

} // _USB_FindClassDriver


/****************************************************************************
  Function:
    bool _USB_FindDeviceLevelClientDriver( void )

  Description:
    This function searches the TPL to try to find a device-level client
    driver.

  Precondition:
    * usbHostState == STATE_ATTACHED|SUBSTATE_VALIDATE_VID_PID
    * usbTPL must be define by the application.

  Parameters:
    None - None

  Return Values:
    true    - Client driver found
    false   - Client driver not found

  Remarks:
    If successful, this function preserves the client's index from the client
    driver table and sets flags indicating that the device should use a
    single client driver.
  ***************************************************************************/

bool _USB_FindDeviceLevelClientDriver( USB_DEVICE_DESCRIPTOR *pDesc )
{
    int i;
    
    USB_DEVICE_INFO *deviceInfo = usbHostInfo.pCurrentDeviceInfo;

    // Scan TPL
    i = 0;
    deviceInfo->flags.bfUseDeviceClientDriver = 0;
    deviceInfo->flags.bfUseEP0Driver = 0;
    while (i < NUM_TPL_ENTRIES)
    {
        if (usbTPL[i].flags.bfIsClassDriver)
        {
            // Check for a device-class client driver
            if ((usbTPL[i].device.bClass    == pDesc->bDeviceClass   ) &&
                (usbTPL[i].device.bSubClass == pDesc->bDeviceSubClass) &&
                (usbTPL[i].device.bProtocol == pDesc->bDeviceProtocol)   )
            {
#if defined (DEBUG_ENABLE)
                DEBUG_PutString( " -> Class driver\r\n" );
#endif

                deviceInfo->flags.bfUseDeviceClientDriver = 1;
            }
        }
        else
        {
            // Check for a device-specific client driver by VID & PID
            if ((usbTPL[i].device.idVendor  == pDesc->idVendor ) &&
                (usbTPL[i].device.idProduct == pDesc->idProduct))
            {
                if( usbTPL[i].flags.bfEP0OnlyCustomDriver == 1 )
                {
                    deviceInfo->flags.bfUseEP0Driver = 1;
                    deviceInfo->deviceEP0Driver = usbTPL[i].ClientDriver;

                    // Select configuration if it is given in the TPL
                    if (usbTPL[i].flags.bfSetConfiguration)
                    {
                        deviceInfo->currentConfigurationNumber = usbTPL[i].bConfiguration;
                    }
                }
                else
                {
#if defined (DEBUG_ENABLE)
                    DEBUG_PutString( " -> validated by VID/PID\r\n" );
#endif

                    deviceInfo->flags.bfUseDeviceClientDriver = 1;
                }
            }

#ifdef ALLOW_GLOBAL_VID_AND_PID
            if ((usbTPL[i].device.idVendor  == 0xFFFF) &&
                (usbTPL[i].device.idProduct == 0xFFFF))
            {
                USB_OVERRIDE_CLIENT_DRIVER_EVENT_DATA   eventData;
                     
                // Make sure the application layer does not have a problem with the selection.
                // If the application layer returns false, which it should if the event is not
                // defined, then accept the selection.
                eventData.idVendor          = pDesc->idVendor;              
                eventData.idProduct         = pDesc->idProduct;             
                eventData.bDeviceClass      = usbTPL[i].device.bClass;          
                eventData.bDeviceSubClass   = usbTPL[i].device.bSubClass;       
                eventData.bDeviceProtocol   = usbTPL[i].device.bProtocol;       
    
                if (!USB_HOST_APP_EVENT_HANDLER( USB_ROOT_HUB, EVENT_OVERRIDE_CLIENT_DRIVER_SELECTION,
                                &eventData, sizeof(USB_OVERRIDE_CLIENT_DRIVER_EVENT_DATA) ))
                {
#if defined (DEBUG_ENABLE)
                    DEBUG_PutString( " -> validated by special VID/PID\r\n" );
#endif

                    deviceInfo->flags.bfUseDeviceClientDriver = 1;
                }
            }    
#endif
        }

        if (deviceInfo->flags.bfUseDeviceClientDriver)
        {
            // Save client driver info
            deviceInfo->deviceClientDriver = usbTPL[i].ClientDriver;

            // Select configuration if it is given in the TPL
            if (usbTPL[i].flags.bfSetConfiguration)
            {
                deviceInfo->currentConfigurationNumber = usbTPL[i].bConfiguration;
            }
#if defined (DEBUG_ENABLE)
            DEBUG_PutString( "HOST: Device validated.\r\n" );
#endif

            return true;
        }

        i++;
    }

#if defined (DEBUG_ENABLE)
    DEBUG_PutString( "HOST: Device not yet validated\r\n" );
#endif

    return false;
}


/****************************************************************************
  Function:
    USB_ENDPOINT_INFO * _USB_FindEndpoint( )

  Description:
    This function searches the list of interfaces to try to find the specified
    endpoint.

  Precondition:
    None

  Parameters:
    USB_DEVICE_INFO *deviceInfo - Points to the desired device info
    uint8_t endpoint   - The endpoint to find.

  Returns:
    Returns a pointer to the USB_ENDPOINT_INFO structure for the endpoint.

  Remarks:
    None
  ***************************************************************************/

USB_ENDPOINT_INFO * _USB_FindEndpoint( USB_DEVICE_INFO *deviceInfo, uint8_t endpoint )
{
    if (endpoint == 0)
    {
        return usbHostInfo.pEndpoint0;
    }

    return USB_InterfaceList_FindEndpoint( deviceInfo->pInterfaceList, endpoint );
}


/****************************************************************************
  Function:
    void _USB_FreeMemory( void )

  Description:
    This function frees all memory that can be freed.  Only the EP0
    information block is retained.

  Precondition:
    None

  Parameters:
    None - None

  Returns:
    None

  Remarks:
    None
  ***************************************************************************/

void _USB_FreeMemory( void )
{
//    uint8_t    *pTemp;
    USB_DEVICE_INFO *deviceInfo = usbHostInfo.pCurrentDeviceInfo;

    USBStructConfigList_Clear(&deviceInfo->pConfigurationDescriptorList);

    USB_FREE_AND_CLEAR( usbHostInfo.pEP0Data );

    USB_InterfaceList_Clear(&deviceInfo->pInterfaceList);

//    pCurrentEndpoint = usbHostInfo.pEndpoint0;
}

/****************************************************************************
  Function:
    void _USB_NotifyClients( uint8_t address, USB_EVENT event, void *data,
                unsigned int size )

  Description:
    This routine notifies all active client drivers for the given device of
    the given event.

  Precondition:
    None

  Parameters:
    uint8_t address        - Address of the device generating the event
    USB_EVENT event     - Event ID
    void *data          - Pointer to event data
    unsigned int size   - Size of data pointed to by data

  Returns:
    None

  Remarks:
    When this driver is modified to support multiple devices, this function
    will require modification.
  ***************************************************************************/

void _USB_NotifyClients( USB_DEVICE_INFO *deviceInfo, USB_EVENT event, void *data, unsigned int size )
{
    USB_INTERFACE_INFO  *pInterface;

    // Some events go to all drivers, some only to specific drivers.
    switch(event)
    {
        case EVENT_TRANSFER:
        case EVENT_BUS_ERROR:
            if (((HOST_TRANSFER_DATA *)data)->clientDriver != CLIENT_DRIVER_HOST)
            {
                USBClientDriver_EventHandler(((HOST_TRANSFER_DATA *)data)->clientDriver, deviceInfo->deviceAddress, event, data, size);
            }
            break;
        default:
            pInterface = deviceInfo->pInterfaceList;
            while (pInterface != NULL)  // Scan the interface list for all active drivers.
            {
                USBClientDriver_EventHandler(pInterface->clientDriver, deviceInfo->deviceAddress, event, data, size);
                pInterface = pInterface->next;
            }

            if(deviceInfo->flags.bfUseEP0Driver)
            {
                USBClientDriver_EventHandler(deviceInfo->deviceEP0Driver, deviceInfo->deviceAddress, event, data, size);
            }
            break;
    }
} // _USB_NotifyClients

#if defined(USB_ENABLE_SOF_EVENT) && defined(USB_HOST_APP_DATA_EVENT_HANDLER)
/****************************************************************************
  Function:
    void _USB_NotifyClients( uint8_t address, USB_EVENT event, void *data,
                unsigned int size )

  Description:
    This routine notifies all active client drivers for the given device of
    the given event.

  Precondition:
    None

  Parameters:
    uint8_t address        - Address of the device generating the event
    USB_EVENT event     - Event ID
    void *data          - Pointer to event data
    unsigned int size   - Size of data pointed to by data

  Returns:
    None

  Remarks:
    When this driver is modified to support multiple devices, this function
    will require modification.
  ***************************************************************************/

void _USB_NotifyDataClients( uint8_t address, USB_EVENT event, void *data, unsigned int size )
{
    USB_INTERFACE_INFO  *pInterface;

    // Some events go to all drivers, some only to specific drivers.
    switch(event)
    {
        default:
            pInterface = usbDeviceInfo.pInterfaceList;
            while (pInterface != NULL)  // Scan the interface list for all active drivers.
            {
                USBClientDriver_DataEventHandler(pInterface->clientDriver, address, event, data, size);
                pInterface = pInterface->next;
            }
            break;
    }
} // _USB_NotifyDataClients
#endif

#if defined(USB_ENABLE_1MS_EVENT) && defined(USB_HOST_APP_DATA_EVENT_HANDLER)
/****************************************************************************
  Function:
    void _USB_NotifyAllDataClients( uint8_t address, USB_EVENT event, void *data,
                unsigned int size )

  Description:
    This routine notifies all client drivers (active or not) for the given device of
    the given event.

  Precondition:
    None

  Parameters:
    uint8_t address        - Address of the device generating the event
    USB_EVENT event     - Event ID
    void *data          - Pointer to event data
    unsigned int size   - Size of data pointed to by data

  Returns:
    None

  Remarks:
    When this driver is modified to support multiple devices, this function
    will require modification.
  ***************************************************************************/

void _USB_NotifyAllDataClients( uint8_t address, USB_EVENT event, void *data, unsigned int size )
{
    uint16_t i;

    // Some events go to all drivers, some only to specific drivers.
    switch(event)
    {
        default:
            for(i=0;i<NUM_CLIENT_DRIVER_ENTRIES;i++)
            {
                USBClientDriver_DataEventHandler(i, address, event, data, size);
            }
            break;
    }
} // _USB_NotifyClients
#endif

/****************************************************************************
  Function:
    bool _USB_ParseConfigurationDescriptor( )

  Description:
    This function parses all the endpoint descriptors for the required
    setting of the required interface and sets up the internal endpoint
    information.

  Precondition:
    The desriptor points to a valid Configuration Descriptor,
    which contains the endpoint descriptors.  The current
    interface and the current interface settings must be set up in
    usbDeviceInfo.

  Parameters:
    None - None

  Returns:
    true    - Successful
    false   - Configuration not supported.

  Remarks:
    * This function also automatically resets all endpoints (except
        endpoint 0) to DATA0, so _USB_ResetDATA0 does not have to be
        called.

    * If the configuration is not supported, the caller will need to clean
        up, freeing memory by calling _USB_InterfaceList_Clear.

    * We do not currently implement checks for descriptors that are shorter
        than the expected length, in the case of invalid USB Peripherals.

    * If there is not enough available heap space for storing the
        interface or endpoint information, this function will return false.
        Currently, there is no other mechanism for informing the user of
        an out of dynamic memory condition.

    * We are assuming that we can support a single interface on a single
        device.  When the driver is modified to support multiple devices,
        each endpoint should be checked to ensure that we have enough
        bandwidth to support it.
  ***************************************************************************/

bool _USB_ParseConfigurationDescriptor( uint8_t *descriptor )
{
    USB_DEVICE_INFO *deviceInfo = usbHostInfo.pCurrentDeviceInfo;
    bool                           error;

#if 0
    uint8_t                        bLength;
    uint8_t                        bDescriptorType;
    uint8_t                        bClass;
    uint8_t                        bSubClass;
    uint8_t                        bProtocol;
    uint8_t                        bClientDriver;
#endif
    uint8_t                        bNumInterfaces;
    uint8_t                        bAlternateSetting;
    uint8_t                        bNumEndpoints;
    uint8_t                        bMaxPower;
    uint16_t                       wTotalLength;

    uint8_t                        bInterfaceNumber;
    uint8_t                        bClientDriver;

    uint8_t                        currentAlternateSetting;
    uint8_t                        currentConfigurationValue;
    uint8_t                        currentEndpoint;
    uint8_t                        currentInterface;
    uint16_t                       index;
    USB_ENDPOINT_INFO             *newEndpointInfo;
    USB_INTERFACE_INFO            *newInterfaceInfo;
    USB_INTERFACE_SETTING_INFO    *newSettingInfo;
    USB_VBUS_POWER_EVENT_DATA      powerRequest;
    USB_INTERFACE_INFO            *pTempInterfaceList;
    USB_ENDPOINT_DATA             *ptr;
    

    // Prime the loops.
    currentEndpoint         = 0;
    error                   = false;
    index                   = 0;
    ptr                     = (USB_ENDPOINT_DATA *)descriptor;
    currentInterface        = 0;
    currentAlternateSetting = 0;
    // Don't set until everything is in place.
    // Class dependent descriptor is not set in it.
    pTempInterfaceList      = deviceInfo->pInterfaceList;

    // Assume no OTG support (determine otherwise, below).
    usbHostInfo.flags.bfSupportsOTG   = 0;
    usbHostInfo.flags.bfConfiguredOTG = 1;

    #ifdef USB_SUPPORT_OTG
        usbDeviceInfo.flags.bfAllowHNP = 1;  //Allow HNP From Host
    #endif

    // Load up the values from the Configuration Descriptor
//    bLength              = *ptr++;
//    bDescriptorType      = *ptr++;
//    wTotalLength         = *ptr++;           // In case these are not word aligned
//    wTotalLength        += (*ptr++) << 8;
//    bNumInterfaces       = *ptr++;
//    currentConfiguration = *ptr++;  // bConfigurationValue
//                            ptr++;  // iConfiguration
//                            ptr++;  // bmAttributes
//    bMaxPower            = *ptr;
    wTotalLength = ptr->cd.wTotalLength;
    bNumInterfaces = ptr->cd.bNumInterfaces;
    currentConfigurationValue = ptr->cd.bConfigurationValue;
    bMaxPower              = ptr->cd.bMaxPower;

    // Check Max Power to see if we can support this configuration.
    powerRequest.current = bMaxPower;
    powerRequest.port    = 0;        // Port 0
    if (!USB_HOST_APP_EVENT_HANDLER( USB_ROOT_HUB, EVENT_VBUS_REQUEST_POWER,
            &powerRequest, sizeof(USB_VBUS_POWER_EVENT_DATA) ))
    {
        usbHostInfo.errorCode = USB_ERROR_INSUFFICIENT_POWER;
        error = true;
    }

    // Skip over the rest of the Configuration Descriptor
    index += ptr->cd.bLength;
    ptr    = (USB_ENDPOINT_DATA *)&descriptor[index];

    while (!error && (index < wTotalLength))
    {
        // Check the descriptor length and type
//        bLength         = *ptr++;
//        bDescriptorType = *ptr++;

#ifdef USB_SUPPORT_OTG
        // Find the OTG discriptor (if present)
        if (bDescriptorType == USB_DESCRIPTOR_OTG)
        {
            // We found an OTG Descriptor, so the device supports OTG.
            usbDeviceInfo.flags.bfSupportsOTG = 1;
            usbDeviceInfo.attributesOTG       = *ptr;

            // See if we need to send the SET FEATURE command.  If we do,
            // clear the bConfiguredOTG flag.
            if ( (usbDeviceInfo.attributesOTG & OTG_HNP_SUPPORT) && (usbDeviceInfo.flags.bfAllowHNP))
            {
                usbDeviceInfo.flags.bfConfiguredOTG = 0;
            }
            else
            {
                usbDeviceInfo.flags.bfAllowHNP = 0;
            }
        }
#endif

        // Find an interface descriptor
        if (ptr->d.bDescriptorType != USB_DESCRIPTOR_INTERFACE)
        {
            // Skip over the rest of the Descriptor
            index += ptr->d.bLength;
            ptr = (USB_ENDPOINT_DATA *)&descriptor[index];
        }
        else
        {
            // Read some data from the interface descriptor
//            bInterfaceNumber  = *ptr++;
//            bAlternateSetting = *ptr++;
//            bNumEndpoints     = *ptr++;
//            bClass            = *ptr++;
//            bSubClass         = *ptr++;
//            bProtocol         = *ptr++;
            bInterfaceNumber  = ptr->id.bInterfaceNumber;
            bAlternateSetting = ptr->id.bAlternateSetting;
            bNumEndpoints     = ptr->id.bNumEndpoints;

            // Get client driver index
            if (deviceInfo->flags.bfUseDeviceClientDriver)
            {
                bClientDriver = deviceInfo->deviceClientDriver;
            }
            else
            {
                if (!_USB_FindClassDriver(ptr->id.bInterfaceClass, ptr->id.bInterfaceSubClass, ptr->id.bInterfaceProtocol, &bClientDriver))
                {
                    // If we cannot support this interface, skip it.
                    index += ptr->id.bLength;
                    ptr = (USB_ENDPOINT_DATA *)&descriptor[index];
                    continue;
                }
            }

            // We can support this interface.  See if we already have a USB_INTERFACE_INFO node for it.
            newInterfaceInfo = pTempInterfaceList;
            while ((newInterfaceInfo != NULL) && (newInterfaceInfo->interface != bInterfaceNumber))
            {
                newInterfaceInfo = newInterfaceInfo->next;
            }
            if (newInterfaceInfo == NULL)
            {
                // This is the first instance of this interface, so create a new node for it.
                if ((newInterfaceInfo = (USB_INTERFACE_INFO *)USB_MALLOC( sizeof(USB_INTERFACE_INFO) )) == NULL)
                {
                    // Out of memory
                    error = true; 
                      
                }

                if(error == false)
                {
                    // Initialize the interface node
                    newInterfaceInfo->interface             = bInterfaceNumber;
                    newInterfaceInfo->clientDriver          = bClientDriver;
                    newInterfaceInfo->pInterfaceSettings    = NULL;
                    newInterfaceInfo->pCurrentSetting       = NULL;
    
                    // Insert it into the list.
                    newInterfaceInfo->next                  = pTempInterfaceList;
                    pTempInterfaceList                      = newInterfaceInfo;
                }
            }

            if (!error)
            {
                // Create a new setting for this interface, and add it to the list.
                if ((newSettingInfo = (USB_INTERFACE_SETTING_INFO *)USB_MALLOC( sizeof(USB_INTERFACE_SETTING_INFO) )) == NULL)
                {
                    // Out of memory
                    error = true;   
                }
            }    
             
            if (!error)   
            {
                newSettingInfo->next                    = newInterfaceInfo->pInterfaceSettings;
                newSettingInfo->interfaceAltSetting     = bAlternateSetting;
                newSettingInfo->pEndpointList           = NULL;
                newInterfaceInfo->pInterfaceSettings    = newSettingInfo;
                if (bAlternateSetting == 0)
                {
                    newInterfaceInfo->pCurrentSetting   = newSettingInfo;
                }

                // Skip over the rest of the Interface Descriptor
                index += ptr->id.bLength;
                ptr = (USB_ENDPOINT_DATA *)&descriptor[index];

                // Find the Endpoint Descriptors.  There might be Class and Vendor descriptors in here
                currentEndpoint = 0;
                while (!error && (index < wTotalLength) && (currentEndpoint < bNumEndpoints))
                {
//                    bLength = *ptr++;
//                    bDescriptorType = *ptr++;

                    if (ptr->d.bDescriptorType != USB_DESCRIPTOR_ENDPOINT)
                    {
                        // Skip over the rest of the Descriptor
                        index += ptr->d.bLength;
                        ptr = (USB_ENDPOINT_DATA *)&descriptor[index];
                    }
                    else
                    {
                        // Create an entry for the new endpoint.
                        if ((newEndpointInfo = (USB_ENDPOINT_INFO *)USB_MALLOC( sizeof(USB_ENDPOINT_INFO) )) == NULL)
                        {
                            // Out of memory
                            error = true;   
                        }
//                        newEndpointInfo->bEndpointAddress           = *ptr++;
//                        newEndpointInfo->bmAttributes.val           = *ptr++;
//                        newEndpointInfo->wMaxPacketSize             = *ptr++;
//                        newEndpointInfo->wMaxPacketSize            += (*ptr++) << 8;
//                        newEndpointInfo->wInterval                  = *ptr++;
                        newEndpointInfo->bEndpointAddress           = ptr->ed.bEndpointAddress;
                        newEndpointInfo->bmAttributes.val           = ptr->ed.bmAttributes;
#if 0
                        // Why exception occur at this line? implementation specfic error?
                        newEndpointInfo->wMaxPacketSize             = ptr->ed.wMaxPacketSize;
#else
                        newEndpointInfo->wMaxPacketSize             = ptr->b[4];
                        newEndpointInfo->wMaxPacketSize             += ((uint16_t)ptr->b[5] << 8);
#endif
                        newEndpointInfo->wInterval                  = ptr->ed.bInterval;
                        newEndpointInfo->status.val                 = 0x00;
                        newEndpointInfo->status.bfUseDTS            = 1;
                        newEndpointInfo->status.bfTransferComplete  = 1;  // Initialize to success to allow preprocessing loops.
                        newEndpointInfo->dataCount                  = 0;  // Initialize to 0 since we set bfTransferComplete.
                        newEndpointInfo->transferState              = TSTATE_IDLE;
                        newEndpointInfo->clientDriver               = bClientDriver;

                        // Special setup for isochronous endpoints.
#ifdef USB_SUPPORT_ISOCHRONOUS_TRANSFERS
                        if (newEndpointInfo->bmAttributes.bfTransferType == USB_TRANSFER_TYPE_ISOCHRONOUS)
                        {
                            // Validate and convert the interval to the number of frames.  The value must
                            // be between 1 and 16, and the frames is 2^(bInterval-1).
                            if (newEndpointInfo->wInterval == 0) newEndpointInfo->wInterval = 1;
                            if (newEndpointInfo->wInterval > 16) newEndpointInfo->wInterval = 16;
                            newEndpointInfo->wInterval = 1 << (newEndpointInfo->wInterval-1);

                            // Disable DTS
                            newEndpointInfo->status.bfUseDTS = 0;
                        }
#endif
                        // Initialize interval count
                        newEndpointInfo->wIntervalCount = newEndpointInfo->wInterval;

                        // Put the new endpoint in the list.
                        newEndpointInfo->next           = newSettingInfo->pEndpointList;
                        newSettingInfo->pEndpointList   = newEndpointInfo;

                        // When multiple devices are supported, check the available
                        // bandwidth here to make sure that we can support this
                        // endpoint.

                        // Get ready for the next endpoint.
                        currentEndpoint++;
                        index += ptr->ed.bLength;
                        ptr = (USB_ENDPOINT_DATA *)&descriptor[index];
                    }
                }
            }    

            // Ensure that we found all the endpoints for this interface.
            if (currentEndpoint != bNumEndpoints)
            {
                error = true;
            }
        }
    }

    // Ensure that we found all the interfaces in this configuration.
    // This is a nice check, but some devices have errors where they have a
    // different number of interfaces than they report they have!
//    if (currentInterface != bNumInterfaces)
//    {
//        error = true;
//    }

    if (pTempInterfaceList == NULL)
    {
        // We could find no supported interfaces.
#if defined (DEBUG_ENABLE)
        UART_PutString( "HOST: No supported interfaces.\r\n" );
#endif
        error = true;
    }

    if (error)
    {
        // Destroy whatever list of interfaces, settings, and endpoints we created.
        // The "new" variables point to the current node we are trying to remove.
        USB_InterfaceList_Clear(&pTempInterfaceList);

        return false;
    }
    else
    {    
        // Set configuration.
        deviceInfo->currentConfigurationNumber = currentConfigurationValue;
        deviceInfo->currentConfigurationPower = bMaxPower;
    
        // Success!
#if defined (DEBUG_ENABLE)
        UART_PutString( "HOST: Parse Descriptor success\r\n" );
#endif

        deviceInfo->pInterfaceList = pTempInterfaceList;
        return true;
    }    
}

/****************************************************************************
  Function:
    void _USB_ResetDATA0( uint8_t endpoint )

  Description:
    This function resets DATA0 for the specified endpoint.  If the
    specified endpoint is 0, it resets DATA0 for all endpoints.

  Precondition:
    None

  Parameters:
    uint8_t endpoint   - Endpoint number to reset.


  Returns:
    None

  Remarks:
    None
  ***************************************************************************/

void _USB_ResetDATA0( USB_DEVICE_INFO *deviceInfo, uint8_t endpoint )
{
    USB_ENDPOINT_INFO   *pEndpoint;

    if (endpoint == 0)
    {
        // Reset DATA0 for all endpoints.
        USB_InterfaceList_SetData0( deviceInfo->pInterfaceList );
    }
    else
    {
        pEndpoint = _USB_FindEndpoint( deviceInfo, endpoint );
        if (pEndpoint)
        {
            pEndpoint->status.bfNextDATA01 = 0;
        }
    }
}

/****************************************************************************
  Function:
    bool _USB_TransferInProgress( void )

  Description:
    This function checks to see if any read or write transfers are in
    progress.

  Precondition:
    None

  Parameters:
    None - None

  Returns:
    true    - At least one read or write transfer is occurring.
    false   - No read or write transfers are occurring.

  Remarks:
    None
  ***************************************************************************/

bool _USB_TransferInProgress( void )
{
    USB_DEVICE_INFO *deviceInfo = usbHostInfo.pCurrentDeviceInfo;
    USB_ENDPOINT_INFO           *pEndpoint;
    USB_INTERFACE_INFO          *pInterface;
    USB_INTERFACE_SETTING_INFO  *pSetting;

    // Check EP0.
    if (!usbHostInfo.pEndpoint0->status.bfTransferComplete)
    {
        return true;
    }

    // Check all of the other endpoints.
    pInterface = deviceInfo->pInterfaceList;
    while (pInterface)
    {
        pSetting = pInterface->pInterfaceSettings;
        while (pSetting)
        {
            pEndpoint = pSetting->pEndpointList;
            while (pEndpoint)
            {
                if (!pEndpoint->status.bfTransferComplete)
                {
                    return true;
                }
                pEndpoint = pEndpoint->next;
            }
            pSetting = pSetting->next;
        }
        pInterface = pInterface->next;
    }

    return false;
}

/****************************************************************************
  Function:
    void USBHost_AttachDeviceOnHUB( )

  Summary:
    This is the routine to process attaching a device on the hub

  Description:

  Precondition:

  Parameters:
    hubAddress  - device address of the HUB
    portNumber  - port number on the HUB
    deviceSpeed - 0 : Full-speed
                  1 : Low-speed

  Returns:
    0 - State is not changed
    1 - State is successflly changed

  Remarks:
    None
  ***************************************************************************/

uint8_t USBHost_AttachDeviceOnHUB( uint8_t hubAddress, uint8_t portNumber, uint8_t deviceSpeed )
{
    if (usbHostState == STATE_RUNNING)
    {
        // Set device descriptor
        usbOverrideHostState = STATE_ATTACHED | SUBSTATE_GET_DEVICE_DESCRIPTOR_SIZE;
        usbDeviceInfos[0].hubAddress = hubAddress;
        usbDeviceInfos[0].portNumber = portNumber;
        if (deviceSpeed) {
            // low-speed
            usbDeviceInfos[0].deviceSpeed |= 0x40;
        } else {
            // full-speed
            usbDeviceInfos[0].deviceSpeed &= ~0x40;
        }
        return 1;
    }
    return 0;
}

/****************************************************************************
  Function:
    void USBHost_DetachDeviceOnHUB( )

  Summary:
    This is the routine to process attaching a device on the hub

  Description:

  Precondition:

  Parameters:
    hubAddress - set address if the device connected on a hub, otherwise 0
    deviceAddress - device address

  Returns:
    0 - State is not changed
    1 - State is successflly changed

  Remarks:
    None
  ***************************************************************************/

void USBHost_DetachDeviceOnHUB( uint8_t hubAddress, uint8_t deviceAddress )
{
#if defined (DEBUG_ENABLE)
    UART_PutStringHexU8( "HOST: HUB addr: ", hubAddress );
    UART_PutStringHexU8( "HOST: Detach on HUB: ", deviceAddress );
#endif

    if (deviceAddress > 0) {
        USB_DEVICE_INFO *deviceInfo = &usbDeviceInfos[deviceAddress];

        // device dependent class (HID etc.)
        _USB_NotifyClients( deviceInfo,
            EVENT_DETACH,
            &deviceInfo->deviceAddress,
            sizeof(uint8_t)
        );

        USBHostDeviceInfos_Clear(deviceAddress);
    }

    // hub class
    if (hubAddress) {
        // nofity hub class
        _USB_NotifyClients( &usbDeviceInfos[hubAddress],
                EVENT_UNSUPPORTED_DEVICE,
                &deviceAddress,
                sizeof(deviceAddress)
        );
    }
}


// *****************************************************************************
// *****************************************************************************
// Section: Interrupt Handlers
// *****************************************************************************

/****************************************************************************/
static __inline__ void USB_HostInterrupt_Timer(void)
{
    // The interrupt is cleared by writing a '1' to it.
    U1OTGIR = U1OTGIE_INTERRUPT_T1MSECIF;

#if defined(USB_ENABLE_1MS_EVENT) && defined(USB_HOST_APP_DATA_EVENT_HANDLER)
    msec_count++;

    //Notify ping all client drivers of 1MSEC event (address, event, data, sizeof_data)
    _USB_NotifyAllDataClients(0, EVENT_1MS, (void*)&msec_count, 0);
#endif

#if defined (DEBUG_ENABLE)
    DEBUG_PutChar('~');
#endif

#ifdef  USB_SUPPORT_OTG
    if (USBOTGGetSRPTimeOutFlag())
    {
        if (USBOTGIsSRPTimeOutExpired())
        {
            USB_OTGEventHandler(0,OTG_EVENT_SRP_FAILED,0,0);
        }

    }

    else if (USBOTGGetHNPTimeOutFlag())
    {
        if (USBOTGIsHNPTimeOutExpired())
        {
            USB_OTGEventHandler(0,OTG_EVENT_HNP_FAILED,0,0);
        }

    }

    else
#endif
    {
        if(usbHostTimer.numTimerInterrupts != 0)
        {
            usbHostTimer.numTimerInterrupts--;

            if (usbHostTimer.numTimerInterrupts == 0)
            {
                //If we aren't using the 1ms events, then turn of the interrupt to
                // save CPU time
                #if !defined(USB_ENABLE_1MS_EVENT)
                    // Turn off the timer interrupt.
                    U1OTGIEbits.T1MSECIE = 0;
                #endif

                if (usbHostTimer.handler) {
                    usbHostTimer.handler();
                } else {
                    if((usbHostState & STATE_MASK) != STATE_DETACHED)
                    {
                        // Advance to the next state.  We can do this here, because the only time
                        // we'll get a timer interrupt is while we are in one of the holding states.
                        _USB_SetNextSubSubState();
                    }
                }
            }
        }
    }
}

/****************************************************************************/
static void USB_HostInterrupt_Attach(void)
{
#if defined (DEBUG_ENABLE)
    DEBUG_PutChar( '[' );
#endif

    // The attach interrupt is level, not edge, triggered.  If we clear it, it just
    // comes right back.  So clear the enable instead
    U1IEbits.ATTACHIE   = 0;
    U1IR                = U1IE_INTERRUPT_ATTACH;

    if (usbHostState == (STATE_DETACHED | SUBSTATE_WAIT_FOR_DEVICE))
    {
        usbOverrideHostState = STATE_ATTACHED;
    }

#ifdef  USB_SUPPORT_OTG
    //If HNP Related Attach, Process Connect Event
    USB_OTGEventHandler(0, OTG_EVENT_CONNECT, 0, 0 );

    //If SRP Related A side D+ High, Process D+ High Event
    USB_OTGEventHandler (0, OTG_EVENT_SRP_DPLUS_HIGH, 0, 0 );

    //If SRP Related B side Attach
    USB_OTGEventHandler (0, OTG_EVENT_SRP_CONNECT, 0, 0 );
#endif
}

/****************************************************************************/
static void USB_HostInterrupt_Detach(void)
{
#if defined (DEBUG_ENABLE)
    DEBUG_PutString( "Detach\r\n" );
#endif

    U1IR                    = U1IE_INTERRUPT_DETACH;
    U1IEbits.DETACHIE       = 0;
    usbOverrideHostState    = STATE_DETACHED;

#ifdef  USB_SUPPORT_OTG
    //If HNP Related Detach Detected, Process Disconnect Event
    USB_OTGEventHandler (0, OTG_EVENT_DISCONNECT, 0, 0 );

    //If SRP Related D+ Low and SRP Is Active, Process D+ Low Event
    USB_OTGEventHandler (0, OTG_EVENT_SRP_DPLUS_LOW, 0, 0 );

    //Disable HNP, Detach Interrupt Could've Triggered From Cable Being Unplugged
    USBOTGDisableHnp();
#endif
}

/****************************************************************************
  Function:
    void USB_HostInterruptHandler( void )

  Summary:
    This is the interrupt service routine for the USB interrupt.

  Description:
    This is the interrupt service routine for the USB interrupt.  The
    following cases are serviced:
         * Device Attach
         * Device Detach
         * One millisecond Timer
         * Start of Frame
         * Transfer Done
         * USB Error

  Precondition:
    In TRNIF handling, pCurrentEndpoint is still pointing to the last
    endpoint to which a token was sent.

  Parameters:
    None - None

  Returns:
    None

  Remarks:
    None
  ***************************************************************************/

void USB_HostInterruptHandler(void)
{
#if defined( __PIC32__)
    _ClearUSBIF();
#else
    #error "Cannot clear USB interrupt."
#endif

    // -------------------------------------------------------------------------
    // One Millisecond Timer ISR
    if (U1OTGIEbits.T1MSECIE && U1OTGIRbits.T1MSECIF)
    {
        USB_HostInterrupt_Timer();
    }

    // -------------------------------------------------------------------------
    // Attach ISR

    // The attach interrupt is level, not edge, triggered.  So make sure we have it enabled.
    if (U1IEbits.ATTACHIE && U1IRbits.ATTACHIF)
    {
        USB_HostInterrupt_Attach();
    }

    // -------------------------------------------------------------------------
    // Detach ISR

    if (U1IEbits.DETACHIE && U1IRbits.DETACHIF)
    {
        USB_HostInterrupt_Detach();
    }

#ifdef USB_SUPPORT_OTG

    // -------------------------------------------------------------------------
    //ID Pin Change ISR
    if (U1OTGIRbits.IDIF && U1OTGIEbits.IDIE)
    {
         USBOTGInitialize();

         //Clear Interrupt Flag
         U1OTGIR = 0x80;
    }

    // -------------------------------------------------------------------------
    //VB_SESS_END ISR
    if (U1OTGIRbits.SESENDIF && U1OTGIEbits.SESENDIE)
    {
        //If B side Host And Cable Was Detached Then
        if (U1OTGSTATbits.ID == CABLE_B_SIDE && USBOTGCurrentRoleIs() == ROLE_HOST)
        {
            //Reinitialize
            USBOTGInitialize();
        }

        //Clear Interrupt Flag
        U1OTGIR = 0x04;
    }

    // -------------------------------------------------------------------------
    //VA_SESS_VLD ISR
    if (U1OTGIRbits.SESVDIF && U1OTGIEbits.SESVDIE)
    {
        //If A side Host and SRP Is Active Then
        if (USBOTGDefaultRoleIs() == ROLE_HOST && USBOTGSrpIsActive())
        {
            //If VBUS > VA_SESS_VLD Then
            if (U1OTGSTATbits.SESVD == 1)
            {
                //Process SRP VBUS High Event
                USB_OTGEventHandler (0, OTG_EVENT_SRP_VBUS_HIGH, 0, 0 );
            }

            //If VBUS < VA_SESS_VLD Then
            else
            {
                 //Process SRP Low Event
                USB_OTGEventHandler (0, OTG_EVENT_SRP_VBUS_LOW, 0, 0 );
            }
        }

        U1OTGIR = 0x08;
    }

    // -------------------------------------------------------------------------
    //Resume Signaling for Remote Wakeup
    if (U1IRbits.RESUMEIF && U1IEbits.RESUMEIE)
    {
        //Process SRP VBUS High Event
        USB_OTGEventHandler (0, OTG_EVENT_RESUME_SIGNALING,0, 0 );

        //Clear Resume Interrupt Flag
        U1IR = 0x20;
    }
#endif


    // -------------------------------------------------------------------------
    // Transfer Done ISR - only process if there was no error

    if ((U1IEbits.TRNIE && U1IRbits.TRNIF) &&
        (!(U1IEbits.UERRIE && U1IRbits.UERRIF)
#ifdef USB_SUPPORT_ISOCHRONOUS_TRANSFERS
            || (pCurrentEndpoint->bmAttributes.bfTransferType == USB_TRANSFER_TYPE_ISOCHRONOUS)
#endif
        )
    ) {
        USB_HostInterrupt_Transfer();
    } // U1IRbits.TRNIF


    // -------------------------------------------------------------------------
    // Start-of-Frame ISR

    if (U1IEbits.SOFIE && U1IRbits.SOFIF)
    {
        USB_HostInterrupt_SOF();
    }

    // -------------------------------------------------------------------------
    // USB Error ISR

    if (U1IEbits.UERRIE && U1IRbits.UERRIF)
    {
        USB_HostInterrupt_Error();
    }

#ifdef DEBUG_ENABLE
    if (U1IE & U1IR) {
        DEBUG_PutStringHexU8( "Intr: ", U1IE & U1IR );
    }
#endif
}

/*************************************************************************
 * EOF usb_host.c
 */

