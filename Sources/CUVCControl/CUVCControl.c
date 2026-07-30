#include "CUVCControl.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <stdlib.h>
#include <string.h>

enum {
    CC_UVC_OK = 0,
    CC_UVC_NOT_FOUND = -1001,
    CC_UVC_PLUGIN_FAILED = -1002,
    CC_UVC_INTERFACE_FAILED = -1003,
    CC_UVC_DESCRIPTOR_FAILED = -1004,
    CC_UVC_CONTROL_FAILED = -1005
};

enum {
    UVC_SET_CUR = 0x01,
    UVC_GET_CUR = 0x81,
    UVC_GET_MIN = 0x82,
    UVC_GET_MAX = 0x83,
    UVC_GET_DEF = 0x87
};

struct CleanCamUVCHandle {
    IOUSBInterfaceInterface190 **interface;
    uint8_t interface_number;
    uint8_t camera_terminal_id;
    uint8_t processing_unit_id;
};

static int query_plugin(
    io_service_t service,
    CFUUIDRef service_type,
    IOCFPlugInInterface ***out_plugin
) {
    SInt32 score = 0;
    IOCFPlugInInterface **plugin = NULL;
    kern_return_t result = IOCreatePlugInInterfaceForService(
        service,
        service_type,
        kIOCFPlugInInterfaceID,
        &plugin,
        &score
    );
    if (result != kIOReturnSuccess || plugin == NULL) {
        return CC_UVC_PLUGIN_FAILED;
    }
    *out_plugin = plugin;
    return CC_UVC_OK;
}

static int parse_uvc_descriptors(
    IOUSBDeviceInterface **device,
    uint8_t *interface_number,
    uint8_t *camera_terminal_id,
    uint8_t *processing_unit_id
) {
    IOUSBConfigurationDescriptorPtr descriptor = NULL;
    IOReturn result = (*device)->GetConfigurationDescriptorPtr(device, 0, &descriptor);
    if (result != kIOReturnSuccess || descriptor == NULL) {
        return CC_UVC_DESCRIPTOR_FAILED;
    }

    const uint8_t *bytes = (const uint8_t *)descriptor;
    uint16_t total_length = descriptor->wTotalLength;
    uint16_t offset = 0;
    bool in_video_control = false;

    *interface_number = 0xff;
    *camera_terminal_id = 0xff;
    *processing_unit_id = 0xff;

    while (offset + 2 <= total_length) {
        uint8_t length = bytes[offset];
        uint8_t type = bytes[offset + 1];
        if (length < 2 || offset + length > total_length) break;

        if (type == kUSBInterfaceDesc && length >= 9) {
            uint8_t interface_class = bytes[offset + 5];
            uint8_t interface_subclass = bytes[offset + 6];
            in_video_control = interface_class == 0x0e && interface_subclass == 0x01;
            if (in_video_control) {
                *interface_number = bytes[offset + 2];
            }
        } else if (in_video_control && type == 0x24 && length >= 4) {
            uint8_t subtype = bytes[offset + 2];
            if (subtype == 0x02) {
                *camera_terminal_id = bytes[offset + 3];
            } else if (subtype == 0x05) {
                *processing_unit_id = bytes[offset + 3];
            }
        }
        offset += length;
    }

    if (*interface_number == 0xff ||
        *camera_terminal_id == 0xff ||
        *processing_unit_id == 0xff) {
        return CC_UVC_DESCRIPTOR_FAILED;
    }
    return CC_UVC_OK;
}

static int create_control_interface(
    IOUSBDeviceInterface **device,
    IOUSBInterfaceInterface190 ***out_interface
) {
    IOUSBFindInterfaceRequest request = {
        .bInterfaceClass = 0x0e,
        .bInterfaceSubClass = 0x01,
        .bInterfaceProtocol = kIOUSBFindInterfaceDontCare,
        .bAlternateSetting = kIOUSBFindInterfaceDontCare
    };
    io_iterator_t iterator = IO_OBJECT_NULL;
    IOReturn result = (*device)->CreateInterfaceIterator(device, &request, &iterator);
    if (result != kIOReturnSuccess) return CC_UVC_INTERFACE_FAILED;

    int status = CC_UVC_INTERFACE_FAILED;
    io_service_t interface_service;
    while ((interface_service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        IOCFPlugInInterface **plugin = NULL;
        if (query_plugin(interface_service, kIOUSBInterfaceUserClientTypeID, &plugin) == CC_UVC_OK) {
            LPVOID raw_interface = NULL;
            HRESULT query_result = (*plugin)->QueryInterface(
                plugin,
                CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID),
                &raw_interface
            );
            (*plugin)->Release(plugin);
            if (query_result == S_OK && raw_interface != NULL) {
                *out_interface = (IOUSBInterfaceInterface190 **)raw_interface;
                status = CC_UVC_OK;
                IOObjectRelease(interface_service);
                break;
            }
        }
        IOObjectRelease(interface_service);
    }
    IOObjectRelease(iterator);
    return status;
}

