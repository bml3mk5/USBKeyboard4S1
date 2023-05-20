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

#ifndef _USB_HOST_LOCAL_
#define _USB_HOST_LOCAL_

#include <xc.h>
#include "common.h"
#include "usb_hal_local.h"
#include "usb_struct_config_list.h"
#include "usb_struct_interface.h"

#ifdef Simulator
//#define DEBUG_ENABLE 1
#endif

#ifdef DEBUG_ENABLE
#define DEBUG_PutString(x)
#define DEBUG_PutChar(x)
#define DEBUG_PutHexa(x,y)
#define DEBUG_PutHexU8(x)
#define DEBUG_PutStringHexU8(x,y)
#define DEBUG_Flush()
#endif

// *****************************************************************************
// *****************************************************************************
// Section: Constants
//
// These constants are internal to the stack.  All constants required by the
// API are in the header file(s).
// *****************************************************************************
// *****************************************************************************

// *****************************************************************************
// Section: State Machine Constants
// *****************************************************************************

#define STATE_MASK                                      0x0F00  //
#define SUBSTATE_MASK                                   0x00F0  //
#define SUBSUBSTATE_MASK                                0x000F  //

#define NEXT_STATE                                      0x0100  //
#define NEXT_SUBSTATE                                   0x0010  //
#define NEXT_SUBSUBSTATE                                0x0001  //

#define SUBSUBSTATE_ERROR                               0x000F  //

#define NO_STATE                                        0xFFFF  //

/*
*******************************************************************************
DETACHED state machine values

This state machine handles the condition when no device is attached.
*/

#define STATE_DETACHED                                  0x0000  //
#define SUBSTATE_INITIALIZE                             0x0000  //
#define SUBSTATE_WAIT_FOR_POWER                         0x0010  //
#define SUBSUBSTATE_WAIT_FOR_POWER_START                0x0000  //
#define SUBSUBSTATE_WAIT_FOR_POWER_WAIT                 0x0001  //
#define SUBSUBSTATE_WAIT_FOR_POWER_DONE                 0x0002  //
#define SUBSTATE_TURN_ON_POWER                          0x0020  //
#define SUBSTATE_WAIT_FOR_DEVICE                        0x0030  //

/*
*******************************************************************************
ATTACHED state machine values

This state machine gets the device descriptor of the remote device.  We get the
size of the device descriptor, and use that size to get the entire device
descriptor.  Then we check the VID and PID and make sure they appear in the TPL.
*/

#define STATE_ATTACHED                                  0x0100  //

#define SUBSTATE_SETTLE                                 0x0000  //
#define SUBSUBSTATE_START_SETTLING_DELAY                0x0000  //
#define SUBSUBSTATE_WAIT_FOR_SETTLING                   0x0001  //
#define SUBSUBSTATE_SETTLING_DONE                       0x0002  //

#define SUBSTATE_RESET_DEVICE                           0x0010  //
#define SUBSUBSTATE_SET_RESET                           0x0000  //
#define SUBSUBSTATE_RESET_WAIT                          0x0001  //
#define SUBSUBSTATE_RESET_RECOVERY                      0x0002  //
#define SUBSUBSTATE_RECOVERY_WAIT                       0x0003  //
#define SUBSUBSTATE_RESET_COMPLETE                      0x0004  //

#define SUBSTATE_GET_DEVICE_DESCRIPTOR_SIZE             0x0020  //
#define SUBSUBSTATE_SEND_GET_DEVICE_DESCRIPTOR_SIZE     0x0000  //
#define SUBSUBSTATE_WAIT_FOR_GET_DEVICE_DESCRIPTOR_SIZE 0x0001  //
#define SUBSUBSTATE_GET_DEVICE_DESCRIPTOR_SIZE_COMPLETE 0x0002  //

#define SUBSTATE_GET_DEVICE_DESCRIPTOR                  0x0030  //
// same as SUBSTATE_GET_DEVICE_DESCRIPTOR_SIZE
#define SUBSUBSTATE_SEND_GET_DEVICE_DESCRIPTOR          0x0000  //
#define SUBSUBSTATE_WAIT_FOR_GET_DEVICE_DESCRIPTOR      0x0001  //
#define SUBSUBSTATE_GET_DEVICE_DESCRIPTOR_COMPLETE      0x0002  //

