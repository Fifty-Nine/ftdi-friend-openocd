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
#include <jtag/drivers/bitq.h>
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
     * Using the SRST pin requires soldering an extra wire to the DTR pad on
     * the bottom of the FTDI friend.
     */
    PIN_SRST = PIN_DTR,

    /*
     * Good luck connecting this pin. You will likely need some kind of
     * specialized equipment like fine-pitch probes or a 28-SSOP test clip.
     */
    PIN_TRST = PIN_DSR
};

static const uint8_t ftdi_output_mask =
    PIN_TDI | PIN_TMS | PIN_TCK | PIN_TRST | PIN_SRST;

static uint8_t latency_timer = 1;

enum { buffer_size = 1 << 14, frame_size = 1 << 8 };

struct buffer {
    uint8_t data[buffer_size];
    uint16_t available;
};

static struct buffer tx_buffer;
static struct buffer rx_buffer;
uint16_t rx_idx = 0;

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
    tx_buffer.available = 0;
    rx_buffer.available = 0;
}

static int buffer_empty(struct buffer *buf)
{
    return buf->available == 0;
}

static int buffer_full(struct buffer *buf)
{
    return buf->available == sizeof(buf->data);
}

static int flush_buffers(void)
{
    if (buffer_empty(&tx_buffer)) return ERROR_OK;

    int num_to_write = tx_buffer.available;
    int num_to_read = tx_buffer.available;
    uint8_t *wr_idx = tx_buffer.data;
    uint8_t *rwr_idx = tx_buffer.data;
    uint8_t *rd_idx = rx_buffer.data;

    uint8_t rd_buffer[frame_size];
    rx_idx = 0;
    tx_buffer.available = 0;

    while (1) {
        struct ftdi_transfer_control *wtc = NULL;
        struct ftdi_transfer_control *rtc = NULL;

        if (num_to_write > 0) {
            wtc = ftdi_write_data_submit(ftdi, wr_idx, MIN(frame_size, num_to_write));
        }

        if (num_to_read > 0) {
            rtc = ftdi_read_data_submit(ftdi, rd_buffer, MIN(frame_size, num_to_read));
        }

        if (!wtc && !rtc) return ERROR_OK;

        if (wtc) {
            int rc = ftdi_transfer_data_done(wtc);
            if (rc < 0) {
                on_ftdi_warning("write");
                return ERROR_FAIL;
            }
            num_to_write -= rc;
            wr_idx += rc;
        }

        if (rtc) {
            int rc = ftdi_transfer_data_done(rtc);
            if (rc < 0) {
                on_ftdi_warning("read");
                return ERROR_FAIL;
            }

            for (int i = 0; i < rc; ++i, ++rwr_idx)
            {
                if (*rwr_idx & PIN_TDO) {
                    *rd_idx = !!(rd_buffer[i] & PIN_TDO);
                    ++rd_idx;
                    rx_buffer.available++;
                }
            }
            num_to_read -= rc;
        }

    }
}

static void buffer_enqueue(struct buffer *buf, uint8_t data)
{
    if (buffer_full(buf)) {
        flush_buffers();
    }
    buf->data[buf->available++] = data;
}

static int ftdi_friend_quit(void)
{
    if (!ftdi) return ERROR_OK;

    if (ftdi_usb_close(ftdi)) {
        return on_ftdi_error("ftdi_usb_close");
    }

    ftdi_free(ftdi);
    ftdi = NULL;
    return ERROR_OK;
}

static void write_data_pins(int tck, int tms, int tdi, int tdo_req)
{
    buffer_enqueue(
        &tx_buffer,
        (tck ? PIN_TCK : 0) |
        (tms ? PIN_TMS : 0) |
        (tdi ? PIN_TDI : 0) |
        (tdo_req ? PIN_TDO : 0) |
        PIN_TRST |
        PIN_SRST
    );
}

static int write_reset_pins(int trst, int srst)
{
    buffer_enqueue(
        &tx_buffer,
        (trst ? 0 : PIN_TRST) |
        (srst ? 0 : PIN_SRST)
    );
    return ERROR_OK;
}

static int clock_data(int tms, int tdi, int tdo_req) {
    write_data_pins(0, tms, tdi, 0);
    write_data_pins(1, tms, tdi, tdo_req);
    return ERROR_OK;
}

__attribute__((unused))
static void idle(void)
{
    write_data_pins(0, 0, 0, 0);
}

static int ftdi_friend_speed(int speed)
{
    if (ftdi_set_baudrate(ftdi, speed)) {
        on_ftdi_warning("ftdi_set_baudrate");
    }
    return ERROR_OK;
}

static int ftdi_friend_speed_div(int speed, int *khz)
{
    *khz = speed;
    return ERROR_OK;
}

static int ftdi_friend_khz(int khz, int *jtag_speed)
{
    if (khz == 0) {
        LOG_ERROR("RTCK not supported.");
        return ERROR_FAIL;
    }
    *jtag_speed = khz;
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

static int ftdi_friend_sleep(unsigned long us)
{
    flush_buffers();
    jtag_sleep(us);
    return ERROR_OK;
}

static int ftdi_in_rdy(void)
{
    return rx_buffer.available;
}

static int ftdi_in(void)
{
    if (ftdi_in_rdy() > 0) {
        rx_buffer.available--;
        return rx_buffer.data[rx_idx++];
    }
    return -1;
}

static struct bitq_interface ftdi_friend_bitq = {
    .out = clock_data,
    .flush = flush_buffers,
    .sleep = ftdi_friend_sleep,
    .reset = write_reset_pins,
    .in_rdy = ftdi_in_rdy,
    .in = ftdi_in,
};

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

    if (ftdi_set_baudrate(ftdi, jtag_get_speed_khz())) {
        return on_ftdi_error("ftdi_set_baudrate");
    }

    bitq_interface = &ftdi_friend_bitq;
    return ERROR_OK;
}


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
    .execute_queue = bitq_execute_queue
};
