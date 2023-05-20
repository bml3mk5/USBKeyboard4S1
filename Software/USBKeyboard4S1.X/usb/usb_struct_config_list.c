/** @file usb_struct_config_list.c
 *
 *  @author Sasaji
 *  @date 2023/04/12
 *
 *  @brief Treat a list of configuration descriptor
 */

#include "common.h"
#include "usb_host_local.h"
#include "usb_struct_config_list.h"
#include <string.h>
#include "uart.h"

/****************************************************************************
  Function:
    void USBStructConfigList_Initialize( )

  Description:
    This function initialize the structure of configuration descriptor list

  Precondition:
    None

  Parameters:
    USB_CONFIGURATION *list - pointer of structure

  Returns:
    None

  Remarks:
    None
  ***************************************************************************/
void USBStructConfigList_Initialize(USB_CONFIGURATION *list)
{
    if (!list) return;

    memset(list, 0, sizeof(USB_CONFIGURATION));
}

/****************************************************************************
  Function:
    void USBStructConfigList_Initialize( )

  Description:
    This function frees configuration descriptor and list

  Precondition:
    None

  Parameters:
    USB_CONFIGURATION **list - pointer pointer of structure

  Returns:
    None

  Remarks:
    None
  ***************************************************************************/
void USBStructConfigList_Clear(USB_CONFIGURATION **list)
{
    USB_CONFIGURATION *pList = *list;
    USB_CONFIGURATION *pNext;

    while (pList) {
        pNext = pList->next;
        USB_FREE( pList->descriptor );
        USB_FREE( pList );
        pList = pNext;
    }
    *list = pList;
}

/****************************************************************************
  Function:
    void USBStructConfigList_PushFront()

  Description:
    This function adds an item of configuration at the front of the list

  Precondition:
    None

  Parameters:
    USB_CONFIGURATION **list - pointer pointer of structure
    uint8_t config_number    - number
    uint16_t descriptor_size - size

  Returns:
     1 - Success
     0 - malloc error

  Remarks:
    None
  ***************************************************************************/
int USBStructConfigList_PushFront(USB_CONFIGURATION **list, uint8_t config_number, uint16_t descriptor_size)
{
    USB_CONFIGURATION *pNewItem;

    // Allocate a buffer for an entry in the configuration descriptor list.
    if ((pNewItem = (USB_CONFIGURATION *)USB_MALLOC(sizeof(USB_CONFIGURATION))) == NULL)
    {
        return 0;
    }

    // Allocate a buffer for the entire Configuration Descriptor
    if ((pNewItem->descriptor = (USB_CONFIGURATION_DESCRIPTOR *)USB_MALLOC(descriptor_size * sizeof(uint8_t))) == NULL)
    {
        // Not enough memory for the descriptor!
        USB_FREE(pNewItem);
        
        return 0;
    }

    // Save wTotalLength
    pNewItem->descriptor->wTotalLength = descriptor_size;
    
    // number
    pNewItem->configNumber = config_number;

    // Put the new node at the front of the list.
    pNewItem->next = *list;
    *list = pNewItem;

    return 1;
}

/****************************************************************************
  Function:
    void USBStructConfigList_FindItemByNumber( )

  Description:
    This function searches an item of configuration matching specified number

  Precondition:
    None

  Parameters:
    USB_CONFIGURATION  *list - pointer of structure
    uint8_t config_number    - number

  Returns:
    an item or NULL

  Remarks:
    None
  ***************************************************************************/
USB_CONFIGURATION *USBStructConfigList_FindItemByNumber(USB_CONFIGURATION *list, uint8_t config_number)
{
    while (list && list->configNumber != config_number)
    {
        list = list->next;
    }
    return list;
}

/*************************************************************************
 * EOF usb_struct_config_list.c
 */