#define SUBSTATE_VALIDATE_VID_PID                       0x0040  //
//#define SUBSTATE_VALIDATE_VID_PID                       0x0030  //

/*
*******************************************************************************
ADDRESSING state machine values

This state machine sets the address of the remote device.
*/

#define STATE_ADDRESSING                                0x0200  //

#define SUBSTATE_SET_DEVICE_ADDRESS                     0x0000  //
#define SUBSUBSTATE_SEND_SET_DEVICE_ADDRESS             0x0000  //
#define SUBSUBSTATE_WAIT_FOR_SET_DEVICE_ADDRESS         0x0001  //
#define SUBSUBSTATE_SET_DEVICE_ADDRESS_COMPLETE         0x0002  //

/*
*******************************************************************************
CONFIGURING state machine values

This state machine sets the configuration of the remote device, and sets up
internal variables to support the device.
*/
#define STATE_CONFIGURING                               0x0300  //

#define SUBSTATE_INIT_CONFIGURATION                     0x0000  //

#define SUBSTATE_GET_CONFIG_DESCRIPTOR_SIZE             0x0010  //
#define SUBSUBSTATE_SEND_GET_CONFIG_DESCRIPTOR_SIZE     0x0000  //
#define SUBSUBSTATE_WAIT_FOR_GET_CONFIG_DESCRIPTOR_SIZE 0x0001  //
#define SUBSUBSTATE_GET_CONFIG_DESCRIPTOR_SIZECOMPLETE  0x0002  //

#define SUBSTATE_GET_CONFIG_DESCRIPTOR                  0x0020  //
#define SUBSUBSTATE_SEND_GET_CONFIG_DESCRIPTOR          0x0000  //
#define SUBSUBSTATE_WAIT_FOR_GET_CONFIG_DESCRIPTOR      0x0001  //
#define SUBSUBSTATE_GET_CONFIG_DESCRIPTOR_COMPLETE      0x0002  //

#define SUBSTATE_SELECT_CONFIGURATION                   0x0030  //
#define SUBSUBSTATE_SELECT_CONFIGURATION                0x0000  //
#define SUBSUBSTATE_SEND_SET_OTG                        0x0001  //
#define SUBSUBSTATE_WAIT_FOR_SET_OTG_DONE               0x0002  //
#define SUBSUBSTATE_SET_OTG_COMPLETE                    0x0003  //

#define SUBSTATE_APPLICATION_CONFIGURATION              0x0040  //

#define SUBSTATE_SET_CONFIGURATION                      0x0050  //
#define SUBSUBSTATE_SEND_SET_CONFIGURATION              0x0000  //
#define SUBSUBSTATE_WAIT_FOR_SET_CONFIGURATION          0x0001  //
#define SUBSUBSTATE_SET_CONFIGURATION_COMPLETE          0x0002  //
//#define SUBSUBSTATE_INIT_CLIENT_DRIVERS                 0x0003  //

#define SUBSTATE_INIT_CLIENT_DRIVERS                    0x0060  //

/*
*******************************************************************************
RUNNING state machine values

*/

#define STATE_RUNNING                                   0x0400  //
#define SUBSTATE_NORMAL_RUN                             0x0000  //
#define SUBSTATE_SUSPEND_AND_RESUME                     0x0010  //
#define SUBSUBSTATE_SUSPEND                             0x0000  //
#define SUBSUBSTATE_RESUME                              0x0001  //
#define SUBSUBSTATE_RESUME_WAIT                         0x0002  //
#define SUBSUBSTATE_RESUME_RECOVERY                     0x0003  //
#define SUBSUBSTATE_RESUME_RECOVERY_WAIT                0x0004  //
#define SUBSUBSTATE_RESUME_COMPLETE                     0x0005  //


/*
*******************************************************************************
HOLDING state machine values

*/

#define STATE_HOLDING                                   0x0500  //
#define SUBSTATE_HOLD_INIT                              0x0000  //
#define SUBSTATE_HOLD                                   0x0001  //


// *****************************************************************************
// Section: Token State Machine Constants
// *****************************************************************************

#define TSTATE_MASK                             0x00F0  //
#define TSUBSTATE_MASK                          0x000F  //

