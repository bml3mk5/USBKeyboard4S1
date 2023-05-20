/** @file usb_host_hub.c
 *
 *  @author Sasaji
 *  @date   2023/04/07
 *
 * 	@brief  Treat HUB class 
 */

#include <xc.h>
#include "../common.h"
#include "usb_ch9.h"
#include "usb_common.h"
#include "usb_host_hub.h"
#include "usb.h"
#include "usb_host.h"
#include "usb_host_local.h"
#include "system.h"
#include <string.h>
#include "../uart.h"

//------------------------------------------------------------------------------
// state machine on the HUB class
typedef enum _USB_STATE_HUB {
    STATE_HUB_NONE = 0,
    STATE_HUB_WAIT_GET_HUB_DESCRIPTOR,
    STATE_HUB_IDLE,
    STATE_HUB_WAIT_PORT_POWER_ON,
    STATE_HUB_REQ_GET_STATUS_CHANGE,
    STATE_HUB_WAIT_GET_STATUS_CHANGE,
    STATE_HUB_CONT_STATUS_CHANGE,
    STATE_HUB_WAIT_GET_HUB_STATUS,
    STATE_HUB_WAIT_GET_PORT_STATUS,
//    STATE_HUB_REQ_PORT_RESET,
    STATE_HUB_WAIT_PORT_RESET,
//    STATE_HUB_REQ_CLEAR_C_PORT_RESET,
    STATE_HUB_WAIT_CLEAR_C_PORT_RESET,
//    STATE_HUB_REQ_CLEAR_C_PORT_CONNECT,
    STATE_HUB_WAIT_CLEAR_C_PORT_CONNECT,
    STATE_HUB_WAIT_CLEAR_PORT_CONNECT,
    STATE_HUB_REQ_COMMAND,
    STATE_HUB_WAIT_COMMAND,
    STATE_HUB_REQ_PORT_SETTING_DEVICE,
    STATE_HUB_WAIT_PORT_SETTING_DEVICE,
} USB_STATE_HUB;

//------------------------------------------------------------------------------
// Hub Class Feature Selectors
enum _USB_HUB_FEATURE_SELECT {
    FS_C_HUB_LOCAL_POWER = 0,
    FS_C_HUB_OVER_CURRENT = 1,
    FS_PORT_CONNECTION = 0,
    FS_PORT_ENABLE = 1,
    FS_PORT_SUSPEND = 2,
    FS_PORT_OVER_CURRENT = 3,
    FS_PORT_RESET = 4,
    FS_PORT_POWER = 8,
    FS_PORT_LOW_SPEED = 9,
    FS_C_PORT_CONNECTION = 16,
    FS_C_PORT_ENABLE = 17,
    FS_C_PORT_SUSPEND = 18,
    FS_C_PORT_OVER_CURRENT = 19,
    FS_C_PORT_RESET = 20,
    FS_PORT_TEST = 21,
    FS_PORT_INDICATOR = 22
};

//Port Status Field
enum ENUM_USB_HUB_PORT_STATUS {
    // lower byte d7-d0
    PS_PORT_CONNECTION = 0x01,
    PS_PORT_ENABLE = 0x02,
    PS_PORT_SUSPEND = 0x04,
    PS_PORT_OVER_CURRENT = 0x08,
    PS_PORT_RESET = 0x10,
    // upper byte d15-d8
    PS_PORT_POWER = 0x0100,
    PS_PORT_LOW_SPEED = 0x0200,
    PS_PORT_HIGH_SPEED = 0x0400,
    PS_PORT_TEST = 0x0800,
    PS_PORT_INDICATOR = 0x1000
};

//------------------------------------------------------------------------------
typedef struct ST_USB_HUB_PORT_STATUS {
    uint16_t current_status;
    uint16_t changed_status;
} USB_HUB_PORT_STATUS;

//------------------------------------------------------------------------------
/*  USB HUB Interface Information

   This structure is used to hold interface specific information.
*/
typedef struct _USB_HUB_INTERFACE_DETAILS
{
//    struct _USB_HID_INTERFACE_DETAILS   *next;                // Pointer to next interface in the list.
//    uint16_t                            sizeOfRptDescriptor;  // Size of report descriptor of a particular interface.
    USB_ENDPOINT_INFO                  *endpointIN;           // HID IN endpoint for corresponding interface.
    USB_ENDPOINT_INFO                  *endpointOUT;          // HID OUT endpoint for corresponding interface.
    uint16_t                            endpointMaxDataSize;  // Max data size for a interface.
    uint8_t                             interfaceNumber;      // Interface number.
    uint8_t                             endpointPollInterval; // Polling rate of corresponding interface.
}   USB_HUB_INTERFACE_DETAILS;

