/** @file usb_struct_config_list.h
 *
 *  @author Sasaji
 *  @date 2023/04/12
 *
 *  @brief Treat a list of configuration descriptor
 */

#ifndef USB_STRUCT_CONFIG_LIST_H
#define	USB_STRUCT_CONFIG_LIST_H

#include "usb_ch9.h"

// *****************************************************************************
/* USB Configuration Node

This structure is used to make a linked list of all the configuration
descriptors of an attached device.
*/
typedef struct _USB_CONFIGURATION
{
    struct _USB_CONFIGURATION     *next;          // Pointer to next node.
    USB_CONFIGURATION_DESCRIPTOR  *descriptor;    // Complete Configuration Descriptor.
    uint8_t                        configNumber;  // Number of this Configuration.
} USB_CONFIGURATION;

void USBStructConfigList_Initialize(USB_CONFIGURATION *list);
void USBStructConfigList_Clear(USB_CONFIGURATION **list);
int USBStructConfigList_PushFront(USB_CONFIGURATION **list, uint8_t config_number, uint16_t descriptor_size);
USB_CONFIGURATION *USBStructConfigList_FindItemByNumber(USB_CONFIGURATION *list, uint8_t config_number);

#endif	/* USB_STRUCT_CONFIG_LIST_H */

