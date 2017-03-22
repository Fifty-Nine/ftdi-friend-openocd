/***************************************************************************
 *   JTAG driver for the Adafruit FTDI Friend board.                       *
 *                                                                         *
 *   Copyright (c) 2017 Tim Prince                                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <jtag/interface.h>
#include <jtag/commands.h>
#include <ftdi.h>

struct ftdi_context *ftdi;
static const int ftdi_friend_vid = 0x0403;
static const int ftdi_friend_pid = 0x6001;

enum ft232r_pins {
    PIN_TXD = 0x01,
    PIN_RXD = 0x02,
    PIN_RTS = 0x04,
    PIN_CTS = 0x08,

    /*
     * This pin is unconnected by default but can be connected to the
     * RTS output on the 6-pin header.
     */
    PIN_DTR = 0x10,

    /* These are unconnected on the FTDI friend. */
    PIN_DSR = 0x20,
    PIN_DCD = 0x04,
    PIN_RI  = 0x80,

    /* These are aliases for the JTAG pins. */
    PIN_TCK = PIN_RXD,
    PIN_TDO = PIN_TXD,
    PIN_TDI = PIN_RTS,
    PIN_TMS = PIN_CTS,

    /*
     * Using the TRST pin requires soldering an extra wire to the DTR pad on
     * the bottom of the FTDI friend.
     */
    PIN_TRST = PIN_DTR,

    /*
     * Good luck connecting this pin. You will likely need some kind of
     * specialized equipment like fine-pitch probes or a 28-SSOP test clip.
     */
    PIN_SRST = PIN_DSR
};

static const uint8_t ftdi_output_mask =
    PIN_TDI | PIN_TMS | PIN_TCK | PIN_TRST | PIN_SRST;

static uint8_t latency_timer = 1;

enum { buffer_size = 64 };
struct buffer {
    uint8_t data[buffer_size];
    uint8_t available;
};

static struct buffer tx_buffer;
static struct buffer rx_buffer;

static int on_ftdi_error(const char *when)
{
    LOG_ERROR("libftdi call failed: %s: %s", when, ftdi_get_error_string(ftdi));
    ftdi_free(ftdi);
    ftdi = NULL;
    return ERROR_FAIL;
}

static void on_ftdi_warning(const char *when)
{
    LOG_WARNING("libftdi call failed: %s: %s", when, ftdi_get_error_string(ftdi));
}

static int buffer_empty(struct buffer *buf)
{
    return buf->available == 0;
}

static int buffer_full(struct buffer *buf)
{
    return buf->available == sizeof(buf->data);
}

static void buffer_enqueue(struct buffer *buf, uint8_t data)
{
    if (buf->available == sizeof(buf->data)) {
        LOG_ERROR("ftdi_friend xmit buffer overflow");
        return;
    }
    buf->data[buf->available++] = data;
}

static void flush_buffers(void)
{
    if (buffer_empty(&tx_buffer)) return;
    if (ftdi_write_data(ftdi, tx_buffer.data, tx_buffer.available) < 0) {
        on_ftdi_warning("ftdi_write_data");
    }
    tx_buffer.available = 0;

    int rc = ftdi_read_data(ftdi, rx_buffer.data, buffer_size);
    if (rc < 0) {
        on_ftdi_warning("ftdi_read_data");
        rx_buffer.available = 0;
    } else {
        rx_buffer.available = (uint8_t)rc;
    }
}


static int ftdi_friend_init(void)
{
    if ((ftdi = ftdi_new()) == 0) {
        LOG_ERROR("ftdi_new failed");
        return ERROR_FAIL;
    }

    if (ftdi_usb_open(ftdi, ftdi_friend_vid, ftdi_friend_pid)) {
        return on_ftdi_error("ftdi_usb_open");
    }

    if (ftdi_set_bitmode(ftdi, ftdi_output_mask, BITMODE_SYNCBB)) {
        return on_ftdi_error("ftdi_set_bitmode");
    }

    if ( ftdi_set_latency_timer(ftdi, latency_timer)) {
        return on_ftdi_error("ftdi_set_latency_timer");
    }

    if (ftdi_set_baudrate(ftdi, 11520)) {
        return on_ftdi_error("ftdi_set_baudrate");
    }

    return ERROR_OK;
}