//------------------------------------------------------------------------------
/*  USB HUB Port Information
*/
typedef struct _USB_HUB_PORT_INFO
{
    uint8_t                             deviceAddress;          // Address of the device on attached port
} USB_HUB_PORT_INFO;

//------------------------------------------------------------------------------
typedef struct _USB_HUB_TRANS_PARAMETER
{
    SETUP_PKT               packet;
    volatile USB_STATE_HUB  nextState;
} USB_HUB_TRANS_PARAMETER;

//------------------------------------------------------------------------------

#define USB_HUB_BUFFER_SIZE         64

/*  USB HUB Device Information

   This structure is used to hold information about the entire device.
*/
typedef struct _USB_HUB_DEVICE_INFO
{
    uint8_t                             deviceAddress;          // Address of the device on the USB
    uint8_t                             clientDriverID;         // Client driver ID for device requests
    uint8_t                             numOfInterfaces;        // Total number of interfaces in the device.
    uint8_t                             numOfPorts;             // Total number of ports on the hub.
    uint32_t                            portStatus;             // status of each ports
    uint32_t                            portStatusMask;         // Mask of ports
    volatile USB_STATE_HUB              state;                  // State machine state of the device.
    USB_DEVICE_INFO                    *pDeviceInfo;            // Device Information (after decided address)
    USB_HUB_INTERFACE_DETAILS          *pInterfaceDetails;      // Interface details
    USB_HUB_PORT_INFO                  *portInfo;               // Information on each ports
    uint8_t                             currentPortNumber;      // Current processing port number
    uint8_t                             waitTimerFlag;          // wait timer
    uint16_t                            bPwrOn2PwrGood;         // time until power is good on that port.
    uint16_t                            currentPortStatus;      // status of current port
    uint16_t                            currentPortChangeStatus; // status of current port
    USB_HUB_TRANS_PARAMETER             param;                  // parameters using CLEAR_FEATURE/SET_FEATURE/GET_STATUS
    uint8_t                            *buffer;
} USB_HUB_DEVICE_INFO;

//******************************************************************************
//******************************************************************************
// Section: HUB Host Global Variables
//******************************************************************************
//******************************************************************************

static USB_HUB_DEVICE_INFO          deviceInfoHUB[USB_MAX_HUB_DEVICES] __attribute__ ((aligned));

#define P_PORT_STATUS(x) ((USB_HUB_PORT_STATUS *)(x))

/*******************************************************************************
  Function:
    bool USBHostHUB(  )

  Summary:
    Constructor

  Description:
    Clear static variables

  Precondition:
    None

  Parameters:
    None

  Return Values:
    None

  Remarks:
    None
*******************************************************************************/
void USBHostHUB( void )
{
    memset(deviceInfoHUB, 0, sizeof(deviceInfoHUB));
}

// *****************************************************************************
static USB_HUB_DEVICE_INFO *USBHostHUB_GetInstance( uint8_t address )
{
    USB_HUB_DEVICE_INFO *match = NULL;
    for (int device = 0; device < USB_MAX_HUB_DEVICES; device++)
    {
        if(deviceInfoHUB[device].deviceAddress == address) {
            match = &deviceInfoHUB[device];
            break;
        }
    }
    return match;
}

// *****************************************************************************
static void USBHostHUBClearFeature(USB_HUB_DEVICE_INFO *infoHUB, uint16_t feature, uint16_t port, USB_STATE_HUB next)
{
    // port 0 mean HUB
    infoHUB->param.packet.bmRequestType =
            (port ? USB_SETUP_HOST_TO_DEVICE | USB_SETUP_TYPE_CLASS | USB_SETUP_RECIPIENT_OTHER 
                  : USB_SETUP_HOST_TO_DEVICE | USB_SETUP_TYPE_CLASS | USB_SETUP_RECIPIENT_DEVICE);
    infoHUB->param.packet.bRequest = USB_REQUEST_CLEAR_FEATURE; 
    infoHUB->param.packet.wValue = feature;
    infoHUB->param.packet.wIndex = port;
    infoHUB->param.packet.wLength = 0;
    infoHUB->param.nextState = next;

    infoHUB->state = STATE_HUB_REQ_COMMAND;
}