int cleancam_uvc_open(
    uint16_t vendor_id,
    uint16_t product_id,
    CleanCamUVCHandle **out_handle
) {
    if (out_handle == NULL) return CC_UVC_INTERFACE_FAILED;
    *out_handle = NULL;

    CFMutableDictionaryRef matching = IOServiceMatching(kIOUSBDeviceClassName);
    if (matching == NULL) return CC_UVC_NOT_FOUND;

    CFNumberRef vendor = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt16Type, &vendor_id);
    CFNumberRef product = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt16Type, &product_id);
    CFDictionarySetValue(matching, CFSTR(kUSBVendorID), vendor);
    CFDictionarySetValue(matching, CFSTR(kUSBProductID), product);
    CFRelease(vendor);
    CFRelease(product);

    io_iterator_t iterator = IO_OBJECT_NULL;
    kern_return_t match_result = IOServiceGetMatchingServices(
        kIOMainPortDefault,
        matching,
        &iterator
    );
    if (match_result != kIOReturnSuccess) return CC_UVC_NOT_FOUND;

    int status = CC_UVC_NOT_FOUND;
    io_service_t service;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        IOCFPlugInInterface **plugin = NULL;
        if (query_plugin(service, kIOUSBDeviceUserClientTypeID, &plugin) != CC_UVC_OK) {
            IOObjectRelease(service);
            continue;
        }

        LPVOID raw_device = NULL;
        HRESULT query_result = (*plugin)->QueryInterface(
            plugin,
            CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
            &raw_device
        );
        (*plugin)->Release(plugin);
        if (query_result != S_OK || raw_device == NULL) {
            IOObjectRelease(service);
            continue;
        }

        IOUSBDeviceInterface **usb_device = (IOUSBDeviceInterface **)raw_device;
        uint8_t interface_number = 0xff;
        uint8_t camera_terminal = 0xff;
        uint8_t processing_unit = 0xff;
        status = parse_uvc_descriptors(
            usb_device,
            &interface_number,
            &camera_terminal,
            &processing_unit
        );

        IOUSBInterfaceInterface190 **control_interface = NULL;
        if (status == CC_UVC_OK) {
            status = create_control_interface(usb_device, &control_interface);
        }
        (*usb_device)->Release(usb_device);
        IOObjectRelease(service);

        if (status == CC_UVC_OK && control_interface != NULL) {
            CleanCamUVCHandle *handle = calloc(1, sizeof(CleanCamUVCHandle));
            if (handle == NULL) {
                (*control_interface)->Release(control_interface);
                status = CC_UVC_INTERFACE_FAILED;
                break;
            }
            handle->interface = control_interface;
            handle->interface_number = interface_number;
            handle->camera_terminal_id = camera_terminal;
            handle->processing_unit_id = processing_unit;
            *out_handle = handle;
            break;
        }
    }
    IOObjectRelease(iterator);
    return status;
}

void cleancam_uvc_close(CleanCamUVCHandle *handle) {
    if (handle == NULL) return;
    if (handle->interface != NULL) {
        (*handle->interface)->Release(handle->interface);
    }
    free(handle);
}

static int control_request(
    CleanCamUVCHandle *handle,
    bool input,
    uint8_t request_code,
    uint8_t selector,
    uint8_t unit_id,
    void *data,
    uint16_t length
) {
    if (handle == NULL || handle->interface == NULL) return CC_UVC_INTERFACE_FAILED;

    IOUSBDevRequest request;
    memset(&request, 0, sizeof(request));
    request.bmRequestType = input ? 0xa1 : 0x21;
    request.bRequest = request_code;
    request.wValue = (uint16_t)selector << 8;
    request.wIndex = ((uint16_t)unit_id << 8) | handle->interface_number;
    request.wLength = length;
    request.pData = data;

    IOReturn result = (*handle->interface)->ControlRequest(
        handle->interface,
        0,
        &request
    );
    return result == kIOReturnSuccess ? CC_UVC_OK : (int)result;
}

static int get_uint32(
    CleanCamUVCHandle *handle,
    uint8_t request_code,
    uint8_t selector,
    uint8_t unit_id,
    uint32_t *value
) {
    return control_request(handle, true, request_code, selector, unit_id, value, sizeof(*value));
}

static int get_uint16(
    CleanCamUVCHandle *handle,
    uint8_t request_code,
    uint8_t selector,
    uint8_t unit_id,
    uint16_t *value
) {
    return control_request(handle, true, request_code, selector, unit_id, value, sizeof(*value));
}

