#ifndef KERNEL_NET_CONTROLLER_H
#define KERNEL_NET_CONTROLLER_H

struct virtio_net_controller;
struct rtl8111_controller;
struct usb_ncm_nic;

enum net_driver_kind { NET_DRIVER_KIND_NONE, NET_DRIVER_KIND_VIRTIO, NET_DRIVER_KIND_RTL8111, NET_DRIVER_KIND_USB_NCM };

/* The worker binds once; each driver retains its boot-lifetime hardware state. */
struct net_driver_binding {
  enum net_driver_kind kind;
  union {
    struct virtio_net_controller *virtio;
    struct rtl8111_controller *rtl;
    struct usb_ncm_nic *ncm;
  };
};

#endif