// *****************************************************************************
static void USBHostHUBSetFeature(USB_HUB_DEVICE_INFO *infoHUB, uint16_t feature, uint16_t port, USB_STATE_HUB next)
{
    // port 0 mean HUB
    infoHUB->param.packet.bmRequestType =
            (port ? USB_SETUP_HOST_TO_DEVICE | USB_SETUP_TYPE_CLASS | USB_SETUP_RECIPIENT_OTHER 
                  : USB_SETUP_HOST_TO_DEVICE | USB_SETUP_TYPE_CLASS | USB_SETUP_RECIPIENT_DEVICE);
    infoHUB->param.packet.bRequest = USB_REQUEST_SET_FEATURE; 
    infoHUB->param.packet.wValue = feature;
    infoHUB->param.packet.wIndex = port;
    infoHUB->param.packet.wLength = 0;
    infoHUB->param.nextState = next;

    infoHUB->state = STATE_HUB_REQ_COMMAND;
}

// *****************************************************************************
static void USBHostHUBGetStatus(USB_HUB_DEVICE_INFO *infoHUB, uint16_t port, USB_STATE_HUB next)
{
    // port 0 mean HUB
    infoHUB->param.packet.bmRequestType =
            (port ? USB_SETUP_DEVICE_TO_HOST | USB_SETUP_TYPE_CLASS | USB_SETUP_RECIPIENT_OTHER 
                  : USB_SETUP_DEVICE_TO_HOST | USB_SETUP_TYPE_CLASS | USB_SETUP_RECIPIENT_DEVICE);
    infoHUB->param.packet.bRequest = USB_REQUEST_GET_STATUS; 
    infoHUB->param.packet.wValue = 0;
    infoHUB->param.packet.wIndex = port;
    infoHUB->param.packet.wLength = 4;
    infoHUB->param.nextState = next;

    infoHUB->state = STATE_HUB_REQ_COMMAND;
}

/*******************************************************************************
  Function:
    bool USBHostHUBInitialize(  )

  Summary:
    This function is the initialization routine for this client driver.

  Description:
    This function is the initialization routine for this client driver.  It
    is called by the host layer when the USB device is being enumerated.For a 
    HID device we need to look into HID descriptor, interface descriptor and 
    endpoint descriptor.

  Precondition:
    None

  Parameters:
    uint8_t address        - Address of the new device
    uint32_t flags          - Initialization flags
    uint8_t clientDriverID - Client driver identification for device requests

  Return Values:
    true   - We can support the device.
    false  - We cannot support the device.

  Remarks:
    None
*******************************************************************************/

