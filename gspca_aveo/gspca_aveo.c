// SPDX-License-Identifier: GPL-2.0
#define MODULE_NAME "gspca_aveo"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>

#include "gspca.h"

/* =========================
   PARAMETRY
   ========================= */
#define FRAME_W   1280
#define FRAME_H   1024
#define FRAME_SZ  (FRAME_W * FRAME_H * 2)

/* =========================
   Firmware
   ========================= */
#include "aveo_firmware.h"

/* =========================
   Struktura prywatna
   ========================= */
struct sd {
    struct gspca_dev gspca_dev;

    u8 *framebuf;
    int fpos;
    atomic_t stopping;
};

/* =========================
   sensor init
   ========================= */

static int aveo_ctrl_out(struct gspca_dev *gspca_dev,
                        u8 req, u16 val, u16 idx)
{
    int ret;

    ret = usb_control_msg(gspca_dev->dev,
            usb_sndctrlpipe(gspca_dev->dev, 0),
            req,
            USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
            val, idx,
            NULL, 0,
            1000);

    if (ret < 0)
        pr_err("aveo: ctrl_out 0x%02x failed (%d)\n", req, ret);

    msleep(200);
    return ret;
}


static int aveo_sensor_init(struct gspca_dev *gspca_dev)
{
    int ret;

#define VCMD(r,v,i) \
    do { \
        ret = aveo_ctrl_out(gspca_dev, r, v, i); \
        if (ret < 0) return ret; \
    } while (0)

    pr_info("aveo: sensor init\n");

    VCMD(0x5c, 0x0000, 0x0000);
    VCMD(0x5d, 0x0000, 0x0000);
    VCMD(0x5e, 0x0000, 0x0000);
    VCMD(0x72, 0x0000, 0x0001);
    VCMD(0x90, 0x0000, 0x0000);
    VCMD(0x8f, 0x0000, 0x0000);
    VCMD(0x5e, 0x0000, 0x0000);

    VCMD(0x6c, 0x0032, 0x0002);

    VCMD(0x5f, 0x0001, 0x0000);   /* AE ON */
    msleep(200);

    VCMD(0x69, 0x0032, 0x0000);
    VCMD(0x6a, 0x0032, 0x0000);
    VCMD(0x6b, 0x0032, 0x0000);

    VCMD(0x6e, 0x0003, 0x0000);
    VCMD(0x70, 0x0004, 0x0001);
    VCMD(0x71, 0x0005, 0x0001);
    VCMD(0x6f, 0x0032, 0x0000);
    VCMD(0x73, 0x0002, 0x0000);
    VCMD(0x6d, 0x0046, 0x0001);

#undef VCMD

    pr_info("aveo: sensor ready\n");
    return 0;
}










/* =========================
   check firmware
   ========================= */
static int fw_already_loaded(struct gspca_dev *gspca_dev)
{    
     int ret;
     int ok = 0;
     unsigned int pipe = 0;
     gspca_dev->usb_buf[0]='\0';
     pipe = usb_rcvctrlpipe(gspca_dev->dev, 0);
     //(h, 0x08, 0, 0, buf, 12);
     //   gspca_dev->dev
     // mutex_lock(&gspca_dev->usb_lock);
     ret = usb_control_msg(gspca_dev->dev, pipe,
			      0x08,
                              USB_TYPE_VENDOR |  USB_DIR_IN |USB_RECIP_DEVICE,
			      0, 0, gspca_dev->usb_buf, 12, 500); 
  ok = (strncmp((char *)gspca_dev->usb_buf, "Ver R003.001", 12) == 0);

  
  return ok;



}
/* =========================
   Start MCU i Sensor
   ========================= */

static int aveo_start_mcu(struct gspca_dev *gspca_dev)
{
    int ret;

    ret = usb_control_msg(gspca_dev->dev,
            usb_sndctrlpipe(gspca_dev->dev, 0),
            5,                      /* bRequest */
            USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
            0,                      /* wValue */
            1,                      /* wIndex */
            NULL,
            0,
            1000);

    if (ret < 0) {
        pr_err("aveo: MCU start failed\n");
        return ret;
    }

    msleep(500);   /* VERY IMPORTANT */

    pr_info("aveo: MCU started\n");
    return 0;
}









/* =========================
   Tryb video
   ========================= */
static const struct v4l2_pix_format  aveo_mode[] = {
    {
        .width        = FRAME_W,
        .height       = FRAME_H,
        .pixelformat  = V4L2_PIX_FMT_UYVY,
        //.pixelformat  =V4L2_PIX_FMT_YUYV,
        .field        = V4L2_FIELD_NONE,
        .bytesperline = FRAME_W * 2,
        .sizeimage    = FRAME_SZ,
        .colorspace   = V4L2_COLORSPACE_SRGB,
        .priv         = 0,
    }
};

/* =========================
   SYNC
   ========================= */
static int find_sync(const u8 *buf, int len)
{
    int i;
    for (i = 0; i <= len - 4; i++) {
        if (buf[i] == 0x00 &&
            buf[i+1] == 0xFF &&
            buf[i+2] == 0xFF &&
            buf[i+3] == 0xFF)
            return i;
    }
    return -1;
}

/* =========================
   Firmware upload
   ========================= */
static int aveo_upload_firmware(struct gspca_dev *gspca_dev)
{
    struct usb_device *dev = gspca_dev->dev;
    int i, ret;

    for (i = 0; i < 154; i++) {
        ret = usb_control_msg(dev,
            usb_sndctrlpipe(dev, 0),
            4,
            USB_TYPE_VENDOR | USB_DIR_OUT,
            0,
            i,
            (u8 *)&aveo_firmware[i * 32],
            32,
            500);

        if (ret < 0) {
            pr_err("aveo: firmware chunk %d failed\n", i);
            return ret;
        }
    }

    pr_info("aveo: firmware uploaded\n");
    return 0;
}

