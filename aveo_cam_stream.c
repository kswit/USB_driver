#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <jpeglib.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "aveo_firmware.h"

#define VID         0x1871
#define PID         0x01b0
#define INTERFACE   0
#define TIMEOUT_MS  2000

static inline int decode_iso_pkt(uint16_t raw) {
    int size = raw & 0x7FF;
    int mult = (raw >> 11) & 3;
    return size * (mult + 1);
}

static libusb_context *g_ctx = NULL;
static volatile int g_stop   = 0;
static volatile int g_active = 0;

/* ---- helpers ------------------------------------------------ */
static int ctrl_out(libusb_device_handle *h, uint8_t req,
                    uint16_t val, uint16_t idx, uint8_t *data, uint16_t len)
{
    int r = libusb_control_transfer(h,
        LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_ENDPOINT,
        req, val, idx, data, len, TIMEOUT_MS);
    if (r < 0)
        fprintf(stderr, "  ctrl_out 0x%02x [%u] FAIL: %s\n",
                req, idx, libusb_error_name(r));
    return r;
}

static int ctrl_in(libusb_device_handle *h, uint8_t req,
                   uint16_t val, uint16_t idx, uint8_t *data, uint16_t len)
{
    int r = libusb_control_transfer(h,
        LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_ENDPOINT | LIBUSB_ENDPOINT_IN,
        req, val, idx, data, len, TIMEOUT_MS);
    if (r < 0)
        fprintf(stderr, "  ctrl_in  0x%02x [%u] FAIL: %s\n",
                req, idx, libusb_error_name(r));
    return r;
}

/* ---- list all endpoints ------------------------------------- */
static void list_endpoints(libusb_device *dev)
{
    struct libusb_config_descriptor *cfg;
    if (libusb_get_config_descriptor(dev, 0, &cfg) < 0) return;

    printf("=== Endpointy urządzenia ===\n");
    for (int i = 0; i < cfg->bNumInterfaces; i++) {
        const struct libusb_interface *iface = &cfg->interface[i];
        for (int a = 0; a < iface->num_altsetting; a++) {
            const struct libusb_interface_descriptor *alt = &iface->altsetting[a];
            printf("  Interface %d alt=%d  class=%02x endpoints=%d\n",
                   alt->bInterfaceNumber, alt->bAlternateSetting,
                   alt->bInterfaceClass, alt->bNumEndpoints);
            for (int e = 0; e < alt->bNumEndpoints; e++) {
                const struct libusb_endpoint_descriptor *ep = &alt->endpoint[e];
                const char *type[] = {"Control","Iso","Bulk","Interrupt"};
                const char *dir = (ep->bEndpointAddress & 0x80) ? "IN" : "OUT";
                printf("    EP 0x%02x  %s  %s  maxPkt=%d\n",
                       ep->bEndpointAddress,
                       type[ep->bmAttributes & 3], dir,
                       ep->wMaxPacketSize);
            }
        }
    }
    printf("\n");
    libusb_free_config_descriptor(cfg);
}

/* ---- find iso IN endpoint ---------------------------------- */
static int find_iso_ep(libusb_device *dev, int alt_setting, uint16_t *max_pkt)
{
    struct libusb_config_descriptor *cfg;
    if (libusb_get_config_descriptor(dev, 0, &cfg) < 0) return -1;

    int found = -1;
    const struct libusb_interface *iface = &cfg->interface[INTERFACE];
    for (int a = 0; a < iface->num_altsetting; a++) {
        const struct libusb_interface_descriptor *alt = &iface->altsetting[a];
        if (alt->bAlternateSetting != alt_setting) continue;
        for (int e = 0; e < alt->bNumEndpoints; e++) {
            const struct libusb_endpoint_descriptor *ep = &alt->endpoint[e];
            if ((ep->bmAttributes & 3) == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS &&
                (ep->bEndpointAddress & 0x80)) {
                found     = ep->bEndpointAddress;
                *max_pkt  = ep->wMaxPacketSize;
                break;
            }
        }
    }
    libusb_free_config_descriptor(cfg);
    return found;
}

