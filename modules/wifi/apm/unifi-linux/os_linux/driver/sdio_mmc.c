/*
 * ---------------------------------------------------------------------------
 *
 * FILE: sdio_mmc.c
 *
 * PURPOSE: SDIO driver interface for generic MMC stack.
 *
 * Copyright (C) 2008-2009 by Cambridge Silicon Radio Ltd.
 *
 * ---------------------------------------------------------------------------
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/gfp.h>

#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>

#include "unifi_priv.h"


static CsrSdioFunctionDriver *sdio_func_drv;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
/*
 * We need to keep track of the power on/off because we can not call
 * mmc_power_restore_host() when the card is already powered.
 * Even then, we need to patch the MMC driver to add a power_restore handler
 * in the mmc_sdio_ops structure. If the MMC driver is not patched,
 * mmc_power_save_host() and mmc_power_restore_host() are no-ops.
 */
static int card_is_powered = 1;
#endif /* 2.6.32 */

#ifdef UNIFI_DEBUG
#define _sdio_claim_host(_func)                                         \
    do {                                                                \
        if (func->card->host->claimed) {                                \
            printk("%s: host already claimed, will wait\n", __FUNCTION__); \
        }                                                               \
        sdio_claim_host(_func);                                         \
    } while (0)

#define _sdio_release_host(_func)                               \
    do {                                                        \
        if (!func->card->host->claimed) {                       \
            printk("%s: host not claimed\n", __FUNCTION__);     \
        }                                                       \
        sdio_release_host(_func);                               \
    } while (0)
#else
#define _sdio_claim_host(_func)     sdio_claim_host(_func)
#define _sdio_release_host(_func)   sdio_release_host(_func)
#endif /* UNIFI_DEBUG */

/* MMC uses ENOMEDIUM to indicate card gone away */
static inline CsrInt32 convert_sdio_error(int r)
{
    switch (r) {
        case 0:
            return 0;
        case 1: /* For power on */
            return CSR_SDIO_RESULT_DEVICE_NOT_RESET;
        case -ENOMEDIUM:
            return -CSR_ENODEV;
        case -EIO:
            return -CSR_EIO;
        case -ENODEV:
            return -CSR_ENODEV;
        case -ENOMEM:
            return -CSR_ENOMEM;
        case -EINVAL:
            return -CSR_EINVAL;
        case -ETIMEDOUT:
            return -CSR_ETIMEDOUT;
        default:
            return -CSR_EIO;
    }
}


static int
csr_io_rw_direct(struct mmc_card *card, int write, uint8_t fn,
                 uint32_t addr, uint8_t in, uint8_t* out)
{
    struct mmc_command cmd;
    int err;

    BUG_ON(!card);
    BUG_ON(fn > 7);

    memset(&cmd, 0, sizeof(struct mmc_command));

    cmd.opcode = SD_IO_RW_DIRECT;
    cmd.arg = write ? 0x80000000 : 0x00000000;
    cmd.arg |= fn << 28;
    cmd.arg |= (write && out) ? 0x08000000 : 0x00000000;
    cmd.arg |= addr << 9;
    cmd.arg |= in;
    cmd.flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_AC;

    err = mmc_wait_for_cmd(card->host, &cmd, 0);
    if (err)
        return err;

    /* this function is not exported, so we will need to sort it out here
     * for now, lets hard code it to sdio */
    if (0) {
        /* old arg (mmc_host_is_spi(card->host)) { */
        /* host driver already reported errors */
    } else {
        if (cmd.resp[0] & R5_ERROR) {
            printk(KERN_ERR "%s: r5 error 0x%02x\n",
                   __FUNCTION__, cmd.resp[0]);
            return -EIO;
        }
        if (cmd.resp[0] & R5_FUNCTION_NUMBER)
            return -EINVAL;
        if (cmd.resp[0] & R5_OUT_OF_RANGE)
            return -ERANGE;
    }

    if (out) {
        if (0) {    /* old argument (mmc_host_is_spi(card->host)) */
            *out = (cmd.resp[0] >> 8) & 0xFF;
        }
        else {
            *out = cmd.resp[0] & 0xFF;
        }
    }

    return 0;
}