bool USBHostHUBInitialize( uint8_t address, uint32_t flags, uint8_t clientDriverID )
{
    USB_DEVICE_INFO             *deviceInfo             = NULL;
    uint8_t                     device;
    uint8_t                     *descriptor;
    uint8_t                     numofinterfaces         = 0;
    USB_HUB_INTERFACE_DETAILS   *pInterfaceDetails      = NULL;
    bool                        validConfiguration      = false;

#ifdef DEBUG_ENABLE
    UART_PutString( "HUB Init " );
//    UART_PutHexU8( address );
#endif

    // Find the device in the table.  If it's there, we have already initialized this device.
    for (device = 0; (device < USB_MAX_HUB_DEVICES) ; device++)
    {
        if(deviceInfoHUB[device].deviceAddress == address) {
            return true;
        }
    }

    // See if we have room for another device
    for (device = 0; (device < USB_MAX_HUB_DEVICES) && (deviceInfoHUB[device].deviceAddress != 0); device++);
    if (device == USB_MAX_HUB_DEVICES)
    {
        return false;
    }

	// Find device information matching the address
    deviceInfo = USBHost_GetDeviceInfo(address);
    if (!deviceInfo) {
    	return false;
    }
    
    USB_HUB_DEVICE_INFO *infoHUB = &deviceInfoHUB[device];

    // Fill in the VID, PID, and client driver ID. They are not not valid unless deviceAddress is non-zero.
//    descriptor = (uint8_t *)deviceInfo->deviceDescriptor;
//    infoHUB->flags.bfAddressReported = 0;
//    infoHUB->ID.vid            = ((USB_DEVICE_DESCRIPTOR *)descriptor)->idVendor;
//    infoHUB->ID.pid            = ((USB_DEVICE_DESCRIPTOR *)descriptor)->idProduct;
    infoHUB->clientDriverID = clientDriverID;

    // Get ready to parse the configuration descriptor.
    descriptor = (uint8_t *)deviceInfo->currentConfigurationDescriptor;

    int i = 0;

    // Total number of interfaces
    infoHUB->numOfInterfaces = descriptor[i+4];

    // Set current configuration to this configuration.  We can change it later.

    // MCHP - Check power requirement

    // Find the next interface descriptor.
    while (i < ((USB_CONFIGURATION_DESCRIPTOR *)descriptor)->wTotalLength)
    {
        // See if we are pointing to an interface descriptor.
        if (descriptor[i+1] == USB_DESCRIPTOR_INTERFACE)
        {
            // See if the interface is a HUB interface.
            if (descriptor[i+5] == DEVICE_CLASS_HUB)
            {
                if (numofinterfaces < 1) {
                    if ((pInterfaceDetails = (USB_HUB_INTERFACE_DETAILS *)USB_MALLOC(sizeof(USB_HUB_INTERFACE_DETAILS))) == NULL)
                    {
                        return false;
                    }
                    numofinterfaces++;

                    pInterfaceDetails->interfaceNumber = descriptor[i+2];

                    // Scan for endpoint descriptors.
                    i += descriptor[i];
                    if (descriptor[i+1] == USB_DESCRIPTOR_ENDPOINT)
                    {
                        if (descriptor[i+3] == 0x03) // Interrupt
                        {
                            if (descriptor[i+2] & 0x80) {
                                pInterfaceDetails->endpointIN =
                                    USB_InterfaceList_FindEndpointEx(deviceInfo->pInterfaceList
                                    , pInterfaceDetails->interfaceNumber, 0, descriptor[i+2]);
                                pInterfaceDetails->endpointMaxDataSize  = ((descriptor[i+4]) |
                                                                           (descriptor[i+5] << 8));
                                pInterfaceDetails->endpointPollInterval = descriptor[i+6];
                            } else {
                                // ?? why?
                                pInterfaceDetails->endpointOUT =
                                    USB_InterfaceList_FindEndpointEx(deviceInfo->pInterfaceList
                                    , pInterfaceDetails->interfaceNumber, 0, descriptor[i+2]);
                            }
                        }
                        // Initialize the remaining device information.
                        infoHUB->deviceAddress = address;
                        infoHUB->pDeviceInfo   = deviceInfo;
                        infoHUB->pInterfaceDetails = pInterfaceDetails;
                        validConfiguration = true;
                    }

                } else {
                    // Ignore interfaces
                    
                }
            }
        }

        // Jump to the next descriptor in this configuration.
        i += descriptor[i];
    }
    
    if (validConfiguration) {
        // allocate buffer
        if (!infoHUB->buffer) {
            infoHUB->buffer = (uint8_t *)USB_MALLOC(USB_HUB_BUFFER_SIZE);
        }
        memset(infoHUB->buffer, 0, USB_HUB_BUFFER_SIZE);
        // Request GET_HUB_DESCRIPTOR
        infoHUB->param.packet.bmRequestType = USB_SETUP_DEVICE_TO_HOST | USB_SETUP_TYPE_CLASS | USB_SETUP_RECIPIENT_DEVICE;
        infoHUB->param.packet.bRequest = USB_REQUEST_GET_DESCRIPTOR;
        infoHUB->param.packet.wValue = USB_GET_HUB_DESCRIPTOR;
        infoHUB->param.packet.wIndex = 0;
        infoHUB->param.packet.wLength = 8;
        if (USBHostIssueDeviceRequest( infoHUB->pDeviceInfo
                , &infoHUB->param.packet
                , infoHUB->buffer
                , infoHUB->clientDriverID
#ifdef DEBUG_ENABLE
                , 0x0900
#endif
        )) {
            return false;
        }
#ifdef DEBUG_ENABLE
        DEBUG_PutStringHexU8( "Valid: ", infoHUB->clientDriverID );
#endif
        infoHUB->state = STATE_HUB_WAIT_GET_HUB_DESCRIPTOR;

        return true;
    }

    return true;
}

// *****************************************************************************
static __inline__ bool USBHostHUBEvent_Detach( uint8_t address )
{
#ifdef DEBUG_ENABLE
    DEBUG_PutString( "USBHostHUBEvent_Detach\r\n" );
#endif

    // Find the device in the table.  If found, clear the fields.
    USB_HUB_DEVICE_INFO *infoHUB = USBHostHUB_GetInstance(address);
    if(infoHUB) {
        /* Free the memory used by the HID device */
        USB_FREE(infoHUB->pInterfaceDetails);
        USB_FREE(infoHUB->buffer);
        memset(infoHUB, 0, sizeof(USB_HUB_DEVICE_INFO));
    }
    return true;
}


// *****************************************************************************
// *****************************************************************************
// Section: USB Host Stack Interface Functions
// *****************************************************************************
// *****************************************************************************

// *****************************************************************************
static void USBHostHUBEventTimerHandler()
{
    uint8_t device;
    USB_HUB_DEVICE_INFO *infoHUB;
    for (device = 0; device < USB_MAX_HUB_DEVICES; device++)
    {
        infoHUB = &deviceInfoHUB[device];
        infoHUB->waitTimerFlag = 0;
    }
}

