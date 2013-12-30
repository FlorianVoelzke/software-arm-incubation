/*
 *  Copyright (c) 2013 Stefan Taferner <stefan.taferner@gmx.at>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 */

#include "sb_main.h"

#include "sb_bus.h"
#include "sb_proto.h"
#include "sb_memory.h"
#include "sb_eeprom.h"
#include "sb_comobj.h"
#include "internal/sb_hal.h"

#ifdef __USE_CMSIS
# include "LPC11xx.h"
#endif
#include "gpio.h"

/**
 * The system time in usec since the last reset. The resolution of this time
 * depends on the wakeup time. This time is not available when the wakeup timer
 * is disabled.
 *
 * @see sb_set_wakeup_time()
 */
unsigned int sbSysTime;

// Increment for sbSysTime on SysTick timeout
static unsigned int sbSysTickInc = -1;

/**
 * The system tick handler.
 */
void SysTick_Handler()
{
    sbSysTime += sbSysTickInc;
}

/**
 * Set the wakeup timer.
 *
 * @param timeout - the timeout in usec between wakeups. 0 to disable the timer.
 *
 * @brief This timer generates wakeup interrupts. It uses the SysTick timer, which
 * is a 24bit timer that runs with the system clock. The default timeout is 1msec.
 * Shorter timeouts give exacter sbSysTime. Longer timeouts use less resources.
 */
void sb_set_wakeup_time(unsigned int timeout)
{
    sbSysTickInc = timeout;

    if (timeout)
    {
        unsigned short usecTicks = SystemCoreClock / 1000000; // Timer ticks in one usec
        SysTick_Config(timeout * usecTicks);
    }
    else SysTick->CTRL = 0; // disable SysTick timer
}

/**
 * Initialize the library. Call this function once when the program starts.
 */
void sb_init()
{
    sb_set_wakeup_time(1000);

    sb_eeprom_init(0);
    sb_init_bus();
    sb_init_proto();

    LPC_GPIO[SB_PROG_PORT]->DIR |= 1 << SB_PROG_BIT; // Set prog button+led to output
    LPC_IOCON_PROG = SB_PROG_IOCON;                  // IO configuration for prog button+led

    sbUserRam->status = 0x2e;
}

/**
 * The lib's main processing. Call this function regularily from your main().
 * It should be called at least every 100ms.
 */
void sb_main_loop()
{
    if (sbRecvTelegramLen && sbSendCurTelegram == 0)
        sb_process_tel();

    if (sbSendCurTelegram == 0)
        sb_send_next_tel();

    if (sbEepromDirty && sbState == SB_IDLE && sbSendCurTelegram == 0 &&
        !sb_prog_mode_active() && !sb_connected())
    {
        sbEepromDirty = 0;
        sb_eeprom_update();
    }

    //
    // Handle system time
    //


    //
    // Handle programming-mode button and LED
    //
    static unsigned short progButtonLevel = 0;
    const unsigned int progButtonMask = 1 << SB_PROG_BIT;
    LPC_GPIO[SB_PROG_PORT]->DIR &= ~progButtonMask; // Set prog button+led to input
    if (LPC_GPIO[SB_PROG_PORT]->MASKED_ACCESS[progButtonMask] == 0)
    {
        if (progButtonLevel < 255)
        {
            if (++progButtonLevel == 254)
                sbUserRam->status ^= 0x81;  // toggle programming mode and checksum bit
        }
    }
    else if (progButtonLevel > 0)
    {
        --progButtonLevel;
    }

    LPC_GPIO[SB_PROG_PORT]->DIR |= 1 << SB_PROG_BIT; // Set prog button+led to output

    if (sbUserRam->status & SB_STATUS_PROG)
        LPC_GPIO[SB_PROG_PORT]->MASKED_ACCESS[progButtonMask] = 0;
    else LPC_GPIO[SB_PROG_PORT]->MASKED_ACCESS[progButtonMask] = progButtonMask;
}

/**
 * Set manufacturer data, manufacturer-ID, and device type.
 *
 * @param data - the manufacturer data
 * @param manufacturer - the manufacturer ID
 * @param deviceType - the device type
 * @param version - the version of the application program
 */
void sb_set_appdata(unsigned short data, unsigned short manufacturer, unsigned short deviceType,
                    unsigned char version)
{
    sbEeprom->manuDataH = data >> 8;
    sbEeprom->manuDataL = data;

    sbEeprom->manufacturerH = manufacturer >> 8;
    sbEeprom->manufacturerL = manufacturer;

    sbEeprom->deviceTypeH = deviceType >> 8;
    sbEeprom->deviceTypeL = deviceType;

    sbEeprom->version = version;
}