/* ---- firmware upload --------------------------------------- */
static int upload_firmware(libusb_device_handle *h)
{
    printf("[FW] Upload %d chunks × %d bajtów...\n",
           AVEO_FW_CHUNKS, AVEO_FW_CHUNK_SZ);
    for (int i = 0; i < AVEO_FW_CHUNKS; i++) {
        int r = ctrl_out(h, 4, 0, (uint16_t)i,
                         (uint8_t *)aveo_firmware[i], AVEO_FW_CHUNK_SZ);
        if (r < 0) { fprintf(stderr, "  FAIL przy chunk %d\n", i); return -1; }
    }
    if (ctrl_out(h, 5, 0, 1, NULL, 0) < 0) return -1;
    printf("[FW] OK — czekam 500ms na boot 8051...\n\n");
    usleep(500000);
    return 0;
}

/* ---- odczyt parametrów ------------------------------------- */
static int configure_stream(libusb_device_handle *h)
{
    uint8_t buf[16];
    printf("[CFG] Odczyt parametrów kamery...\n");
    ctrl_in(h, 0x01, 0, 0, buf, 8);
    printf("  Capability: 0x%02x\n", buf[0]);
    ctrl_in(h, 0x08, 0, 0, buf, 12);
    printf("  Firmware: \"%.12s\"\n", buf);
    printf("[CFG] OK\n\n");
    return 0;
}

/* ---- sensor config (z FUN_00029b80 — wartości domyślne) ---- */
static void sensor_config(libusb_device_handle *h)
{
    int rc;
    printf("[SENSOR] Konfiguracja sensora...\n");
#define VCMD(req,val,idx) do { \
    rc = ctrl_out(h,req,val,idx,NULL,0); usleep(10000); \
    printf("  0x%02x val=0x%04x idx=0x%04x -> %d\n",(unsigned)(req),(unsigned)(val),(unsigned)(idx),rc); \
} while(0)
    VCMD(0x5c, 0x0000, 0x0000);
    VCMD(0x5d, 0x0000, 0x0000);
    VCMD(0x5e, 0x0000, 0x0000);
    VCMD(0x72, 0x0000, 0x0001);
    VCMD(0x90, 0x0000, 0x0000);
    VCMD(0x8f, 0x0000, 0x0000);
    VCMD(0x5e, 0x0000, 0x0000);
    VCMD(0x6c, 0x0032, 0x0002);
    VCMD(0x5f, 0x0001, 0x0000);   /* AE enable */
    usleep(200000);                /* 200ms na start AE */
    VCMD(0x69, 0x0032, 0x0000);   /* AE target = 50 */
    VCMD(0x6a, 0x0032, 0x0000);   /* AE gain   = 50 */
    VCMD(0x6b, 0x0032, 0x0000);   /* AE exposure = 50 */
    VCMD(0x6e, 0x0003, 0x0000);
    VCMD(0x70, 0x0004, 0x0001);
    VCMD(0x71, 0x0005, 0x0001);
    VCMD(0x6f, 0x0032, 0x0000);
    VCMD(0x73, 0x0002, 0x0000);
    VCMD(0x6d, 0x0046, 0x0001);
#undef VCMD
    uint8_t r1 = 0, r2 = 0;
    ctrl_in(h, 0x01, 0, 0x933f, &r1, 1);
    ctrl_in(h, 0x01, 0, 0x933e, &r2, 1);
    printf("  Sensor reg 0x933F=0x%02x  0x933E=0x%02x\n", r1, r2);
    printf("[SENSOR] OK\n\n");
}

/* ================================================================
 * ZAPIS RAMEK DO BMP  (format YUYV 1280×1024 z bRequest=0x32)
 * ================================================================ */

/* Rozdzielczość z parametrów 0x32: wValue=1280 (szer.), wIndex=(1<<12)|1024 */
#define FRAME_W      1280
#define FRAME_H      1024
#define FRAME_SZ     (FRAME_W * FRAME_H * 2)   /* bajty na ramkę YUYV */
#define MAX_FRAMES   4
#define FRAME_BUF_SZ (FRAME_SZ + 65536)        /* mały margines */

static uint8_t  g_fbuf[FRAME_BUF_SZ];
static int      g_fpos         = 0;
static int      g_frames_saved = 0;