/*******************************************************************************
  Function:
     void USBHostHUBTasks( )

  Summary:
    This function performs the maintenance tasks required by HUB class

  Description:
    This function performs the maintenance tasks required by the HUB
    class.  If transfer events from the host layer are not being used, then
    it should be called on a regular basis by the application.  If transfer
    events from the host layer are being used, this function is compiled out,
    and does not need to be called.

  Precondition:
    USBHostHUBInitialize() has been called.

  Parameters:
    None

  Returns:
    None

  Remarks:
    None
*******************************************************************************/
void USBHostHUBTasks( void )
{
    uint8_t device;
    USB_HUB_DEVICE_INFO *infoHUB;
    USB_HUB_INTERFACE_DETAILS *detail;
    uint16_t size;
    for (device = 0; device < USB_MAX_HUB_DEVICES; device++)
    {
        infoHUB = &deviceInfoHUB[device];
        if (infoHUB->waitTimerFlag) {
            // skip event until timer is over
            continue;
        }
        detail = infoHUB->pInterfaceDetails;
        switch(infoHUB->state) {
            case STATE_HUB_REQ_GET_STATUS_CHANGE:
                //
                // Get status change flags
                //
                size = detail->endpointMaxDataSize;
                if (size > USB_HUB_BUFFER_SIZE) size = USB_HUB_BUFFER_SIZE;
                memset(infoHUB->buffer, 0, size);
                // get port changed flags using interrupt pipe
                if (USBHostReadWrite( false, infoHUB->pDeviceInfo
                    , detail->endpointIN
                    , infoHUB->buffer, size )) {
                    // goto next state
                    infoHUB->state = STATE_HUB_WAIT_GET_STATUS_CHANGE;
                }
                break;

            case STATE_HUB_REQ_COMMAND:
                //
                // Clear/Set feature 
                //
                if (USBHostIssueDeviceRequest( infoHUB->pDeviceInfo
                    , &((USB_HUB_DEVICE_INFO *)infoHUB)->param.packet
                    , infoHUB->buffer
                    , infoHUB->clientDriverID
#ifdef DEBUG_ENABLE
                    , (0x0980 | infoHUB->param.packet.bRequest)
#endif
                )) {
                    infoHUB->state = STATE_HUB_WAIT_COMMAND;
                }
                break;

            case STATE_HUB_REQ_PORT_SETTING_DEVICE:
                //
                // Setting device using HOST service
                //
                if (USBHost_AttachDeviceOnHUB(infoHUB->deviceAddress
                    , infoHUB->currentPortNumber
                    , (infoHUB->currentPortStatus & PS_PORT_LOW_SPEED) ? 0x80 : 0) != 0
                ) {
                    infoHUB->state = STATE_HUB_WAIT_PORT_SETTING_DEVICE;
                }
                break;

            case STATE_HUB_WAIT_PORT_SETTING_DEVICE:
                //
                // Wait for the device is configured 
                //
                if (USBHostDeviceStatus(&infoHUB->portInfo[infoHUB->currentPortNumber].deviceAddress) == USB_DEVICE_ATTACHED) {
                    infoHUB->state = STATE_HUB_REQ_GET_STATUS_CHANGE;
                }
                break;

            default:
                break;
        }
    }
}

// *****************************************************************************
// *****************************************************************************
static __inline__ void USBHostHUBEvent_Transfer_GotHubDescriptor( USB_HUB_DEVICE_INFO *infoHUB )
{
    infoHUB->numOfPorts = infoHUB->buffer[2];
    infoHUB->portStatusMask = (1 << (infoHUB->numOfPorts + 1)) - 2;
    infoHUB->bPwrOn2PwrGood = infoHUB->buffer[5];
    if (infoHUB->bPwrOn2PwrGood < 50) infoHUB->bPwrOn2PwrGood = 50;
    infoHUB->bPwrOn2PwrGood <<= 1;
#ifdef DEBUG_ENABLE
    UART_PutStringHexU8( "Num of Ports: ", infoHUB->buffer[2] );
#endif
    if (infoHUB->numOfPorts > 0) {
        infoHUB->currentPortNumber = 1;

        USB_FREE_AND_CLEAR(infoHUB->portInfo);
        infoHUB->portInfo = (USB_HUB_PORT_INFO *)USB_MALLOC(sizeof(USB_HUB_PORT_INFO) * (infoHUB->numOfPorts + 1));
        if (!infoHUB->portInfo) {
            // malloc error
            USBHost_SetError( USB_MEMORY_ALLOCATION_ERROR );
            infoHUB->state = STATE_HUB_NONE;
         }
        memset(infoHUB->portInfo, 0, sizeof(USB_HUB_PORT_INFO) * (infoHUB->numOfPorts + 1));

//        infoHUB->state = STATE_HUB_REQ_PORT_POWER_ON;
        USBHostHUBSetFeature(infoHUB, FS_PORT_POWER, infoHUB->currentPortNumber, STATE_HUB_WAIT_PORT_POWER_ON);

    } else {
        // no exists port ???
        infoHUB->state = STATE_HUB_REQ_GET_STATUS_CHANGE;
    }
}