#define TSUBSTATE_ERROR                         0x000F  //

#define TSTATE_IDLE                             0x0000  //

#define TSTATE_CONTROL_NO_DATA                  0x0010  //
#define TSUBSTATE_CONTROL_NO_DATA_SETUP         0x0000  //
#define TSUBSTATE_CONTROL_NO_DATA_ACK           0x0001  //
#define TSUBSTATE_CONTROL_NO_DATA_COMPLETE      0x0002  //

#define TSTATE_CONTROL_READ                     0x0020  //
#define TSUBSTATE_CONTROL_READ_SETUP            0x0000  //
#define TSUBSTATE_CONTROL_READ_DATA             0x0001  //
#define TSUBSTATE_CONTROL_READ_ACK              0x0002  //
#define TSUBSTATE_CONTROL_READ_COMPLETE         0x0003  //

#define TSTATE_CONTROL_WRITE                    0x0030  //
#define TSUBSTATE_CONTROL_WRITE_SETUP           0x0000  //
#define TSUBSTATE_CONTROL_WRITE_DATA            0x0001  //
#define TSUBSTATE_CONTROL_WRITE_ACK             0x0002  //
#define TSUBSTATE_CONTROL_WRITE_COMPLETE        0x0003  //

#define TSTATE_INTERRUPT_READ                   0x0040  //
#define TSUBSTATE_INTERRUPT_RW_DATA             0x0000  //
#define TSUBSTATE_INTERRUPT_RW_COMPLETE         0x0001  //

#define TSTATE_INTERRUPT_WRITE                  0x0050  //
//#define TSUBSTATE_INTERRUPT_WRITE_DATA          0x0000  //
//#define TSUBSTATE_INTERRUPT_WRITE_COMPLETE      0x0001  //

#define TSTATE_ISOCHRONOUS_READ                 0x0060  //
#define TSUBSTATE_ISOCHRONOUS_READ_DATA         0x0000  //
#define TSUBSTATE_ISOCHRONOUS_READ_COMPLETE     0x0001  //

#define TSTATE_ISOCHRONOUS_WRITE                0x0070  //
#define TSUBSTATE_ISOCHRONOUS_WRITE_DATA        0x0000  //
#define TSUBSTATE_ISOCHRONOUS_WRITE_COMPLETE    0x0001  //

#define TSTATE_BULK_READ                        0x0080  //
#define TSUBSTATE_BULK_READ_DATA                0x0000  //
#define TSUBSTATE_BULK_READ_COMPLETE            0x0001  //

#define TSTATE_BULK_WRITE                       0x0090  //
#define TSUBSTATE_BULK_WRITE_DATA               0x0000  //
#define TSUBSTATE_BULK_WRITE_COMPLETE           0x0001  //

//******************************************************************************
// Section: USB Peripheral Constants
//******************************************************************************

// Section: USB Control Register Constants

// Section: U1PWRC

#define U1PWRC_SUSPEND_MODE                 _U1PWRC_USUSPEND_MASK // = 0x02    // U1PWRC - Put the module in suspend mode.
#define U1PWRC_NORMAL_OPERATION                0x00                            // U1PWRC - Normal USB operation
#define U1PWRC_ENABLED                      _U1PWRC_USBPWR_MASK   // = 0x01    // U1PWRC - Enable the USB module.
#define U1PWRC_DISABLED                        0x00                            // U1PWRC - Disable the USB module.

// Section: U1OTGCON

#define U1OTGCON_DPLUS_PULLUP_ENABLE        _U1OTGCON_DPPULUP_MASK  //     0x80    // U1OTGCON - Enable D+ pull-up
#define U1OTGCON_DMINUS_PULLUP_ENABLE       _U1OTGCON_DMPULUP_MASK  //     0x40    // U1OTGCON - Enable D- pull-up
#define U1OTGCON_DPLUS_PULLDOWN_ENABLE      _U1OTGCON_DPPULDWN_MASK //     0x20    // U1OTGCON - Enable D+ pull-down
#define U1OTGCON_DMINUS_PULLDOWN_ENABLE     _U1OTGCON_DMPULDWN_MASK //     0x10    // U1OTGCON - Enable D- pull-down
#define U1OTGCON_VBUS_ON                    _U1OTGCON_VBUSON_MASK   //     0x08    // U1OTGCON - Enable Vbus
#define U1OTGCON_OTG_ENABLE                 _U1OTGCON_OTGEN_MASK    //     0x04    // U1OTGCON - Enable OTG
#define U1OTGCON_VBUS_CHARGE_ENABLE         _U1OTGCON_VBUSCHG_MASK  //     0x02    // U1OTGCON - Vbus charge line set to 5V
#define U1OTGCON_VBUS_DISCHARGE_ENABLE      _U1OTGCON_VBUSDIS_MASK  //     0x01    // U1OTGCON - Discharge Vbus