/* ---- zapisz 24-bit BMP top-down z bufora RGB -------------- */
static void write_bmp(const char *path, const uint8_t *rgb,
                      int width, int height)
{
    int stride   = (width * 3 + 3) & ~3;
    uint32_t img = (uint32_t)(stride * height);
    uint32_t fsz = 14 + 40 + img;

    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }

    uint8_t fh[14] = {
        'B','M',
        (uint8_t)fsz,(uint8_t)(fsz>>8),(uint8_t)(fsz>>16),(uint8_t)(fsz>>24),
        0,0, 0,0, 54,0,0,0
    };
    fwrite(fh, 14, 1, f);

    int32_t neg_h = -(int32_t)height;
    uint8_t ih[40];
    memset(ih, 0, 40);
    *(uint32_t*)(ih+ 0) = 40;
    *(int32_t *)(ih+ 4) = (int32_t)width;
    *(int32_t *)(ih+ 8) = neg_h;
    *(uint16_t*)(ih+12) = 1;
    *(uint16_t*)(ih+14) = 24;
    *(uint32_t*)(ih+20) = img;
    fwrite(ih, 40, 1, f);

    uint8_t *row = malloc(stride);
    for (int y = 0; y < height; y++) {
        const uint8_t *src = rgb + y * width * 3;
        for (int x = 0; x < width; x++) {
            row[x*3+0] = src[x*3+2]; /* B */
            row[x*3+1] = src[x*3+1]; /* G */
            row[x*3+2] = src[x*3+0]; /* R */
        }
        memset(row + width*3, 0, stride - width*3);
        fwrite(row, stride, 1, f);
    }
    free(row);
    fclose(f);
}

/* ---- UYVY → RGB ------------------------------------------- */
/* Format AVEO: U₀ Y₀ V₀ Y₁ (nie YUYV!) */
static void yuyv_to_rgb(const uint8_t *uyvy, uint8_t *rgb,
                        int width, int height)
{
#define CLAMP(x) ((x)<0?0:(x)>255?255:(x))
    int n = width * height / 2;
    for (int i = 0; i < n; i++) {
        int U  = uyvy[i*4+0] - 128;
        int Y0 = uyvy[i*4+1];
        int V  = uyvy[i*4+2] - 128;
        int Y1 = uyvy[i*4+3];
        rgb[i*6+0] = CLAMP(Y0 + ((359*V)>>8));
        rgb[i*6+1] = CLAMP(Y0 - ((88*U + 183*V)>>8));
        rgb[i*6+2] = CLAMP(Y0 + ((454*U)>>8));
        rgb[i*6+3] = CLAMP(Y1 + ((359*V)>>8));
        rgb[i*6+4] = CLAMP(Y1 - ((88*U + 183*V)>>8));
        rgb[i*6+5] = CLAMP(Y1 + ((454*U)>>8));
    }
#undef CLAMP
}

/* ---- zapisz kompletną ramkę YUYV jako BMP ----------------- */
static void flush_frame(void)
{
    if (g_fpos < FRAME_SZ) return;  /* jeszcze nie pełna */

    char path[64];
    snprintf(path, sizeof(path), "frame_%03d.bmp", g_frames_saved);

    /* Statystyki Y, U, V */
    long sy=0, su=0, sv=0;
    int ymin=255, ymax=0, umin=255, umax=0;
    int npix = FRAME_W * FRAME_H / 2;
    for (int p = 0; p < npix; p++) {
        int y0=g_fbuf[p*4+0], u=g_fbuf[p*4+1], y1=g_fbuf[p*4+2], v=g_fbuf[p*4+3];
        sy += y0 + y1; su += u; sv += v;
        if (y0 < ymin) ymin=y0; if (y0 > ymax) ymax=y0;
        if (y1 < ymin) ymin=y1; if (y1 > ymax) ymax=y1;
        if (u < umin) umin=u; if (u > umax) umax=u;
    }
    int N = FRAME_W * FRAME_H;
    printf("  [FRAME %d stats] Y: min=%d max=%d avg=%ld  U: avg=%ld [%d-%d]  V: avg=%ld\n",
           g_frames_saved, ymin, ymax, sy/N, su/(N/2), umin, umax, sv/(N/2));

    uint8_t *rgb = malloc(FRAME_W * FRAME_H * 3);
    yuyv_to_rgb(g_fbuf, rgb, FRAME_W, FRAME_H);
    write_bmp(path, rgb, FRAME_W, FRAME_H);
    free(rgb);

    printf("  [FRAME %d] YUYV %dx%d -> %s\n",
           g_frames_saved, FRAME_W, FRAME_H, path);
    g_frames_saved++;

    /* Przesuń nadmiar na początek bufora (dane następnej ramki) */
    int leftover = g_fpos - FRAME_SZ;
    if (leftover > 0)
        memmove(g_fbuf, g_fbuf + FRAME_SZ, leftover);
    g_fpos = leftover;
}

