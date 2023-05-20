/** @file usb_struct_interface.c
 *
 *  @author Sasaji
 *  @date 2023/04/12
 * 
 *  @brief Treat a list of interfaces and endpoints
 *  @note These function was moved from usb_host.c by Sasaji
 */

#include "common.h"
#include "usb_struct_interface.h"
#include "usb_host_local.h"
#include <string.h>
#include "uart.h"

/****************************************************************************
  Function:
    void _USB_EndpointList_Clear( USB_ENDPOINT_INFO **ppEndpoint )

  Description:
    This function frees the endpoint list

  Precondition:
    None

  Parameters:
    USB_ENDPOINT_INFO ** - pointer of pointer of endpoint list

  Returns:
    None

  Remarks:
    None
  ***************************************************************************/
void _USB_EndpointList_Clear( USB_ENDPOINT_INFO **ppEndpoint )
{
    USB_ENDPOINT_INFO *pNextEndpoint;

    while ((*ppEndpoint) != NULL)
    {
        pNextEndpoint = (*ppEndpoint)->next;
        USB_FREE_AND_CLEAR( (*ppEndpoint) );
        (*ppEndpoint) = pNextEndpoint;
    }
}

/****************************************************************************
  Function:
    void _USB_InterfaceSettings_Clear( USB_INTERFACE_SETTING_INFO **ppSettings )

  Description:
    This function frees the setting and endpoint lists associated
                with a configuration.

  Precondition:
    None

  Parameters:
    USB_INTERFACE_SETTING_INFO ** - pointer of pointer of interface settings

  Returns:
    None

  Remarks:
    None
  ***************************************************************************/
void _USB_InterfaceSettings_Clear( USB_INTERFACE_SETTING_INFO **ppSettings )
{
    USB_INTERFACE_SETTING_INFO  *pNextSetting;

    while ((*ppSettings) != NULL)
    {
        pNextSetting = (*ppSettings)->next;
        _USB_EndpointList_Clear(&(*ppSettings)->pEndpointList);
        USB_FREE_AND_CLEAR( (*ppSettings) );
        (*ppSettings) = pNextSetting;
    }

}

/****************************************************************************
  Function:
    void USB_InterfaceList_Clear( )

  Description:
    This function frees the interface, setting and endpoint lists associated
                with a configuration.

  Precondition:
    None

  Parameters:
    USB_INTERFACE_INFO ** - pointer of pointer of interface list

  Returns:
    None

  Remarks:
    None
  ***************************************************************************/
void USB_InterfaceList_Clear( USB_INTERFACE_INFO **ppInterface )
{
    USB_INTERFACE_INFO *pNextInterface;

    while ((*ppInterface) != NULL)
    {
        pNextInterface = (*ppInterface)->next;
        _USB_InterfaceSettings_Clear(&(*ppInterface)->pInterfaceSettings);
        USB_FREE_AND_CLEAR( (*ppInterface) );
        (*ppInterface) = pNextInterface;
    }
}

/****************************************************************************
  Function:
    void USB_InterfaceList_FindEndpointEx( )

  Description:
    This function searches the endpoint in the list

  Precondition:
    None

  Parameters:
    USB_INTERFACE_INFO * - pointer of interface list

  Returns:
    USB_ENDPOINT_INFO or NULL

  Remarks:
    None
  ***************************************************************************/
USB_ENDPOINT_INFO *USB_InterfaceList_FindEndpointEx( USB_INTERFACE_INFO *pInterfaceList, uint8_t interface, uint8_t setting, uint8_t endpoint )
{
    USB_ENDPOINT_INFO *pEndpoint = NULL;

    while (pInterfaceList) {
        if (pInterfaceList->interface == interface) {
            USB_INTERFACE_SETTING_INFO *pSetting = pInterfaceList->pInterfaceSettings;
            // Look for the endpoint in the currently active setting.
            while (pSetting) {
                if (pSetting->interfaceAltSetting == setting) {
                    pEndpoint = pSetting->pEndpointList;
                    while (pEndpoint) {
                        if (pEndpoint->bEndpointAddress == endpoint) {
                            // We have found the endpoint.
                            return pEndpoint;
                        }
                        pEndpoint = pEndpoint->next;
                    }
                }
                pSetting = pSetting->next;
            }
        }
        // Go to the next interface.
        pInterfaceList = pInterfaceList->next;
    }
    
    return NULL;
}

/****************************************************************************
  Function:
    void USB_InterfaceList_FindEndpoint( )

  Description:
    This function searches the endpoint in the list

  Precondition:
    None

  Parameters:
    USB_INTERFACE_INFO * - pointer of interface list

  Returns:
    USB_ENDPOINT_INFO or NULL

  Remarks:
    None
  ***************************************************************************/