// Section: U1OTGIE/U1OTGIR

#define U1OTGIE_INTERRUPT_IDIF              _U1OTGIE_IDIE_MASK      // 0x80    // U1OTGIR - ID state change flag
#define U1OTGIE_INTERRUPT_T1MSECIF          _U1OTGIE_T1MSECIE_MASK  // 0x40    // U1OTGIR - 1ms timer interrupt flag
#define U1OTGIE_INTERRUPT_LSTATEIF          _U1OTGIE_LSTATEIE_MASK  // 0x20    // U1OTGIR - line state stable flag
#define U1OTGIE_INTERRUPT_ACTIVIF           _U1OTGIE_ACTVIE_MASK    // 0x10    // U1OTGIR - bus activity flag
#define U1OTGIE_INTERRUPT_SESVDIF           _U1OTGIE_SESVDIE_MASK   // 0x08    // U1OTGIR - session valid change flag
#define U1OTGIE_INTERRUPT_SESENDIF          _U1OTGIE_SESENDIE_MASK  // 0x04    // U1OTGIR - B-device Vbus change flag
#define U1OTGIE_INTERRUPT_VBUSVDIF          _U1OTGIE_VBUSVDIE_MASK  // 0x01    // U1OTGIR - A-device Vbus change flag

// Section: U1CON

#define U1CON_JSTATE_DETECTED               _U1CON_JSTATE_MASK          // = 0x80    // U1CON - J state
#define U1CON_SE0_DETECTED                  _U1CON_SE0_MASK             // = 0x40    // U1CON - Single ended 0 detected
#define U1CON_TOKEN_BUSY                    _U1CON_PKTDIS_TOKBUSY_MASK  // = 0x20    // U1CON - Token currently being processed
#define U1CON_ASSERT_RESET                  _U1CON_USBRST_MASK          // = 0x10    // U1CON - RESET signalling
#define U1CON_HOST_MODE_ENABLE              _U1CON_HOSTEN_MASK          // = 0x08    // U1CON - Enable host mode
#define U1CON_RESUME_ACTIVATED              _U1CON_RESUME_MASK          // = 0x04    // U1CON - RESUME signalling
#define U1CON_PINGPONG_RESET                _U1CON_PPBRST_MASK          // = 0x02    // U1CON - Reset ping-pong buffer pointer
#define U1CON_SOF_ENABLE                    _U1CON_USBEN_SOFEN_MASK     // = 0x01    // U1CON - Enable SOF generation
#define U1CON_SOF_DISABLE                   0x00    // U1CON - Disable SOF generation

// Section: U1CNFG1

#define U1CNFG1_EYE_PATTERN_TEST                0x80    // U1CFG1 - Enable eye pattern test
#define U1CNFG1_MONITOR_OE                      0x40    // U1CFG1 - nOE signal active
#define U1CNFG1_FREEZE_IN_DEBUG_MODE            0x20    // U1CFG1 - Freeze on halt when in debug mode
#define U1CNFG1_STOP_IN_IDLE_MODE               0x10    // U1CFG1 - Stop module in idle mode
#define U1CNFG1_PING_PONG__ALL_BUT_EP0          0x03    // U1CFG1 - Ping-pong on all endpoints except EP0
#define U1CNFG1_PING_PONG__FULL_PING_PONG       0x02    // U1CFG1 - Ping-pong on all endpoints
#define U1CNFG1_PING_PONG__EP0_OUT_ONLY         0x01    // U1CFG1 - Ping-pong on EP 0 out only
#define U1CNFG1_PING_PONG__NO_PING_PONG         0x00    // U1CFG1 - No ping-pong

