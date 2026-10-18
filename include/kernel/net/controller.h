#ifndef KERNEL_NET_CONTROLLER_H
#define KERNEL_NET_CONTROLLER_H

struct virtio_net_controller;
struct rtl8111_controller;
struct usb_ncm_nic;

enum driver_kind { DRIVER_NONE, DRIVER_VIRTIO, DRIVER_RTL8111, DRIVER_USB_NCM };

/* The worker binds once; each driver retains its boot-lifetime hardware state. */
struct net_driver_binding {
  enum driver_kind kind;
  union {
    struct virtio_net_controller *virtio;
    struct rtl8111_controller *rtl;
    struct usb_ncm_nic *ncm;
  };
};

#endif