USB_ENDPOINT_INFO *USB_InterfaceList_FindEndpoint( USB_INTERFACE_INFO *pInterfaceList, uint8_t endpoint )
{
    USB_ENDPOINT_INFO *pEndpoint = NULL;

    while (pInterfaceList)
    {
        // Look for the endpoint in the currently active setting.
        if (pInterfaceList->pCurrentSetting)
        {
            pEndpoint = pInterfaceList->pCurrentSetting->pEndpointList;
            while (pEndpoint)
            {
                if (pEndpoint->bEndpointAddress == endpoint)
                {
                    // We have found the endpoint.
                    return pEndpoint;
                }
                pEndpoint = pEndpoint->next;
            }
        }
        
        // Go to the next interface.
        pInterfaceList = pInterfaceList->next;
    }
    
    return NULL;
}

/****************************************************************************
  Function:
    void USB_InterfaceList_SetData0( )

  Description:
    This function searches the endpoint in the list

  Precondition:
    None

  Parameters:
    USB_INTERFACE_INFO * - pointer of interface list

  Returns:
    None

  Remarks:
    None
  ***************************************************************************/
void USB_InterfaceList_SetData0( USB_INTERFACE_INFO *pInterfaceList )
{
    USB_INTERFACE_SETTING_INFO *pSetting;
    USB_ENDPOINT_INFO *pEndpoint;

    while (pInterfaceList)
    {
        pSetting = pInterfaceList->pInterfaceSettings;
        while (pSetting)
        {
            pEndpoint = pSetting->pEndpointList;
            while (pEndpoint)
            {
                pEndpoint->status.bfNextDATA01 = 0;
                pEndpoint = pEndpoint->next;
            }
            pSetting = pSetting->next;
        }
        pInterfaceList = pInterfaceList->next;
    }
}

/****************************************************************************
  Function:
    void USB_InterfaceList_DecreaseInterval( )

  Description:
    This function searches the endpoint in the list

  Precondition:
    None

  Parameters:
    USB_INTERFACE_INFO * - pointer of interface list

  Returns:
    None

  Remarks:
    None
  ***************************************************************************/
void USB_InterfaceList_DecreaseInterval( USB_INTERFACE_INFO *pInterfaceList )
{
    USB_ENDPOINT_INFO *pEndpoint;

    while (pInterfaceList)
    {
        if (pInterfaceList->pCurrentSetting)
        {
            pEndpoint = pInterfaceList->pCurrentSetting->pEndpointList;
            while (pEndpoint)
            {
                // Decrement the interval count of all active interrupt and isochronous endpoints.
                if ((pEndpoint->bmAttributes.bfTransferType == USB_TRANSFER_TYPE_INTERRUPT)
#ifdef USB_SUPPORT_ISOCHRONOUS_TRANSFERS
                    || (pEndpoint->bmAttributes.bfTransferType == USB_TRANSFER_TYPE_ISOCHRONOUS)
#endif
                ) {
                    if (pEndpoint->wIntervalCount != 0)
                    {
                        pEndpoint->wIntervalCount--;
                    }
                    pEndpoint->status.bfIntervalCountIsZero = 0;
                    if (pEndpoint->wIntervalCount == 0)
                    {
                        pEndpoint->wIntervalCount = pEndpoint->wInterval;
                        pEndpoint->status.bfIntervalCountIsZero = 1;
                    }
                }

                #ifndef ALLOW_MULTIPLE_NAKS_PER_FRAME
                    pEndpoint->status.bfLastTransferNAKd = 0;
                #endif

                pEndpoint = pEndpoint->next;
            }
        }

        pInterfaceList = pInterfaceList->next;
    }
}

/****************************************************************************
  Function:
    void USB_InterfaceList_CheckInterface( )

  Description:
    This function check interface list for SET_INTERFACE

  Precondition:
    None

  Parameters:
    USB_INTERFACE_INFO * - pointer of interface list

  Returns:
    None

  Remarks:
    0 - invalid data found
    1 - success
  ***************************************************************************/
uint8_t USB_InterfaceList_CheckInterface( USB_INTERFACE_INFO *pInterfaceList, uint16_t wIndex, uint16_t wValue )
{
    USB_ENDPOINT_INFO           *pEndpoint;
    USB_INTERFACE_SETTING_INFO  *pSetting;

    // Make sure there are no transfers currently in progress on the current
    // interface setting.
    while (pInterfaceList && (pInterfaceList->interface != wIndex))
    {
        pInterfaceList = pInterfaceList->next;
    }
    if ((pInterfaceList == NULL) || (pInterfaceList->pCurrentSetting == NULL))
    {
        // The specified interface was not found.
        return 0;
    }
    pEndpoint = pInterfaceList->pCurrentSetting->pEndpointList;
    while (pEndpoint)
    {
        if (!pEndpoint->status.bfTransferComplete)
        {
            // An endpoint on this setting is still transferring data.
            return 0;
        }
        pEndpoint = pEndpoint->next;
    }

    // Make sure the new setting is valid.
    pSetting = pInterfaceList->pInterfaceSettings;
    while( pSetting && (pSetting->interfaceAltSetting != wValue))
    {
        pSetting = pSetting->next;
    }
    if (pSetting == NULL)
    {
        return 0;
    }

    // Set the pointer to the new setting.
    pInterfaceList->pCurrentSetting = pSetting;
    
    return 1;
}

/*************************************************************************
 * EOF usb_struct_interface.c
 */