CsrInt32
CsrSdioRead8(CsrSdioFunction *function, CsrUint32 address, CsrUint8 *data)
{
    struct sdio_func *func = (struct sdio_func *)function->priv;
    int err = 0;

    _sdio_claim_host(func);
    *data = sdio_readb(func, address, &err);
    _sdio_release_host(func);

    if (err) {
        func_exit_r(err);
        return convert_sdio_error(err);
    }

    return 0;
} /* CsrSdioRead8() */

CsrInt32
CsrSdioWrite8(CsrSdioFunction *function, CsrUint32 address, CsrUint8 data)
{
    struct sdio_func *func = (struct sdio_func *)function->priv;
    int err = 0;

    _sdio_claim_host(func);
    sdio_writeb(func, data, address, &err);
    _sdio_release_host(func);

    if (err) {
        func_exit_r(err);
        return convert_sdio_error(err);
    }

    return 0;
} /* CsrSdioWrite8() */

CsrInt32
CsrSdioRead16(CsrSdioFunction *function, CsrUint32 address, CsrUint16 *data)
{
    struct sdio_func *func = (struct sdio_func *)function->priv;
    int err;
    uint8_t b0, b1;

    _sdio_claim_host(func);
    b0 = sdio_readb(func, address, &err);
    if (err) {
        _sdio_release_host(func);
        return convert_sdio_error(err);
    }

    b1 = sdio_readb(func, address+1, &err);
    if (err) {
        _sdio_release_host(func);
        return convert_sdio_error(err);
    }
    _sdio_release_host(func);

    *data = ((uint16_t)b1 << 8) | b0;

    return 0;
} /* CsrSdioRead16() */


CsrInt32
CsrSdioWrite16(CsrSdioFunction *function, CsrUint32 address, CsrUint16 data)
{
    struct sdio_func *func = (struct sdio_func *)function->priv;
    int err;
    uint8_t b0, b1;

    _sdio_claim_host(func);
    b1 = (data >> 8) & 0xFF;
    sdio_writeb(func, b1, address+1, &err);
    if (err) {
        _sdio_release_host(func);
        return convert_sdio_error(err);
    }

    b0 = data & 0xFF;
    sdio_writeb(func, b0, address, &err);
    if (err) {
        _sdio_release_host(func);
        return convert_sdio_error(err);
    }

    _sdio_release_host(func);
    return 0;
} /* CsrSdioWrite16() */


CsrInt32
CsrSdioF0Read8(CsrSdioFunction *function, CsrUint32 address, CsrUint8 *data)
{
    struct sdio_func *func = (struct sdio_func *)function->priv;
    int err = 0;

    _sdio_claim_host(func);
#ifdef MMC_QUIRK_LENIENT_FN0
    *data = sdio_f0_readb(func, address, &err);
#else
    err = csr_io_rw_direct(func->card, 0, 0, address, 0, data);
#endif
    _sdio_release_host(func);

    if (err) {
        func_exit_r(err);
        return convert_sdio_error(err);
    }

    return 0;
} /* CsrSdioF0Read8() */

CsrInt32
CsrSdioF0Write8(CsrSdioFunction *function, CsrUint32 address, CsrUint8 data)
{
    struct sdio_func *func = (struct sdio_func *)function->priv;
    int err = 0;

    _sdio_claim_host(func);
#ifdef MMC_QUIRK_LENIENT_FN0
    sdio_f0_writeb(func, data, address, &err);
#else
    err = csr_io_rw_direct(func->card, 1, 0, address, data, NULL);
#endif
    _sdio_release_host(func);

    if (err) {
        func_exit_r(err);
        return convert_sdio_error(err);
    }

    return 0;
} /* CsrSdioF0Write8() */