int cleancam_uvc_get_exposure(CleanCamUVCHandle *handle, CleanCamUVCRange *out_range) {
    if (handle == NULL || out_range == NULL) return CC_UVC_CONTROL_FAILED;
    uint32_t minimum = 0, maximum = 0, current = 0, default_value = 0;
    int result = get_uint32(handle, UVC_GET_MIN, 0x04, handle->camera_terminal_id, &minimum);
    if (result != CC_UVC_OK) return result;
    result = get_uint32(handle, UVC_GET_MAX, 0x04, handle->camera_terminal_id, &maximum);
    if (result != CC_UVC_OK) return result;
    result = get_uint32(handle, UVC_GET_CUR, 0x04, handle->camera_terminal_id, &current);
    if (result != CC_UVC_OK) return result;
    result = get_uint32(handle, UVC_GET_DEF, 0x04, handle->camera_terminal_id, &default_value);
    if (result != CC_UVC_OK) return result;
    out_range->minimum = (int)minimum;
    out_range->maximum = (int)maximum;
    out_range->current = (int)current;
    out_range->default_value = (int)default_value;
    return minimum < maximum ? CC_UVC_OK : CC_UVC_CONTROL_FAILED;
}

int cleancam_uvc_set_exposure(CleanCamUVCHandle *handle, uint32_t value) {
    int result = cleancam_uvc_set_auto_exposure(handle, false);
    if (result != CC_UVC_OK) return result;
    return control_request(
        handle,
        false,
        UVC_SET_CUR,
        0x04,
        handle->camera_terminal_id,
        &value,
        sizeof(value)
    );
}

int cleancam_uvc_get_gain(CleanCamUVCHandle *handle, CleanCamUVCRange *out_range) {
    if (handle == NULL || out_range == NULL) return CC_UVC_CONTROL_FAILED;
    uint16_t minimum = 0, maximum = 0, current = 0, default_value = 0;
    int result = get_uint16(handle, UVC_GET_MIN, 0x04, handle->processing_unit_id, &minimum);
    if (result != CC_UVC_OK) return result;
    result = get_uint16(handle, UVC_GET_MAX, 0x04, handle->processing_unit_id, &maximum);
    if (result != CC_UVC_OK) return result;
    result = get_uint16(handle, UVC_GET_CUR, 0x04, handle->processing_unit_id, &current);
    if (result != CC_UVC_OK) return result;
    result = get_uint16(handle, UVC_GET_DEF, 0x04, handle->processing_unit_id, &default_value);
    if (result != CC_UVC_OK) return result;
    out_range->minimum = (int)minimum;
    out_range->maximum = (int)maximum;
    out_range->current = (int)current;
    out_range->default_value = (int)default_value;
    return minimum < maximum ? CC_UVC_OK : CC_UVC_CONTROL_FAILED;
}

int cleancam_uvc_set_gain(CleanCamUVCHandle *handle, uint16_t value) {
    return control_request(
        handle,
        false,
        UVC_SET_CUR,
        0x04,
        handle->processing_unit_id,
        &value,
        sizeof(value)
    );
}

int cleancam_uvc_get_auto_exposure(CleanCamUVCHandle *handle, bool *out_enabled) {
    if (handle == NULL || out_enabled == NULL) return CC_UVC_CONTROL_FAILED;
    uint8_t mode = 0;
    int result = control_request(
        handle,
        true,
        UVC_GET_CUR,
        0x02,
        handle->camera_terminal_id,
        &mode,
        sizeof(mode)
    );
    if (result == CC_UVC_OK) *out_enabled = mode != 1;
    return result;
}

int cleancam_uvc_set_auto_exposure(CleanCamUVCHandle *handle, bool enabled) {
    if (handle == NULL) return CC_UVC_CONTROL_FAILED;
    uint8_t mode = enabled ? 8 : 1;
    int result = control_request(
        handle,
        false,
        UVC_SET_CUR,
        0x02,
        handle->camera_terminal_id,
        &mode,
        sizeof(mode)
    );
    if (enabled && result != CC_UVC_OK) {
        mode = 2;
        result = control_request(
            handle,
            false,
            UVC_SET_CUR,
            0x02,
            handle->camera_terminal_id,
            &mode,
            sizeof(mode)
        );
    }
    return result;
}

int cleancam_uvc_disable_backlight_compensation(CleanCamUVCHandle *handle) {
    if (handle == NULL) return CC_UVC_CONTROL_FAILED;
    uint16_t value = 0;
    return control_request(
        handle,
        false,
        UVC_SET_CUR,
        0x01,
        handle->processing_unit_id,
        &value,
        sizeof(value)
    );
}

const char *cleancam_uvc_error_string(int code) {
    switch (code) {
        case CC_UVC_OK: return "success";
        case CC_UVC_NOT_FOUND: return "USB camera not found";
        case CC_UVC_PLUGIN_FAILED: return "macOS could not create the USB control plug-in";
        case CC_UVC_INTERFACE_FAILED: return "macOS could not open the USB video-control interface";
        case CC_UVC_DESCRIPTOR_FAILED: return "the camera's UVC control descriptors could not be parsed";
        case CC_UVC_CONTROL_FAILED: return "the camera does not report this UVC control";
        default: return "the USB control request was rejected";
    }
}