// *****************************************************************************
static __inline__ void USBHostHUBEvent_Transfer_PowerOnNextPort( USB_HUB_DEVICE_INFO *infoHUB )
{
#ifdef DEBUG_ENABLE
    UART_PutStringHexU8( "Power on: ", infoHUB->currentPortNumber );
#endif
    infoHUB->currentPortNumber++;
    if (infoHUB->currentPortNumber <= infoHUB->numOfPorts) {
        // power on the next port
//                infoHUB->state = STATE_HUB_REQ_PORT_POWER_ON;
        USBHostHUBSetFeature(infoHUB, FS_PORT_POWER, infoHUB->currentPortNumber, STATE_HUB_WAIT_PORT_POWER_ON);
    } else {
        // check status on each ports
        infoHUB->state = STATE_HUB_REQ_GET_STATUS_CHANGE;
    }
}

// *****************************************************************************
static __inline__ void USBHostHUBEvent_Transfer_GotStatusChange( USB_HUB_DEVICE_INFO *infoHUB )
{
#ifdef DEBUG_ENABLE
    if (infoHUB->portStatus) {
        UART_PutStringHexU16( "Changed status: ", infoHUB->portStatus );
    }
#endif
//    if (infoHUB->portStatus & 1) {
//        // HUB Status changed
////        infoHUB->state = STATE_HUB_REQ_GET_HUB_STATUS;
//        USBHostHUBGetStatus(infoHUB, 0, STATE_HUB_WAIT_GET_HUB_STATUS);
//    }

    // 
    uint32_t ports = 0x2;
    infoHUB->currentPortNumber = 0;
    for(int i=1; i<=infoHUB->numOfPorts; i++) {
        if (infoHUB->portStatus & ports) {
            infoHUB->currentPortNumber = (uint8_t)i;
            infoHUB->portStatus ^= ports;   // clear
            break;
        }
        ports <<= 1;
    }
    if (infoHUB->currentPortNumber > 0) {
        // change status on the port
        // get status on the port
//        infoHUB->state = STATE_HUB_REQ_GET_PORT_STATUS;
        USBHostHUBGetStatus(infoHUB, infoHUB->currentPortNumber, STATE_HUB_WAIT_GET_PORT_STATUS);
    } else {
        // no change status on the HUB
        infoHUB->state = STATE_HUB_REQ_GET_STATUS_CHANGE;
    }
}