CsrInt32
CsrSdioRead(CsrSdioFunction *function, CsrUint32 address, void *data, CsrUint32 length)
{
    struct sdio_func *func = (struct sdio_func *)function->priv;
    int err;

    _sdio_claim_host(func);
    err = sdio_readsb(func, data, address, length);
    _sdio_release_host(func);

    if (err) {
        func_exit_r(err);
        return convert_sdio_error(err);
    }

    return 0;
} /* CsrSdioRead() */

CsrInt32
CsrSdioWrite(CsrSdioFunction *function, CsrUint32 address, const void *data, CsrUint32 length)
{
    struct sdio_func *func = (struct sdio_func *)function->priv;
    int err;

    _sdio_claim_host(func);
    err = sdio_writesb(func, address, (void*)data, length);
    _sdio_release_host(func);

    if (err) {
        func_exit_r(err);
        return convert_sdio_error(err);
    }

    return 0;
} /* CsrSdioWrite() */


static int
csr_sdio_enable_hs(struct mmc_card *card)
{
    int ret;
    u8 speed;

    if (!(card->host->caps & MMC_CAP_SD_HIGHSPEED))
        return 0;

    if (!card->cccr.high_speed)
        return 0;

    ret = csr_io_rw_direct(card, 0, 0, SDIO_CCCR_SPEED, 0, &speed);
    if (ret)
        return ret;

    speed |= SDIO_SPEED_EHS;

    ret = csr_io_rw_direct(card, 1, 0, SDIO_CCCR_SPEED, speed, NULL);
    if (ret)
        return ret;

    mmc_card_set_highspeed(card);
    card->host->ios.timing = MMC_TIMING_SD_HS;
    card->host->ops->set_ios(card->host, &card->host->ios);

    return 0;
}

static int
csr_sdio_disable_hs(struct mmc_card *card)
{
    int ret;
    u8 speed;

    if (!(card->host->caps & MMC_CAP_SD_HIGHSPEED))
        return 0;

    if (!card->cccr.high_speed)
        return 0;

    ret = csr_io_rw_direct(card, 0, 0, SDIO_CCCR_SPEED, 0, &speed);
    if (ret)
        return ret;

    speed &= ~SDIO_SPEED_EHS;

    ret = csr_io_rw_direct(card, 1, 0, SDIO_CCCR_SPEED, speed, NULL);
    if (ret)
        return ret;

    card->state &= ~MMC_STATE_HIGHSPEED;
    card->host->ios.timing = MMC_TIMING_LEGACY;
    card->host->ops->set_ios(card->host, &card->host->ios);

    return 0;
}


/*
 * ---------------------------------------------------------------------------
 *  csr_sdio_set_max_clock_speed
 *
 *      Set the maximum SDIO bus clock speed to use.
 *
 *  Arguments:
 *      sdio            SDIO context pointer
 *      max_khz         maximum clock speed in kHz
 *
 *  Returns:
 *      Set clock speed in kHz; or a UniFi driver error code.
 * ---------------------------------------------------------------------------
 */
CsrInt32
csr_sdio_set_max_clock_speed(CsrSdioFunction *function, CsrUint32 max_khz)
{
    struct sdio_func *func = (struct sdio_func *)function->priv;
    struct mmc_host *host = func->card->host;
    struct mmc_ios *ios = &host->ios;
    unsigned int max_hz;
    int err;

    if (!max_khz || max_khz > sdio_clock) {
        max_khz = sdio_clock;
    }

    _sdio_claim_host(func);
    max_hz = 1000 * max_khz;
    if (max_hz > host->f_max) {
        max_hz = host->f_max;
    }

    if (max_hz > 25000000) {
        err = csr_sdio_enable_hs(func->card);
    } else {
        err = csr_sdio_disable_hs(func->card);
    }
    if (err) {
        printk(KERN_ERR "SDIO warning: Failed to configure SDIO clock mode\n");
        _sdio_release_host(func);
        return 0;
    }

    ios->clock = max_hz;
    host->ops->set_ios(host, ios);

    _sdio_release_host(func);

    return 0;
} /* csr_sdio_set_max_clock_speed() */


