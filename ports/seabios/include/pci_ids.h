/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * OpenRFS environment for the vendored SeaBIOS drivers: the PCI class codes
 * (PCI Code and ID Assignment Specification) and the vendor and device
 * identifiers the vendored drivers match, with SeaBIOS's names.
 */
#ifndef OPENRFS_SEABIOS_PCI_IDS_H
#define OPENRFS_SEABIOS_PCI_IDS_H

#define PCI_CLASS_STORAGE_SCSI          0x0100
#define PCI_CLASS_STORAGE_IDE           0x0101
#define PCI_CLASS_STORAGE_SATA          0x0106
#define PCI_CLASS_STORAGE_NVME          0x0108
#define PCI_CLASS_STORAGE_OTHER         0x0180
#define PCI_CLASS_DISPLAY_VGA           0x0300
#define PCI_CLASS_BRIDGE_ISA            0x0601
#define PCI_CLASS_SYSTEM_SDHCI          0x0805
#define PCI_CLASS_SERIAL_USB            0x0c03
#define PCI_CLASS_SERIAL_USB_UHCI       0x0c0300
#define PCI_CLASS_SERIAL_USB_OHCI       0x0c0310
#define PCI_CLASS_SERIAL_USB_EHCI       0x0c0320
#define PCI_CLASS_SERIAL_USB_XHCI       0x0c0330

#define PCI_VENDOR_ID_LSI_LOGIC         0x1000
#define PCI_DEVICE_ID_LSI_53C895A       0x0012
#define PCI_DEVICE_ID_LSI_SAS1068       0x0054
#define PCI_DEVICE_ID_LSI_SAS1068E      0x0058
#define PCI_DEVICE_ID_LSI_SAS1078       0x0060
#define PCI_DEVICE_ID_LSI_SAS1078DE     0x007C
#define PCI_DEVICE_ID_LSI_SAS1064R      0x0411
#define PCI_DEVICE_ID_LSI_VERDE_ZCR     0x0413
#define PCI_DEVICE_ID_LSI_53C1030       0x0030
#define PCI_DEVICE_ID_LSI_SAS2004       0x0071
#define PCI_DEVICE_ID_LSI_SAS2008       0x0073
#define PCI_DEVICE_ID_LSI_SAS2108       0x0079
#define PCI_DEVICE_ID_LSI_SAS2108E      0x0078
#define PCI_DEVICE_ID_LSI_SAS2208       0x005B
#define PCI_DEVICE_ID_LSI_SAS3108       0x005D

#define PCI_VENDOR_ID_ATI               0x1002
#define PCI_VENDOR_ID_AMD               0x1022
#define PCI_DEVICE_ID_AMD_SCSI          0x2020
#define PCI_VENDOR_ID_DELL              0x1028
#define PCI_DEVICE_ID_DELL_PERC5        0x0015
#define PCI_VENDOR_ID_VMWARE            0x15ad
#define PCI_DEVICE_ID_VMWARE_PVSCSI     0x07C0
#define PCI_VENDOR_ID_REDHAT_QUMRANET   0x1af4
#define PCI_DEVICE_ID_VIRTIO_BLK_09     0x1001
#define PCI_DEVICE_ID_VIRTIO_SCSI_09    0x1004
#define PCI_DEVICE_ID_VIRTIO_BLK_10     0x1042
#define PCI_DEVICE_ID_VIRTIO_SCSI_10    0x1048
#define PCI_VENDOR_ID_INTEL             0x8086

#endif