// Section: U1CNFG2

#define USB_VBUS_PULLUP_ENABLE              0x01    // U1CNFG2 - Enable Vbus pull-up
#define USB_EXTERNAL_IIC                    0x08    // U1CNFG2 - External module controlled by I2C
#define USB_VBUS_BOOST_DISABLE              0x04    // U1CNFG2 - Disable Vbus boost
#define USB_VBUS_BOOST_ENABLE               0x00    // U1CNFG2 - Enable Vbus boost
#define USB_VBUS_COMPARE_DISABLE            0x02    // U1CNFG2 - Vbus comparator disabled
#define USB_VBUS_COMPARE_ENABLE             0x00    // U1CNFG2 - Vbus comparator enabled
#define USB_ONCHIP_DISABLE                  0x01    // U1CNFG2 - On-chip transceiver disabled
#define USB_ONCHIP_ENABLE                   0x00    // U1CNFG2 - On-chip transceiver enabled

// Section: U1IE/U1IR

#define U1IE_INTERRUPT_STALL                _U1IE_STALLIE_MASK  // 0x80    // U1IE - Stall interrupt enable
#define U1IE_INTERRUPT_ATTACH               _U1IE_ATTACHIE_MASK // 0x40    // U1IE - Attach interrupt enable
#define U1IE_INTERRUPT_RESUME               _U1IE_RESUMEIE_MASK // 0x20    // U1IE - Resume interrupt enable
#define U1IE_INTERRUPT_IDLE                 _U1IE_IDLEIE_MASK   // 0x10    // U1IE - Idle interrupt enable
#define U1IE_INTERRUPT_TRANSFER             _U1IE_TRNIE_MASK    // 0x08    // U1IE - Transfer Done interrupt enable
#define U1IE_INTERRUPT_SOF                  _U1IE_SOFIE_MASK    // 0x04    // U1IE - Start of Frame Threshold interrupt enable
#define U1IE_INTERRUPT_ERROR                _U1IE_UERRIE_MASK   // 0x02    // U1IE - USB Error interrupt enable
#define U1IE_INTERRUPT_DETACH               _U1IE_DETACHIE_MASK // 0x01    // U1IE - Detach interrupt enable


//******************************************************************************
// Section: Other Constants
//******************************************************************************

#define CLIENT_DRIVER_HOST                  0xFF    // Client driver index for indicating the host driver.

#define DTS_DATA0                           0       // DTS bit - DATA0 PID
#define DTS_DATA1                           1       // DTS bit - DATA1 PID
#define DTS_PREDATA                         0xFF    // DTS bit - Keep previous PID

#define UEP_DIRECT_LOW_SPEED                0x80    // UEP0 - Direct connect to low speed device enabled
#define UEP_NO_DIRECT_LOW_SPEED             0x00    // UEP0 - Direct connect to low speed device disabled
#define UEP_RETRY_NAKS                      0x40    // UEP0 - No automatic retry of NAK'd transactions
#define UEP_NO_RETRY_NAKS                   0x00    // UEP0 - Automatic retry of NAK'd transactions
#define UEP_NO_SETUP_TRANSFERS              0x10    // UEP0 - SETUP transfers not allowed
#define UEP_ALLOW_SETUP_TRANSFERS           0x00    // UEP0 - SETUP transfers allowed
#define UEP_RX_ENABLE                       0x08    // UEP0 - Endpoint can receive data
#define UEP_RX_DISABLE                      0x00    // UEP0 - Endpoint cannot receive data
#define UEP_TX_ENABLE                       0x04    // UEP0 - Endpoint can transmit data
#define UEP_TX_DISABLE                      0x00    // UEP0 - Endpoint cannot transmit data
#define UEP_HANDSHAKE_ENABLE                0x01    // UEP0 - Endpoint handshake enabled
#define UEP_HANDSHAKE_DISABLE               0x00    // UEP0 - Endpoint handshake disabled (isochronous endpoints)