/*
 * ---------------------------------------------------------------------------
 *  CsrSdioInterruptEnable
 *  CsrSdioInterruptDisable
 *
 *      Enable or disable the SDIO interrupt.
 *      The driver disables the SDIO interrupt until the i/o thread can
 *      process it.
 *      The SDIO interrupt can be disabled by modifying the SDIO_INT_ENABLE
 *      register in the Card Common Control Register block, but this requires
 *      two CMD52 operations. A better solution is to mask the interrupt at
 *      the host controller.
 *
 *  Arguments:
 *      sdio            SDIO context pointer
 *
 *  Returns:
 *      Zero on success or a UniFi driver error code.
 *
 * ---------------------------------------------------------------------------
 */
CsrInt32
CsrSdioInterruptEnable(CsrSdioFunction *function)
{
    struct sdio_func *func = (struct sdio_func *)function->priv;
    int err = 0;

    _sdio_claim_host(func);
    /* Write the Int Enable in CCCR block */
#ifdef MMC_QUIRK_LENIENT_FN0
    sdio_f0_writeb(func, 0x3, SDIO_CCCR_IENx, &err);
#else
    err = csr_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IENx, 0x03, NULL);
#endif
    _sdio_release_host(func);

    func_exit();
    return convert_sdio_error(err);
} /* CsrSdioInterruptEnable() */

CsrInt32
CsrSdioInterruptDisable(CsrSdioFunction *function)
{
    struct sdio_func *func = (struct sdio_func *)function->priv;
    int err = 0;

    _sdio_claim_host(func);
    /* Write the Int Enable in CCCR block */
#ifdef MMC_QUIRK_LENIENT_FN0
    sdio_f0_writeb(func, 0, SDIO_CCCR_IENx, &err);
#else
    err = csr_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IENx, 0x00, NULL);
#endif
    _sdio_release_host(func);

    func_exit();
    return convert_sdio_error(err);
} /* CsrSdioInterruptDisable() */


void CsrSdioInterruptAcknowledge(CsrSdioFunction *function)
{
}


/*
 * ---------------------------------------------------------------------------
 *  CsrSdioFunctionEnable
 *
 *      Enable i/o on function 1.
 *
 *  Arguments:
 *      sdio            SDIO context pointer
 *
 * Returns:
 *      UniFi driver error code.
 * ---------------------------------------------------------------------------
 */
CsrInt32
CsrSdioFunctionEnable(CsrSdioFunction *function)
{
    struct sdio_func *func = (struct sdio_func *)function->priv;
    int err;

    func_enter();

    /* Enable UniFi function 1 (the 802.11 part). */
    _sdio_claim_host(func);
    err = sdio_enable_func(func);
    _sdio_release_host(func);
    if (err) {
        unifi_error(NULL, "Failed to enable SDIO function %d\n", func->num);
    }

    func_exit();
    return convert_sdio_error(err);
} /* CsrSdioFunctionEnable() */


/*
 * ---------------------------------------------------------------------------
 *  CsrSdioFunctionDisable
 *
 *      Enable i/o on function 1.
 *
 *  Arguments:
 *      sdio            SDIO context pointer
 *
 * Returns:
 *      UniFi driver error code.
 * ---------------------------------------------------------------------------
 */
CsrInt32
CsrSdioFunctionDisable(CsrSdioFunction *function)
{
    struct sdio_func *func = (struct sdio_func *)function->priv;
    int err;

    func_enter();

    /* Disable UniFi function 1 (the 802.11 part). */
    _sdio_claim_host(func);
    err = sdio_disable_func(func);
    _sdio_release_host(func);
    if (err) {
        unifi_error(NULL, "Failed to disable SDIO function %d\n", func->num);
    }

    func_exit();
    return convert_sdio_error(err);
} /* CsrSdioFunctionDisable() */


/*
 * ---------------------------------------------------------------------------
 *  CsrSdioFunctionActive
 *
 *      No-op as the bus goes to an active state at the start of every
 *      command.
 *
 *  Arguments:
 *      sdio            SDIO context pointer
 * ---------------------------------------------------------------------------
 */
void
CsrSdioFunctionActive(CsrSdioFunction *function)
{
} /* CsrSdioFunctionActive() */