/* ================================================================
 * ISO CALLBACK
 * ================================================================ */
#define ISO_PKTS   16
#define FRAME_BUFS 4

static volatile int g_cb_total   = 0;
static int          g_sync_found = 0;   /* czy znaleziono marker 00 FF FF FF */
#define SYNC_HDR_SZ  10                 /* bajty do pominięcia po markerze */

/* Szuka markera 0x00 0xFF 0xFF 0xFF w buforze; zwraca offset lub -1 */
static int find_sync(const uint8_t *buf, int len)
{
    for (int i = 0; i <= len - 4; i++)
        if (buf[i]==0x00 && buf[i+1]==0xFF && buf[i+2]==0xFF && buf[i+3]==0xFF)
            return i;
    return -1;
}

static void LIBUSB_CALL iso_callback(struct libusb_transfer *t)
{
    if (t->status == LIBUSB_TRANSFER_CANCELLED ||
        t->status == LIBUSB_TRANSFER_NO_DEVICE) {
        g_active--;
        return;
    }
    if (t->status != LIBUSB_TRANSFER_COMPLETED &&
        t->status != LIBUSB_TRANSFER_TIMED_OUT) {
        fprintf(stderr, "ISO status: %d\n", t->status);
        g_active--;
        return;
    }

    g_cb_total++;

    int burst_bytes = 0;
    uint8_t *ptr = t->buffer;

    for (int i = 0; i < t->num_iso_packets; i++) {
        struct libusb_iso_packet_descriptor *d = &t->iso_packet_desc[i];
        int actual = (int)d->actual_length;

        if (d->status == LIBUSB_TRANSFER_COMPLETED && actual > 0) {
            /* Dump pierwszych 64 bajtów pierwszych 8 callbacków */
            if (g_cb_total <= 8 && i == 0) {
                fprintf(stderr, "  [cb#%d pkt0] %d B: ", g_cb_total, actual);
                for (int b = 0; b < actual && b < 64; b++)
                    fprintf(stderr, "%02x ", ptr[b]);
                fprintf(stderr, "\n");
            }

            /* Szukaj markera ramki 00 FF FF FF */
            int sync = find_sync(ptr, actual);
            if (sync >= 0 && !g_sync_found) {
                g_sync_found = 1;
                fprintf(stderr, "  [SYNC] Marker 00FF FFFF w cb#%d pkt%d off=%d\n",
                        g_cb_total, i, sync);
            }

            burst_bytes += actual;

            if (g_frames_saved < MAX_FRAMES) {
                int data_off = 0;
                int data_len = actual;

                if (sync >= 0) {
                    /* dane przed markerem → flush poprzedniej (potencjalnie niekompletnej) ramki */
                    if (sync > 0 && g_fpos + sync < FRAME_BUF_SZ) {
                        memcpy(g_fbuf + g_fpos, ptr, sync);
                        g_fpos += sync;
                    }
                    flush_frame();
                    g_fpos   = 0;   /* zawsze zaczynaj nową ramkę po syncmarkerze */

                    data_off = sync + SYNC_HDR_SZ;
                    data_len = actual - data_off;
                    if (data_len <= 0) { ptr += d->length; continue; }
                }

                if (g_fpos + data_len < FRAME_BUF_SZ) {
                    memcpy(g_fbuf + g_fpos, ptr + data_off, data_len);
                    g_fpos += data_len;
                }
            }
        }
        ptr += d->length;
    }

    if (g_cb_total <= 5 || g_cb_total % 50 == 0)
        fprintf(stderr, "  [cb#%d] burst=%d  fpos=%d  frames=%d\n",
                g_cb_total, burst_bytes, g_fpos, g_frames_saved);

    if (g_frames_saved < MAX_FRAMES)
        flush_frame();

    if (!g_stop) {
        int err = libusb_submit_transfer(t);
        if (err < 0) {
            fprintf(stderr, "  resubmit FAIL: %s\n", libusb_error_name(err));
            g_active--;
        }
    } else {
        g_active--;
    }
}