static int ftdi_friend_quit(void)
{
    if (!ftdi) return ERROR_OK;

    int rc;
    if ((rc = ftdi_usb_close(ftdi))) {
        return on_ftdi_error("ftdi_usb_close");
    }

    ftdi_free(ftdi);
    ftdi = NULL;
    return ERROR_OK;
}

static int ftdi_friend_speed(int speed)
{
    return ERROR_OK;
}

static int ftdi_friend_speed_div(int speed, int *khz)
{
    return ERROR_OK;
}

static int ftdi_friend_khz(int khz, int *jtag_speed)
{
    return ERROR_OK;
}

static int handle_scan(struct jtag_command *cmd)
{
    return ERROR_OK;
}

static int handle_tlr_reset(struct jtag_command *cmd)
{
    return ERROR_OK;
}

static int handle_runtest(struct jtag_command *cmd)
{
    return ERROR_OK;
}

static int handle_reset(struct jtag_command *cmd)
{
    return ERROR_OK;
}

static int handle_pathmove(struct jtag_command *cmd)
{
    return ERROR_OK;
}

static int handle_sleep(struct jtag_command *cmd)
{
    return ERROR_OK;
}

static int handle_stableclocks(struct jtag_command *cmd)
{
    return ERROR_OK;
}

static int handle_tms(struct jtag_command *cmd)
{
    return ERROR_OK;
}

static int (* const jtag_cmd_handlers[])(struct jtag_command *cmd) = {
    [JTAG_SCAN]         = handle_scan,
    [JTAG_TLR_RESET]    = handle_tlr_reset,
    [JTAG_RUNTEST]      = handle_runtest,
    [JTAG_RESET]        = handle_reset,
    [JTAG_PATHMOVE]     = handle_pathmove,
    [JTAG_SLEEP]        = handle_sleep,
    [JTAG_STABLECLOCKS] = handle_stableclocks,
    [JTAG_TMS]          = handle_tms,
};

static int execute_one_command(struct jtag_command *cmd)
{
    assert(cmd->type < sizeof(jtag_cmd_handlers));
    if (!buffer_empty(&tx_buffer)) {
        flush_buffers();
    }
    return jtag_cmd_handlers[cmd->type](cmd);
}

static int ftdi_friend_execute_queue(void)
{
    for (struct jtag_command *cmd = jtag_command_queue;
         cmd != NULL;
         cmd = cmd->next) {
        execute_one_command(cmd);
    }
    return ERROR_OK;
}

COMMAND_HANDLER(ftdi_friend_set_latency_timer)
{
    if (CMD_ARGC != 1) {
        LOG_ERROR("ftdi_friend_set_latency_timer expects one argument "
                  "in the range [0-255]");
        return ERROR_OK;
    }

    COMMAND_PARSE_NUMBER(u8, CMD_ARGV[0], latency_timer);
    return ERROR_OK;
}

static const struct command_registration ftdi_friend_command_handlers[] = {
    {
        .name = "ftdi_friend_latency_timer",
        .handler = ftdi_friend_set_latency_timer,
        .mode = COMMAND_CONFIG,
        .help = "Set the latency timer parameter in the FTDI API.",
        .usage = "ftdi_friend_latency_timer [time]"
    },
    COMMAND_REGISTRATION_DONE
};

static const char * const ftdi_friend_transports[] = { NULL };

struct jtag_interface ftdi_friend_interface = {
    .name = "ftdi_friend",
    .commands = ftdi_friend_command_handlers,
    .transports = jtag_only,

    .init = ftdi_friend_init,
    .quit = ftdi_friend_quit,
    .speed = ftdi_friend_speed,
    .speed_div = ftdi_friend_speed_div,
    .khz = ftdi_friend_khz,
    .execute_queue = ftdi_friend_execute_queue
};