/*
 * ---------------------------------------------------------------------------
 *  CsrSdioFunctionIdle
 *
 *      Set the function as idle.
 *
 *  Arguments:
 *      sdio            SDIO context pointer
 * ---------------------------------------------------------------------------
 */
void
CsrSdioFunctionIdle(CsrSdioFunction *function)
{
} /* CsrSdioFunctionIdle() */


/*
 * ---------------------------------------------------------------------------
 *  CsrSdioPowerOn
 *
 *      Power on UniFi.
 *
 *  Arguments:
 *      sdio            SDIO context pointer
 * ---------------------------------------------------------------------------
 */
CsrInt32
CsrSdioPowerOn(CsrSdioFunction *function)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
    struct sdio_func *func = (struct sdio_func *)function->priv;
    struct mmc_host *host = func->card->host;

    _sdio_claim_host(func);
    if (!card_is_powered) {
        mmc_power_restore_host(host);
        card_is_powered = 1;
    } else {
        printk(KERN_INFO "SDIO: Skip power on; card is already powered.\n");
    }
    _sdio_release_host(func);
#endif /* 2.6.32 */

    return 0;
} /* CsrSdioPowerOn() */

/*
 * ---------------------------------------------------------------------------
 *  CsrSdioPowerOff
 *
 *      Power off UniFi.
 *
 *  Arguments:
 *      sdio            SDIO context pointer
 * ---------------------------------------------------------------------------
 */
void
CsrSdioPowerOff(CsrSdioFunction *function)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
    struct sdio_func *func = (struct sdio_func *)function->priv;
    struct mmc_host *host = func->card->host;

    _sdio_claim_host(func);
    if (card_is_powered) {
        mmc_power_save_host(host);
        card_is_powered = 0;
    } else {
        printk(KERN_INFO "SDIO: Skip power off; card is already powered off.\n");
    }
    _sdio_release_host(func);
#endif /* 2.6.32 */
} /* CsrSdioPowerOff() */


static int
sdio_set_block_size_ignore_first_error(struct sdio_func *func, unsigned blksz)
{
    int ret;

    if (blksz > func->card->host->max_blk_size)
        return -EINVAL;

    if (blksz == 0) {
        blksz = min(func->max_blksize, func->card->host->max_blk_size);
        blksz = min(blksz, 512u);
    }

    /*
     * Ignore -ERANGE (OUT_OF_RANGE in R5) on the first byte as
     * the block size may be invalid until both bytes are written.
     */
    ret = csr_io_rw_direct(func->card, 1, 0,
                           SDIO_FBR_BASE(func->num) + SDIO_FBR_BLKSIZE,
                           blksz & 0xff, NULL);
    if (ret && ret != -ERANGE)
        return ret;
    ret = csr_io_rw_direct(func->card, 1, 0,
                           SDIO_FBR_BASE(func->num) + SDIO_FBR_BLKSIZE + 1,
                           (blksz >> 8) & 0xff, NULL);
    if (ret)
        return ret;
    func->cur_blksize = blksz;

    return 0;
}

CsrInt32
CsrSdioBlockSizeSet(CsrSdioFunction *function, CsrUint16 blockSize)
{
    struct sdio_func *func = (struct sdio_func *)function->priv;
    int r = 0;

    /* Module parameter overrides */
    if (sdio_block_size > -1) {
        blockSize = sdio_block_size;
    }

    unifi_trace(NULL, UDBG1, "Set SDIO function block size to %d\n",
                blockSize);

    _sdio_claim_host(func);
    r = sdio_set_block_size(func, blockSize);
    _sdio_release_host(func);

    /*
     * The MMC driver for kernels prior to 2.6.32 may fail this request
     * with -ERANGE. In this case use our workaround.
     */
    if (r == -ERANGE) {
        _sdio_claim_host(func);
        r = sdio_set_block_size_ignore_first_error(func, blockSize);
        _sdio_release_host(func);
    }
    if (r) {
        unifi_error(NULL, "Error %d setting block size\n", r);
    }

    /* Determine the achieved block size to pass to the core */
    function->blockSize = func->cur_blksize;

    return convert_sdio_error(r);
} /* CsrSdioBlockSizeSet() */