/* ---- streaming -------------------------------------------- */
static int start_streaming(libusb_device_handle *h, libusb_device *dev)
{
    printf("[STREAM] SET_INTERFACE alt=5 (probe)...\n");
    int alt = 5;
    int r = libusb_set_interface_alt_setting(h, INTERFACE, alt);
    if (r < 0) {
        fprintf(stderr, "alt=5 FAIL: %s, próbuję alt=4...\n", libusb_error_name(r));
        alt = 4;
        r = libusb_set_interface_alt_setting(h, INTERFACE, alt);
        if (r < 0) { fprintf(stderr, "alt=4 też fail: %s\n", libusb_error_name(r)); return -1; }
    }
    printf("  alt=%d aktywny\n", alt);

    printf("[STREAM] Rozdzielczość (0x32)...\n");
    ctrl_out(h, 0x32, 0x0500, 0x1400, NULL, 0);
    usleep(20000);

    printf("[STREAM] START capture (0x22)...\n");
    ctrl_out(h, 0x22, 1, 2, NULL, 0);
    usleep(200000);

    sensor_config(h);

    /* Daj sensorowi czas na inicjalizację po konfiguracji (bez ponownego SET_INTERFACE) */
    printf("[STREAM] Czekam 2s na gotowość sensora...\n");
    usleep(2000000);

    uint16_t max_pkt = 0;
    int ep = find_iso_ep(dev, alt, &max_pkt);
    if (ep < 0) { fprintf(stderr, "Brak ISO endpoint!\n"); return -1; }
    printf("[STREAM] ISO endpoint: 0x%02x  maxPacket=%u\n", ep, max_pkt);

    int pkt_size = max_pkt > 0 ? decode_iso_pkt(max_pkt) : 1024;
    printf("[STREAM] Rozmiar pakietu ISO: %d bajtów\n\n", pkt_size);
    int buf_size = pkt_size * ISO_PKTS;

    struct libusb_transfer *transfers[FRAME_BUFS];
    uint8_t *buffers[FRAME_BUFS];

    g_active = FRAME_BUFS;
    for (int i = 0; i < FRAME_BUFS; i++) {
        buffers[i] = calloc(buf_size, 1);
        transfers[i] = libusb_alloc_transfer(ISO_PKTS);
        libusb_fill_iso_transfer(transfers[i], h, ep,
                                 buffers[i], buf_size, ISO_PKTS,
                                 iso_callback, NULL, 0);
        libusb_set_iso_packet_lengths(transfers[i], pkt_size);
        int err = libusb_submit_transfer(transfers[i]);
        if (err < 0)
            fprintf(stderr, "  submit[%d] FAIL: %s\n", i, libusb_error_name(err));
        else
            printf("  submit[%d] OK\n", i);
    }

    /* Pre-roll: 30s bez zapisu — czekamy na stabilizację AE/AWB */
    printf("[STREAM] Pre-roll 30s (stabilizacja AE/AWB)...\n");
    struct timeval tv = {0, 10000};   /* 10ms polling */
    for (int i = 0; i < 3000; i++)
        libusb_handle_events_timeout(g_ctx, &tv);
    printf("[STREAM] Pre-roll done (cb=%d bytes_acc=%d), reset i zaczynam zapis...\n",
           g_cb_total, g_fpos);
    /* Reset bufora — odrzucamy dane z pre-roll */
    g_fpos         = 0;
    g_frames_saved = 0;
    g_sync_found   = 0;

    printf("[STREAM] Zapis %d ramek...\n", MAX_FRAMES);
    for (int i = 0; i < 600 && g_frames_saved < MAX_FRAMES; i++)
        libusb_handle_events_timeout(g_ctx, &tv);

    /* Zapisz ewentualną kompletną ramkę która czeka w buforze */
    if (g_frames_saved < MAX_FRAMES)
        flush_frame();

    printf("\n[STREAM] Zapisano %d ramek  (callbacków: %d)\n",
           g_frames_saved, g_cb_total);

    g_stop = 1;
    for (int i = 0; i < FRAME_BUFS; i++)
        libusb_cancel_transfer(transfers[i]);

    struct timeval tv2 = {0, 10000};
    while (g_active > 0)
        libusb_handle_events_timeout(g_ctx, &tv2);

    for (int i = 0; i < FRAME_BUFS; i++) {
        libusb_free_transfer(transfers[i]);
        free(buffers[i]);
    }
    return 0;
}