#define USB_ENDPOINT_CONTROL_BULK           (UEP_NO_SETUP_TRANSFERS | UEP_RX_ENABLE | UEP_TX_ENABLE | UEP_HANDSHAKE_ENABLE) //
#define USB_ENDPOINT_CONTROL_ISOCHRONOUS    (UEP_NO_SETUP_TRANSFERS | UEP_RX_ENABLE | UEP_TX_ENABLE )                       //
#define USB_ENDPOINT_CONTROL_INTERRUPT      (UEP_NO_SETUP_TRANSFERS | UEP_RX_ENABLE | UEP_TX_ENABLE | UEP_HANDSHAKE_ENABLE) //
#define USB_ENDPOINT_CONTROL_SETUP          (UEP_RX_ENABLE | UEP_TX_ENABLE | UEP_HANDSHAKE_ENABLE)                          //

#define USB_DISABLE_ENDPOINT                0x00    // Value to disable an endpoint.

#define USB_SOF_THRESHOLD_08                0x12    // U1SOF - Threshold for a max packet size of 8
#define USB_SOF_THRESHOLD_16                0x1A    // U1SOF - Threshold for a max packet size of 16
#define USB_SOF_THRESHOLD_32                0x2A    // U1SOF - Threshold for a max packet size of 32
#define USB_SOF_THRESHOLD_64                0x4A    // U1SOF - Threshold for a max packet size of 64

#define USB_1MS_TIMER_FLAG                  0x40
#ifndef USB_INSERT_TIME
    #define USB_INSERT_TIME                 (250+1) // Insertion delay time (spec minimum is 100 ms)
#endif
//#define USB_RESET_TIME                      (50+1)  // RESET signaling time - 50ms
#define USB_RESET_TIME                      (100+1)  // RESET signaling time - 100ms
#if defined( __C30__ ) || defined __XC16__
    #define USB_RESET_RECOVERY_TIME         (10+1)  // RESET recovery time.
#elif defined( __PIC32__ )
    #define USB_RESET_RECOVERY_TIME         (100+1) // RESET recovery time - Changed to 100 ms from 10ms.  Some devices take longer.
#else
    #error Unknown USB_RESET_RECOVERY_TIME
#endif
#define USB_RESUME_TIME                     (20+1)  // RESUME signaling time - 20 ms
#define USB_RESUME_RECOVERY_TIME            (10+1)  // RESUME recovery time - 10 ms


//******************************************************************************
//******************************************************************************
// Section: Data Structures
//
// These data structures are all internal to the stack.
//******************************************************************************
//******************************************************************************

// *****************************************************************************
typedef struct _USB_HOST_INFO
{
    uint8_t                errorCode;                          // Error code of last operation.

    union
    {
        struct
        {
            uint8_t        bfSupportsOTG               : 1;    // If the device supports OTG (default = 0).
            uint8_t        bfConfiguredOTG             : 1;    // If OTG on the device has been configured (default = 0).
            uint8_t        bfAllowHNP                  : 1;    // If Host Negotiation Protocol is allowed (default = 0).
        };
        uint8_t            val;
    }                      flags;
    uint8_t                tempCountConfigurations;            // Count the Configuration Descriptors read during enumeration.
    uint8_t                reservedAddress;                    // Set reserve address on SET_ADDRESS setup
    uint16_t               wEP0DataSize;
    USB_ENDPOINT_DATA     *pEP0Data;

    USB_DEVICE_INFO       *pCurrentDeviceInfo;                // Prosessing device information
    USB_ENDPOINT_INFO     *pEndpoint0;                        // Pointer to a structure that describes EP0.
} USB_HOST_INFO;

/* ***********************************************/
typedef void (*USB_TIMER_HANDLER)(void);

// *****************************************************************************
/* USB Root Hub Information

This structure contains information about the USB root hub.
*/

typedef struct _USB_ROOT_HUB_INFO
{
    union
    {
        struct
        {
            uint8_t        bPowerGoodPort0 : 1;    // Power can turned on
        };
        uint8_t            val;
    }                   flags;
} USB_ROOT_HUB_INFO;


// *****************************************************************************
/* Event Data

This structure defines the data associated with any USB events (see USB_EVENT)
that can be generated by the USB ISR (see _USB1Interrupt).  These events and
their associated data are placed in an event queue used to synchronize between
the main host-tasks loop (see USBHostTasks) and the ISR.  This queue is required
only if transfer events are being used.  All other events are send directly to
the client drivers.
*/
#if defined( USB_ENABLE_TRANSFER_EVENT )
    typedef struct
    {
        USB_EVENT               event;          // Event that occured.
        USB_DEVICE_INFO        *deviceInfo;     // Device Information 
        HOST_TRANSFER_DATA      TransferData;   // Event: EVENT_TRANSFER,
    } USB_EVENT_DATA;
