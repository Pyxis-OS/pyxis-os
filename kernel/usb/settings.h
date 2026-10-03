#ifndef USB_SETTINGS_H
#define USB_SETTINGS_H

/* Initial resource/deadline choices, independent of image and machine sizes. */
#define USB_STATE_TIMEOUT_MS 1000
#define USB_COMMAND_TIMEOUT_MS 5000
#define USB_CONTROL_TIMEOUT_MS 5000
#define USB_ENUMERATION_TIMEOUT_MS 30000
#define USB_PORT_POWER_DELAY_MS 20
#define USB_WORKER_POLL_MS 10
#define USB_CONTROL_BYTES 4096
#define USB_INTERFACE_BUDGET 512

#endif