/* Sprawdza czy firmware już załadowany (bez resetu) */
static int fw_already_loaded(libusb_context *ctx)
{
    libusb_device_handle *h = libusb_open_device_with_vid_pid(ctx, VID, PID);
    if (!h) return 0;
    if (libusb_kernel_driver_active(h, INTERFACE) == 1)
        libusb_detach_kernel_driver(h, INTERFACE);
    int ok = 0;
    if (libusb_set_configuration(h, 1) == 0 &&
        libusb_claim_interface(h, INTERFACE) == 0) {
        uint8_t buf[16] = {0};
        ctrl_in(h, 0x08, 0, 0, buf, 12);
        ok = (strncmp((char *)buf, "Ver R003.001", 12) == 0);
        libusb_release_interface(h, INTERFACE);
    }
    libusb_close(h);
    return ok;
}

/* ---- main ------------------------------------------------- */
int main(void)
{
    libusb_context *ctx = NULL;
    libusb_device_handle *handle = NULL;
    libusb_device *dev = NULL;
    int ret = 0;

    if (libusb_init(&ctx) < 0) return 1;
    g_ctx = ctx;
    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);

    if (fw_already_loaded(ctx)) {
        printf("[INIT] Firmware już załadowany — pomijam reset i upload.\n");
    } else {
        handle = libusb_open_device_with_vid_pid(ctx, VID, PID);
        if (!handle) {
            fprintf(stderr, "Urządzenie %04x:%04x nie znalezione\n", VID, PID);
            ret = 1; goto cleanup;
        }
        if (libusb_kernel_driver_active(handle, INTERFACE) == 1)
            libusb_detach_kernel_driver(handle, INTERFACE);
        printf("[INIT] USB reset...\n");
        libusb_reset_device(handle);
        libusb_close(handle);
        handle = NULL;
        usleep(3000000);
    }

    handle = libusb_open_device_with_vid_pid(ctx, VID, PID);
    if (!handle) {
        fprintf(stderr, "Re-open FAIL\n");
        ret = 1; goto cleanup;
    }
    dev = libusb_get_device(handle);
    list_endpoints(dev);

    if (libusb_kernel_driver_active(handle, INTERFACE) == 1)
        libusb_detach_kernel_driver(handle, INTERFACE);
    libusb_set_configuration(handle, 1);
    if (libusb_claim_interface(handle, INTERFACE) < 0) {
        fprintf(stderr, "claim_interface FAIL\n"); ret = 1; goto cleanup;
    }

    if (configure_stream(handle) < 0)     { ret = 1; goto cleanup; }

    /* Upload firmware tylko jeśli nie był załadowany */
    {
        uint8_t vbuf[16] = {0};
        ctrl_in(handle, 0x08, 0, 0, vbuf, 12);
        if (strncmp((char *)vbuf, "Ver R003.001", 12) != 0) {
            printf("[INIT] Ładowanie firmware...\n");
            if (upload_firmware(handle) < 0) { ret = 1; goto cleanup; }
            if (configure_stream(handle) < 0) { ret = 1; goto cleanup; }
        }
    }

    if (start_streaming(handle, dev) < 0) { ret = 1; goto cleanup; }

cleanup:
    if (handle) {
        libusb_release_interface(handle, INTERFACE);
        libusb_attach_kernel_driver(handle, INTERFACE);
        libusb_close(handle);
    }
    libusb_exit(ctx);
    return ret;
}