// *****************************************************************************
static __inline__ void USBHostHUBEvent_Transfer_GotPortStatus( USB_HUB_DEVICE_INFO *infoHUB )
{
    // current status are buffer[0],[1]
    // changed status are buffer[2],[3]
    //
#ifdef DEBUG_ENABLE
    UART_PutString( "PORT status:" );
    UART_PutHex16String( (uint16_t *)infoHUB->buffer, 2 );
#endif

    infoHUB->currentPortStatus = P_PORT_STATUS(infoHUB->buffer)->current_status;
    infoHUB->currentPortChangeStatus = P_PORT_STATUS(infoHUB->buffer)->changed_status;
    if (infoHUB->currentPortStatus & PS_PORT_RESET) {
        // device is resetting
        // repeat get status
//        infoHUB->state = STATE_HUB_REQ_GET_PORT_STATUS;
        USBHostHUBGetStatus(infoHUB, infoHUB->currentPortNumber, STATE_HUB_WAIT_GET_PORT_STATUS);

    } else if ((infoHUB->currentPortStatus & (PS_PORT_CONNECTION | PS_PORT_ENABLE | PS_PORT_SUSPEND | PS_PORT_RESET | PS_PORT_POWER)) == (PS_PORT_CONNECTION | PS_PORT_ENABLE | PS_PORT_POWER)) {
        if (infoHUB->waitTimerFlag == 0 && (infoHUB->currentPortChangeStatus & (PS_PORT_CONNECTION | PS_PORT_ENABLE | PS_PORT_SUSPEND | PS_PORT_RESET)) == (PS_PORT_CONNECTION | PS_PORT_RESET)) {
            // device is enable after resetted it
            // clear changed status and set the device
//            infoHUB->state = STATE_HUB_REQ_CLEAR_C_PORT_RESET;
            USBHostHUBClearFeature(infoHUB, FS_C_PORT_RESET, infoHUB->currentPortNumber, STATE_HUB_WAIT_CLEAR_C_PORT_RESET);
        } else {
            // device is preparing ?
            // get status again
//            infoHUB->state = STATE_HUB_REQ_GET_STATUS_CHANGE;
            USBHostHUBGetStatus(infoHUB, infoHUB->currentPortNumber, STATE_HUB_WAIT_GET_PORT_STATUS);
        }

    } else if ((infoHUB->currentPortStatus & (PS_PORT_CONNECTION | PS_PORT_ENABLE | PS_PORT_SUSPEND | PS_PORT_RESET | PS_PORT_POWER)) == (PS_PORT_CONNECTION | PS_PORT_POWER)) {
        // device is power on and connected on this port
        if (infoHUB->currentPortChangeStatus & PS_PORT_CONNECTION) {
            // reset the device
//            infoHUB->state = STATE_HUB_REQ_PORT_RESET;
            USBHostHUBSetFeature(infoHUB, FS_PORT_RESET, infoHUB->currentPortNumber, STATE_HUB_WAIT_PORT_RESET);
        } else {
//            infoHUB->state = STATE_HUB_REQ_GET_STATUS_CHANGE;
            USBHostHUBGetStatus(infoHUB, infoHUB->currentPortNumber, STATE_HUB_WAIT_GET_PORT_STATUS);
        }

    } else if ((infoHUB->currentPortStatus & (PS_PORT_CONNECTION | PS_PORT_ENABLE | PS_PORT_SUSPEND | PS_PORT_RESET | PS_PORT_POWER)) == PS_PORT_POWER) {
        // device is disconnected
        // so, reset the device
        if (infoHUB->currentPortChangeStatus & PS_PORT_CONNECTION) {
            // clear changed status
//                        infoHUB->state = STATE_HUB_REQ_CLEAR_C_PORT_CONNECT;
            USBHostHUBClearFeature(infoHUB, FS_C_PORT_CONNECTION, infoHUB->currentPortNumber, STATE_HUB_WAIT_CLEAR_C_PORT_CONNECT);
        } else {
            // device is disable
            infoHUB->state = STATE_HUB_REQ_GET_STATUS_CHANGE;
        }

    } else {
        // no more changed bit
        infoHUB->state = STATE_HUB_REQ_GET_STATUS_CHANGE;

    }
}