/*
 * ---------------------------------------------------------------------------
 *  CsrSdioHardReset
 *
 *      Hard Resets UniFi is possible.
 *
 *  Arguments:
 *      sdio            SDIO context pointer
 * ---------------------------------------------------------------------------
 */
CsrInt32
CsrSdioHardReset(CsrSdioFunction *function)
{
    return -CSR_ENOTSUP;
} /* CsrSdioHardReset() */



/*
 * ---------------------------------------------------------------------------
 *  uf_glue_sdio_int_handler
 *
 *      Interrupt callback function for SDIO interrupts.
 *      This is called in kernel context (i.e. not interrupt context).
 *
 *  Arguments:
 *      func      SDIO context pointer
 *
 *  Returns:
 *      None.
 *
 *  Note: Called with host already claimed.
 * ---------------------------------------------------------------------------
 */
static void
uf_glue_sdio_int_handler(struct sdio_func *func)
{
    CsrSdioFunction *sdio_ctx;
    CsrSdioDsrCallback func_dsr_callback;
    int r;

    sdio_ctx = sdio_get_drvdata(func);
    if (!sdio_ctx) {
        return;
    }

    /*
     * Normally, we are not allowed to do any SDIO commands here.
     * However, this is called in a thread context and with the SDIO lock
     * so we disable the interrupts here instead of trying to do complicated
     * things with the SDIO lock.
     */
#ifdef MMC_QUIRK_LENIENT_FN0
    sdio_f0_writeb(func, 0, SDIO_CCCR_IENx, &r);
#else
    r = csr_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IENx, 0x00, NULL);
#endif
    if (r) {
        printk(KERN_ERR "UniFi MMC Int handler: Failed to disable interrupts\n");
    }

    /* If the function driver has registered a handler, call it */
    if (sdio_func_drv && sdio_func_drv->interrupt) {

        func_dsr_callback = sdio_func_drv->interrupt(sdio_ctx);

        /* If interrupt handle returns a DSR handle, call it */
        if (func_dsr_callback) {
            func_dsr_callback(sdio_ctx, 0);
        }
    }

} /* uf_glue_sdio_int_handler() */



/*
 * ---------------------------------------------------------------------------
 *  csr_sdio_linux_remove_irq
 *
 *      Unregister the interrupt handler.
 *      This means that the linux layer can not process interrupts any more.
 *
 *  Arguments:
 *      sdio      SDIO context pointer
 *
 *  Returns:
 *      Status of the removal.
 * ---------------------------------------------------------------------------
 */
int
csr_sdio_linux_remove_irq(CsrSdioFunction *function)
{
    struct sdio_func *func = (struct sdio_func *)function->priv;
    int r;

    _sdio_claim_host(func);
    r = sdio_release_irq(func);
    _sdio_release_host(func);

    return r;

} /* csr_sdio_linux_remove_irq() */


/*
 * ---------------------------------------------------------------------------
 *  csr_sdio_linux_install_irq
 *
 *      Register the interrupt handler.
 *      This means that the linux layer can process interrupts.
 *
 *  Arguments:
 *      sdio      SDIO context pointer
 *
 *  Returns:
 *      Status of the removal.
 * ---------------------------------------------------------------------------
 */
int
csr_sdio_linux_install_irq(CsrSdioFunction *function)
{
    struct sdio_func *func = (struct sdio_func *)function->priv;
    int r;

    /* Register our interrupt handle */
    _sdio_claim_host(func);
    r = sdio_claim_irq(func, uf_glue_sdio_int_handler);
    _sdio_release_host(func);

    /* If the interrupt was installed earlier, is fine */
    if (r == -EBUSY) {
        r = 0;
    }

    return r;
} /* csr_sdio_linux_install_irq() */



/*
 * ---------------------------------------------------------------------------
 *  uf_glue_sdio_probe
 *
 *      Card insert callback.
 *
 * Arguments:
 *      func            Our (glue layer) context pointer.
 *
 * Returns:
 *      UniFi driver error code.
 * ---------------------------------------------------------------------------
 */
