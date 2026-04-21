#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "usb_parser.h"

#define VID         0x1871
#define PID         0x01b0
#define INTERFACE   0
#define TIMEOUT_MS  2000
#define FW_CHUNKS   154
#define FW_CHUNK_SZ 32

static int ctrl_out(libusb_device_handle *h,
                    uint8_t req, uint16_t val, uint16_t idx,
                    uint8_t *data, uint16_t len)
{
    int ret = libusb_control_transfer(h,
        LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE,
        req, val, idx, data, len, TIMEOUT_MS);
    if (ret < 0)
        fprintf(stderr, "  ctrl_out req=0x%02x idx=%u FAIL: %s\n",
                req, idx, libusb_error_name(ret));
    return ret;
}

static int ctrl_in(libusb_device_handle *h,
                   uint8_t req, uint16_t val, uint16_t idx,
                   uint8_t *data, uint16_t len)
{
    int ret = libusb_control_transfer(h,
        LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_IN,
        req, val, idx, data, len, TIMEOUT_MS);
    if (ret < 0)
        fprintf(stderr, "  ctrl_in  req=0x%02x idx=%u FAIL: %s\n",
                req, idx, libusb_error_name(ret));
    return ret;
}

static int open_device(libusb_context *ctx, libusb_device_handle **out)
{
    libusb_device_handle *h = libusb_open_device_with_vid_pid(ctx, VID, PID);
    if (!h) {
        fprintf(stderr, "Urządzenie %04x:%04x nie znalezione\n", VID, PID);
        return -1;
    }

    if (libusb_kernel_driver_active(h, INTERFACE) == 1) {
        printf("Odłączanie sterownika kernela...\n");
        if (libusb_detach_kernel_driver(h, INTERFACE) < 0) {
            fprintf(stderr, "Nie można odłączyć sterownika kernela\n");
            libusb_close(h);
            return -1;
        }
    }

    if (libusb_set_configuration(h, 1) < 0) {
        fprintf(stderr, "Błąd set_configuration\n");
        libusb_close(h);
        return -1;
    }

    if (libusb_claim_interface(h, INTERFACE) < 0) {
        fprintf(stderr, "Błąd claim_interface\n");
        libusb_close(h);
        return -1;
    }

    *out = h;
    return 0;
}

static int read_capability(libusb_device_handle *h)
{
    uint8_t buf[8] = {0};
    printf("[1] Odczyt capability (bRequest=0x01)...\n");
    int ret = ctrl_in(h, 0x01, 0, 0, buf, sizeof(buf));
    if (ret >= 0) {
        printf("    Capability: ");
        for (int i = 0; i < ret; i++) printf("%02x ", buf[i]);
        printf("\n");
    }
    return ret;
}

static int read_firmware_version(libusb_device_handle *h)
{
    uint8_t buf[12] = {0};
    printf("[2] Odczyt wersji firmware (bRequest=0x08)...\n");
    int ret = ctrl_in(h, 0x08, 0, 0, buf, sizeof(buf));
    if (ret >= 0) {
        printf("    Firmware: \"%.12s\"\n", buf);
    }
    return ret;
}

static int upload_firmware(libusb_device_handle *h, UsbRequest *reqs, int n)
{
    printf("[3] Upload firmware (%d chunks × %d bajtów)...\n", n, FW_CHUNK_SZ);

    for (int i = 0; i < n; i++) {
        int ret = ctrl_out(h,
            reqs[i].bRequest,
            reqs[i].wValue,
            reqs[i].wIndex,
            reqs[i].data,
            reqs[i].wLength);
        if (ret < 0) {
            fprintf(stderr, "  FAIL przy chunk %d\n", i);
            return ret;
        }
        if (i % 20 == 0 || i == n - 1)
            printf("  chunk %3d/%d OK\n", i, n - 1);
    }
    return 0;
}

static int start_camera(libusb_device_handle *h)
{
    printf("[4] Start kamery (bRequest=5, wIndex=1)...\n");
    int ret = ctrl_out(h, 5, 0, 1, NULL, 0);
    if (ret >= 0)
        printf("    Kamera uruchomiona!\n");
    return ret;
}

int main(void)
{
    static UsbRequest reqs[FW_CHUNKS + 4];
    libusb_context *ctx = NULL;
    libusb_device_handle *handle = NULL;
    int ret = 0;

    if (libusb_init(&ctx) < 0) {
        fprintf(stderr, "Błąd inicjalizacji libusb\n");
        return 1;
    }
    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);

    printf("=== Inicjalizacja kamery AVEO %04x:%04x ===\n", VID, PID);

    if (open_device(ctx, &handle) < 0) {
        ret = 1;
        goto cleanup;
    }
    printf("Urządzenie otwarte.\n\n");

    /* Kroki inicjalizacji */
    if (read_capability(handle) < 0)      { ret = 1; goto cleanup; }
    if (read_firmware_version(handle) < 0) { ret = 1; goto cleanup; }

    int n = parse_request_file("AVEO_PAKIET_DATA.txt", reqs, FW_CHUNKS + 4);
    if (n <= 0) {
        fprintf(stderr, "Błąd parsowania pliku firmware\n");
        ret = 1;
        goto cleanup;
    }
    printf("    Wczytano %d requestów z pliku.\n\n", n);

    if (upload_firmware(handle, reqs, n) < 0) { ret = 1; goto cleanup; }
    if (start_camera(handle) < 0)             { ret = 1; goto cleanup; }

    printf("\n=== Inicjalizacja zakończona sukcesem ===\n");

cleanup:
    if (handle) {
        libusb_release_interface(handle, INTERFACE);
        libusb_attach_kernel_driver(handle, INTERFACE);
        libusb_close(handle);
    }
    libusb_exit(ctx);
    return ret;
}
