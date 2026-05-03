#include <stdio.h>
#include <stdlib.h>
#include <libusb-1.0/libusb.h>

#define VENDOR_ID    0x1871
#define PRODUCT_ID   0x01b0

int main(void) {
    libusb_device_handle *handle = NULL;
    libusb_context *ctx = NULL;
    int r;
    char product[256];

    r = libusb_init(&ctx);
    if (r < 0) {
        fprintf(stderr, "Błąd inicjalizacji libusb: %s\n", libusb_error_name(r));
        return 1;
    }

    libusb_set_debug(ctx, 3);  // Opcjonalne: więcej logów

    handle = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if (!handle) {
        fprintf(stderr, "Nie znaleziono urządzenia USB: %04x:%04x\n", VENDOR_ID, PRODUCT_ID);
        libusb_exit(ctx);
        return 1;
    }

    struct libusb_device_descriptor desc;
    libusb_device *dev = libusb_get_device(handle);
    r = libusb_get_device_descriptor(dev, &desc);
    if (r < 0) {
        fprintf(stderr, "Nie można pobrać deskryptora urządzenia: %s\n", libusb_error_name(r));
        libusb_close(handle);
        libusb_exit(ctx);
        return 1;
    }

    // Odczyt napisu z deskryptora produktu (index 2 zazwyczaj)
    if (desc.iProduct) {
        r = libusb_get_string_descriptor_ascii(handle, desc.iProduct, (unsigned char *)product, sizeof(product));
        if (r > 0) {
            printf("Nazwa produktu: %s\n", product);
        } else {
            fprintf(stderr, "Nie można odczytać nazwy produktu: %s\n", libusb_error_name(r));
        }
    } else {
        printf("Deskryptor produktu nie jest dostępny (brak indexu).\n");
    }

    libusb_close(handle);
    libusb_exit(ctx);
    return 0;
}