static int
uf_glue_sdio_probe(struct sdio_func *func,
                   const struct sdio_device_id *id)
{
    int r;
    int instance;
    CsrSdioFunction *sdio_ctx;

    func_enter();

    /* First of all claim the SDIO driver */
    _sdio_claim_host(func);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
    /* Assume that the card is already powered */
    card_is_powered = 1;
#endif

    /* Assumes one card per host, which is true for SDIO */
    instance = func->card->host->index;
    printk("sdio bus_id: %16s - UniFi card 0x%X inserted\n",
           sdio_func_id(func), instance);

    /* Allocate context */
    sdio_ctx = (CsrSdioFunction *)kmalloc(sizeof(CsrSdioFunction),
                                          GFP_KERNEL);
    if (sdio_ctx == NULL) {
        _sdio_release_host(func);
        return -ENOMEM;
    }

    /* Initialise the context */
    sdio_ctx->sdioId.manfId  = func->vendor;
    sdio_ctx->sdioId.cardId  = func->device;
    sdio_ctx->sdioId.sdioFunction  = func->num;
    sdio_ctx->sdioId.sdioInterface = func->class;
    sdio_ctx->blockSize = func->cur_blksize;
    sdio_ctx->priv = (void *)func;
    sdio_ctx->features = 0;

    /* Module parameter enables byte mode */
    if (sdio_byte_mode) {
        sdio_ctx->features |= CSR_SDIO_FEATURE_BYTE_MODE;
    }

#ifdef MMC_QUIRK_LENIENT_FN0
    func->card->quirks |= MMC_QUIRK_LENIENT_FN0;
#endif

    /* Pass context to the SDIO driver */
    sdio_set_drvdata(func, sdio_ctx);

    /* Register this device with the SDIO function driver */
    /* Call the main UniFi driver inserted handler */
    r = -EINVAL;
    if (sdio_func_drv && sdio_func_drv->inserted) {
        uf_add_os_device(instance, &func->dev);
        r = sdio_func_drv->inserted(sdio_ctx);
    }

    /* We have finished, so release the SDIO driver */
    _sdio_release_host(func);

    func_exit();
    return r;
} /* uf_glue_sdio_probe() */


/*
 * ---------------------------------------------------------------------------
 *  uf_glue_sdio_remove
 *
 *      Card removal callback.
 *
 * Arguments:
 *      func            Our (glue layer) context pointer.
 *
 * Returns:
 *      UniFi driver error code.
 * ---------------------------------------------------------------------------
 */
static void
uf_glue_sdio_remove(struct sdio_func *func)
{
    CsrSdioFunction *sdio_ctx;

    sdio_ctx = sdio_get_drvdata(func);
    if (!sdio_ctx) {
        return;
    }

    func_enter();

    unifi_info(NULL, "UniFi card removed\n");

    /* Clean up the SDIO function driver */
    if (sdio_func_drv && sdio_func_drv->removed) {
        uf_remove_os_device(func->card->host->index);
        sdio_func_drv->removed(sdio_ctx);
    }

    kfree(sdio_ctx);

    func_exit();

} /* uf_glue_sdio_remove */


/*
 * SDIO ids *must* be statically declared, so we can't take
 * them from the list passed in csr_sdio_register_driver().
 */
static const struct sdio_device_id unifi_ids[] = {
    { SDIO_DEVICE(SDIO_MANF_ID_CSR,SDIO_CARD_ID_UNIFI_3) },
    { SDIO_DEVICE(SDIO_MANF_ID_CSR,SDIO_CARD_ID_UNIFI_4) },
    { /* end: all zeroes */				},
};

MODULE_DEVICE_TABLE(sdio, unifi_ids);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
#ifdef CONFIG_PM

/*
 * ---------------------------------------------------------------------------
 *  uf_glue_sdio_suspend
 *
 *      Card suspend callback.
 *
 * Arguments:
 *      dev            The struct device owned by the MMC driver
 *
 * Returns:
 *      None
 * ---------------------------------------------------------------------------
 */