// *****************************************************************************
static __inline__ bool USBHostHUBEvent_Transfer( uint8_t address, void *data, uint32_t size )
{
    HOST_TRANSFER_DATA *transdata = (HOST_TRANSFER_DATA *)data;
#ifdef DEBUG_ENABLE
    DEBUG_PutString( "HUB Event_Transfer\r\n" );
#endif

    USB_HUB_DEVICE_INFO *infoHUB = USBHostHUB_GetInstance(address);
    if (!infoHUB) {
        return true;
    }

    int loop = 1;
    while(loop) {
      loop = 0;
      switch(infoHUB->state) {
        case STATE_HUB_WAIT_GET_HUB_DESCRIPTOR:
            //
            // get hub descriptor (get number of ports)
            //
            USBHostHUBEvent_Transfer_GotHubDescriptor( infoHUB );
            break;

        case STATE_HUB_WAIT_PORT_POWER_ON:
            //
            // power on the next port
            //
            USBHostHUBEvent_Transfer_PowerOnNextPort( infoHUB );
            break;

        case STATE_HUB_WAIT_GET_STATUS_CHANGE:
            //
            // get status change
            //
#ifdef DEBUG_ENABLE
//            UART_PutHexU8( (transdata->dataCount) & 0xff );
#endif
            // count of ports limits 31 (4bytes)
            infoHUB->portStatus = 0;
            for(int i=(transdata->dataCount < 4 ? transdata->dataCount : 4); i>0; i--) {
                infoHUB->portStatus <<= 8;
                infoHUB->portStatus = infoHUB->buffer[i-1];
            }
            //[: through :]
            // |
            // V
        case STATE_HUB_CONT_STATUS_CHANGE:
            //
            // parse changed status and decide next action
            //
            USBHostHUBEvent_Transfer_GotStatusChange( infoHUB );
            break;

        case STATE_HUB_WAIT_GET_HUB_STATUS:
            //
            // get HUB status
            //
#ifdef DEBUG_ENABLE
            UART_PutStringHexU8( "HUB status: ", infoHUB->buffer[0] );
#endif
            infoHUB->state = STATE_HUB_REQ_GET_STATUS_CHANGE;
            break;

        case STATE_HUB_WAIT_GET_PORT_STATUS:
            //
            // get PORT status
            //
            USBHostHUBEvent_Transfer_GotPortStatus( infoHUB );
            break;

        case STATE_HUB_WAIT_PORT_RESET:
            //
            // port resetting
            // change status on the port
//            infoHUB->state = STATE_HUB_REQ_GET_PORT_STATUS;
            USBHostHUBGetStatus(infoHUB, infoHUB->currentPortNumber, STATE_HUB_WAIT_GET_PORT_STATUS);
            infoHUB->waitTimerFlag = 1;
            USBHost_StartTimer(infoHUB->bPwrOn2PwrGood, &USBHostHUBEventTimerHandler);
            break;

        case STATE_HUB_WAIT_CLEAR_C_PORT_RESET:
            //
            // cleared change status reset on the port
            //
//            infoHUB->state = STATE_HUB_REQ_CLEAR_C_PORT_CONNECT;
            USBHostHUBClearFeature(infoHUB, FS_C_PORT_CONNECTION, infoHUB->currentPortNumber, STATE_HUB_WAIT_CLEAR_C_PORT_CONNECT);
            break;

        case STATE_HUB_WAIT_CLEAR_C_PORT_CONNECT:
            //
            // cleared change status enable on the port
            //    
            if (infoHUB->currentPortStatus & (PS_PORT_CONNECTION | PS_PORT_ENABLE | PS_PORT_POWER) == (PS_PORT_CONNECTION | PS_PORT_ENABLE | PS_PORT_POWER)) {
                // Next: Set the device on the port
                infoHUB->state = STATE_HUB_REQ_PORT_SETTING_DEVICE;
            } else {
                // Next: detach on the port
                USBHost_DetachDeviceOnHUB(infoHUB->deviceAddress, infoHUB->portInfo[infoHUB->currentPortNumber].deviceAddress);
                infoHUB->portInfo[infoHUB->currentPortNumber].deviceAddress = 0;
                infoHUB->state = STATE_HUB_REQ_GET_STATUS_CHANGE;
            }
            break;
            
        case STATE_HUB_WAIT_CLEAR_PORT_CONNECT:
            // device is disabled
#ifdef DEBUG_ENABLE
            UART_PutStringHexU8( "PORT Disconnect: ", infoHUB->currentPortNumber );
#endif
            infoHUB->portInfo[infoHUB->currentPortNumber].deviceAddress = 0;
            infoHUB->state = STATE_HUB_REQ_GET_STATUS_CHANGE;
            break;

         case STATE_HUB_WAIT_COMMAND:
            //
            // Clear/set feature
            //
            infoHUB->state = infoHUB->param.nextState;
            loop = 1; // process next state if it exists in this function
            break;
        default:
            break;
      }
    }

    return true;
}

// *****************************************************************************
static __inline__ bool USBHostHUBEvent_UnsupportedDevice( uint8_t address )
{
    // disable the specified device
    USB_HUB_DEVICE_INFO *infoHUB = USBHostHUB_GetInstance(address);
    if (!infoHUB) {
        return true;
    }
 #ifdef DEBUG_ENABLE
    UART_PutStringHexU8( "HUB: Unsupported device on port ", infoHUB->currentPortNumber );
#endif
   USBHostHUBClearFeature(infoHUB, FS_PORT_ENABLE, infoHUB->currentPortNumber, STATE_HUB_WAIT_CLEAR_PORT_CONNECT);
}

// *****************************************************************************
bool USBHostHUBEventHandler( uint8_t address, USB_EVENT event, void *data, uint32_t size )
{
    switch (event)
    {
        case EVENT_NONE:             // No event occured (NULL event)
            return true;
            break;

        case EVENT_DETACH:           // USB cable has been detached (data: uint8_t, address of device)
            return USBHostHUBEvent_Detach( address );
            break;

        case EVENT_TRANSFER:         // A USB transfer has completed
            return USBHostHUBEvent_Transfer( address, data, size );
            break;

        case EVENT_UNSUPPORTED_DEVICE: // Device is not supported
            return USBHostHUBEvent_UnsupportedDevice( address );
            break;

        case EVENT_SOF:              // Start of frame - NOT NEEDED
        case EVENT_RESUME:           // Device-mode resume received
        case EVENT_SUSPEND:          // Device-mode suspend/idle event received
        case EVENT_RESET:            // Device-mode bus reset received
        case EVENT_STALL:            // A stall has occured
            return true;
            break;

        default:
            return false;
            break;
    }
    return false;
}

// EOF usb_host_hub.c