#endif

// *****************************************************************************
/* Transfer Data
*/
typedef struct
{
    USB_DEVICE_INFO    *deviceInfo;
    USB_ENDPOINT_INFO  *endpointInfo;
} USB_TRANSFER_DATA;

/********************************************************************
 * USB Endpoint Control Registers
 *******************************************************************/

// See _UEP data type for EP Control Register definitions in the
// processor-specific header files.

#define UEPList (*((_UEP*)&U1EP0))


//******************************************************************************
//******************************************************************************
// Section: Macros
//
// These macros are all internal to the host layer.
//******************************************************************************
//******************************************************************************

#define _USB_InitErrorCounters()        { numCommandTries   = USB_NUM_COMMAND_TRIES; }
#define _USB_SetErrorCode(obj,x)        { (obj).errorCode = x; }
#define _USB_SetHoldState()             { usbHostState = STATE_HOLDING; }
#define _USB_SetRunningState()          { usbHostState = STATE_RUNNING; }
#define _USB_SetNextState()             { usbHostState = (usbHostState & STATE_MASK) + NEXT_STATE; }
#define _USB_SetNextSubState()          { usbHostState = (usbHostState & (STATE_MASK | SUBSTATE_MASK)) + NEXT_SUBSTATE; }
#define _USB_SetNextSubSubState()       { usbHostState =  usbHostState + NEXT_SUBSUBSTATE; }
#define _USB_SetPreviousSubSubState()   { usbHostState =  usbHostState - NEXT_SUBSUBSTATE; }
#define _USB_SetTransferErrorState(x)   { x->transferState = (x->transferState & TSTATE_MASK) | TSUBSTATE_ERROR; }


//******************************************************************************
//******************************************************************************
// Section: Local Prototypes
//******************************************************************************
//******************************************************************************

void                 USBHostDeviceDescs_Clear( uint8_t deviceAddress );
void                 USBHost_StartTimer(uint8_t ms, USB_TIMER_HANDLER handler);

void                 _USB_CheckCommandAndEnumerationAttempts( void );
bool                 _USB_FindClassDriver( uint8_t bClass, uint8_t bSubClass, uint8_t bProtocol, uint8_t *pbClientDrv );
bool                 _USB_FindDeviceLevelClientDriver( USB_DEVICE_DESCRIPTOR *pDesc );
USB_ENDPOINT_INFO *  _USB_FindEndpoint( USB_DEVICE_INFO *deviceInfo, uint8_t endpoint );
USB_INTERFACE_INFO * _USB_FindInterface ( uint8_t bInterface, uint8_t bAltSetting );
bool                 _USB_FindServiceEndpoint( uint8_t transferType );
void                 _USB_FreeMemory( void );
//void                 _USB_InitControlReadWrite( bool is_write, USB_ENDPOINT_INFO *pEndpoint, uint8_t *pControlData, uint16_t controlSize,
//                              uint8_t *pData, uint16_t size );
//void                 _USB_InitControlWrite( USB_ENDPOINT_INFO *pEndpoint, uint8_t *pControlData, uint16_t controlSize,
//                               uint8_t *pData, uint16_t size );
//void                 _USB_InitReadWrite( bool is_write, USB_ENDPOINT_INFO *pEndpoint, uint8_t *pData, uint16_t size );
//void                 _USB_InitWrite( USB_ENDPOINT_INFO *pEndpoint, uint8_t *pData, uint16_t size );
void                 _USB_NotifyClients( USB_DEVICE_INFO *deviceInfo, USB_EVENT event, void *data, unsigned int size );
bool                 _USB_ParseConfigurationDescriptor( uint8_t *descriptor );
void                 _USB_ResetDATA0( USB_DEVICE_INFO *deviceInfo, uint8_t endpoint );
bool                 _USB_TransferInProgress( void );
void                 USBHost_DecreaseInterval( void );


#endif // _USB_HOST_LOCAL_