static int
uf_glue_sdio_suspend(struct device *dev)
{
    struct sdio_func *func;
    CsrSdioFunction *sdio_ctx;

    func_enter();

    func = dev_to_sdio_func(dev);
    WARN_ON(!func);

    sdio_ctx = sdio_get_drvdata(func);
    WARN_ON(!sdio_ctx);

    unifi_trace(NULL, UDBG1, "System Suspend...\n");

    /* Clean up the SDIO function driver */
    if (sdio_func_drv && sdio_func_drv->suspend) {
        sdio_func_drv->suspend(sdio_ctx);
    }

    func_exit();
    return 0;
} /* uf_glue_sdio_suspend */


/*
 * ---------------------------------------------------------------------------
 *  uf_glue_sdio_resume
 *
 *      Card resume callback.
 *
 * Arguments:
 *      dev            The struct device owned by the MMC driver
 *
 * Returns:
 *      None
 * ---------------------------------------------------------------------------
 */
static int
uf_glue_sdio_resume(struct device *dev)
{
    struct sdio_func *func;
    CsrSdioFunction *sdio_ctx;

    func_enter();

    func = dev_to_sdio_func(dev);
    WARN_ON(!func);

    sdio_ctx = sdio_get_drvdata(func);
    WARN_ON(!sdio_ctx);

    unifi_trace(NULL, UDBG1, "System Resume...\n");

    /* Clean up the SDIO function driver */
    if (sdio_func_drv && sdio_func_drv->resume) {
        sdio_func_drv->resume(sdio_ctx);
    }

    func_exit();
    return 0;

} /* uf_glue_sdio_resume */

static struct dev_pm_ops unifi_pm_ops = {
    .suspend = uf_glue_sdio_suspend,
    .resume  = uf_glue_sdio_resume,
};

#define UNIFI_PM_OPS  (&unifi_pm_ops)

#else

#define UNIFI_PM_OPS  NULL

#endif /* CONFIG_PM */
#endif /* 2.6.32 */

static struct sdio_driver unifi_driver = {
    .probe      = uf_glue_sdio_probe,
    .remove     = uf_glue_sdio_remove,
    .name       = "unifi",
    .id_table	= unifi_ids,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
    .drv.pm     = UNIFI_PM_OPS,
#endif /* 2.6.32 */
};


/*
 * ---------------------------------------------------------------------------
 *  CsrSdioFunctionDriverRegister
 *  CsrSdioFunctionDriverUnregister
 *
 *      These functions are called from the main module load and unload
 *      functions. They perform the appropriate operations for the
 *      linux MMC/SDIO driver.
 *
 *  Arguments:
 *      sdio_drv    Pointer to the function driver's SDIO structure.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
CsrInt32
CsrSdioFunctionDriverRegister(CsrSdioFunctionDriver *sdio_drv)
{
    int r;

    printk("UniFi: Using native Linux MMC driver for SDIO.\n");

    if (sdio_func_drv) {
        unifi_error(NULL, "sdio_mmc: UniFi driver already registered\n");
        return -CSR_EINVAL;
    }

    /* Save the registered driver description */
    /*
     * FIXME:
     * Need a table here to handle a call to register for just one function.
     * mmc only allows us to register for the whole device
     */
    sdio_func_drv = sdio_drv;

    /* Register ourself with mmc_core */
    r = sdio_register_driver(&unifi_driver);
    if (r) {
        printk(KERN_ERR "unifi_sdio: Failed to register UniFi SDIO driver: %d\n", r);
        return convert_sdio_error(r);
    }

    return 0;
} /* CsrSdioFunctionDriverRegister() */



void
CsrSdioFunctionDriverUnregister(CsrSdioFunctionDriver *sdio_drv)
{
    printk(KERN_INFO "UniFi: unregister from MMC sdio\n");
    sdio_unregister_driver(&unifi_driver);

    sdio_func_drv = NULL;

} /* CsrSdioFunctionDriverUnregister() */