/* =========================
   CONFIG
   ========================= */
static int sd_config(struct gspca_dev *gspca_dev,
                     const struct usb_device_id *id)
{
    int ret;
    struct cam *cam = &gspca_dev->cam;
    
    pr_info("aveo: config\n");

    if (!fw_already_loaded(gspca_dev))
    {
        ret = aveo_upload_firmware(gspca_dev);
        if (ret < 0)
        return ret;
    }
    ret = aveo_start_mcu(gspca_dev);
    if (ret < 0)
    return ret;
     
    ret = aveo_sensor_init(gspca_dev);
    if (ret < 0)
    return ret;

  
    cam->cam_mode = aveo_mode;
    cam->nmodes   = ARRAY_SIZE(aveo_mode);
    cam->npkt     = 32;
    

    return 0;
}

/* =========================
   START / STOP
   ========================= */
static int sd_start(struct gspca_dev *gspca_dev)
{   
      int ret;
       struct sd *sd = (struct sd *)gspca_dev;

       atomic_set(&sd->stopping, 0);
     ret = usb_set_interface(gspca_dev->dev, 0, 5);
      if (ret < 0) {
        pr_err("aveo: set_interface failed %d\n", ret);
        return ret;
    }  
    
    /* 2. ustaw rozdzielczość */
    ret = aveo_ctrl_out(gspca_dev, 0x32, 0x0500, 0x1400);
    if (ret < 0)
        return ret;

    msleep(20);
    
    /* 3. start stream */
    ret = aveo_ctrl_out(gspca_dev, 0x22, 1, 2);
    if (ret < 0)
        return ret;
    msleep(200);

    //BUG_ON(!gspca_dev);
   // struct sd *sd = (struct sd *)gspca_dev;

    pr_info("aveo: start\n");
    
    sd->framebuf = kmalloc(FRAME_SZ, GFP_KERNEL);
    if (!sd->framebuf)
        return -ENOMEM;

    sd->fpos = 0;
    return 0;
}

static void sd_stop(struct gspca_dev *gspca_dev)
{
    struct sd *sd = (struct sd *)gspca_dev;

    pr_info("aveo: stop\n");
    atomic_set(&sd->stopping, 1);

    //kfree(sd->framebuf);
    //sd->framebuf = NULL;

   
    aveo_ctrl_out(gspca_dev, 0x22, 0, 0);  // stop capture
    msleep(50);

    //usb_set_interface(gspca_dev->dev, 0, 0); // wyłącz streaming

    msleep(100);


}


static int sd_init(struct gspca_dev *gspca_dev)
{
    pr_info("aveo: init\n");
    return 0;
}

//static void sd_stopN(struct gspca_dev *gspca_dev)
//{
//  

//    struct sd *sd = (struct sd *)gspca_dev;

//    pr_info("aveo: stop\n");

//    kfree(sd->framebuf);
//    sd->framebuf = NULL;



//}



/* =========================
   PARSER
   ========================= */






static void sd_pkt_scan(struct gspca_dev *gspca_dev,
                        u8 *data, int len)
{
    struct sd *sd = (struct sd *)gspca_dev;

    if (!gspca_dev)
    return;

    if (atomic_read(&sd->stopping))
    return;

    while (len > 0) {

        /* jeśli nie jesteśmy zsynchronizowani */
        if (sd->fpos == 0) {
            int pos = find_sync(data, len);
            if (pos < 0)
                return;

            /* zaczynamy nową ramkę */
            data += pos + 4;
            len  -= pos + 4;

            gspca_frame_add(gspca_dev, FIRST_PACKET, NULL, 0);
        }

        int chunk = min(len, FRAME_SZ - sd->fpos);

        gspca_frame_add(gspca_dev, INTER_PACKET, data, chunk);

        sd->fpos += chunk;
        data += chunk;
        len  -= chunk;

        if (sd->fpos >= FRAME_SZ) {
            gspca_frame_add(gspca_dev, LAST_PACKET, NULL, 0);
            sd->fpos = 0;
        }
    }
}



 /*=========================
   DESC
   ========================= */
static const struct sd_desc sd_desc = {
    .name     = MODULE_NAME,
    .config   = sd_config,
    .init     = sd_init,
    .start    = sd_start,
    .stopN    = sd_stop,
    .pkt_scan = sd_pkt_scan,
};

/* =========================
   USB ID
   ========================= */
static const struct usb_device_id device_table[] = {
    { USB_DEVICE(0x1871, 0x01b0) },
    {}
};
MODULE_DEVICE_TABLE(usb, device_table);

/* =========================
   PROBE
   ========================= */
static int sd_probe(struct usb_interface *intf,
                    const struct usb_device_id *id)
{
    return gspca_dev_probe(intf, id, &sd_desc,
                           sizeof(struct sd), THIS_MODULE);
}

static void sd_disconnect(struct usb_interface *intf)
{
    gspca_disconnect(intf);
}

/* =========================
   DRIVER
   ========================= */
static struct usb_driver sd_driver = {
    .name       = MODULE_NAME,
    .id_table   = device_table,
    .probe      = sd_probe,
    .disconnect = sd_disconnect,
};

module_usb_driver(sd_driver);

MODULE_AUTHOR("aveo reverse");
MODULE_DESCRIPTION("Aveo USB microscope driver");
MODULE_LICENSE("GPL");
