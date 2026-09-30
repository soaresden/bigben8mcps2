#ifndef XM_USBIO_H
#define XM_USBIO_H
#include "mcio.h"
#define XM_USBDIR "mass:/PS2SAVES"
int usb_ready(void);
int usb_list(Entry *out, int max);                 /* .psu de mass:/PS2SAVES */
int usb_read_all(const char *name, unsigned char **buf, unsigned int *len);
int usb_read_psu_file(const char *name, const char *inner, unsigned char **buf, unsigned int *len);
int usb_write(const char *name, const unsigned char *buf, unsigned int len);   /* ecrase */
int usb_exists(const char *name);
void usb_safe_name(const char *dir, char *out);   /* nom de fichier .psu pour un dossier */
#endif
