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

enum { buffer_size = 1 << 12, frame_size = 1 << 8 };

struct buffer {
    uint8_t data[buffer_size];
    uint16_t available;
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

static void log_modem_status(void)
{
    if (!LOG_LEVEL_IS(LOG_LVL_DEBUG)) { return; }
    uint16_t status;
    if (ftdi_poll_modem_status(ftdi, &status)) {
        on_ftdi_warning("ftdi_poll_modem_status");
    }
    LOG_DEBUG("modem status: %d %4.4x", tx_buffer.available, status);
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
    if (buffer_full(buf)) {
        LOG_ERROR("ftdi_friend xmit buffer overflow");
        return;
    }
    buf->data[buf->available++] = data;
}

static void flush_buffers(void)
{
    if (buffer_empty(&tx_buffer)) return;

    rx_buffer.available = 0;
    uint8_t *wr_idx = tx_buffer.data;
    uint8_t *rd_idx = rx_buffer.data;
    while (tx_buffer.available) {
        int wr_count = MIN(frame_size, tx_buffer.available);
        int rc = ftdi_write_data(ftdi, wr_idx, wr_count);
        if (rc < 0) {
            on_ftdi_warning("ftdi_write_data");
        } else {
            wr_idx += rc;
            tx_buffer.available -= rc;
        }

        int rd_count = MIN(buffer_size, rx_buffer.available + frame_size);
        rc = ftdi_read_data(ftdi, rd_idx, rd_count);
        if (rc < 0) {
            on_ftdi_warning("ftdi_read_data");
        } else {
            rd_idx += rc;
            rx_buffer.available += rc;
        }
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

    log_modem_status();

    return ERROR_OK;
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

static void write_data_pins(int tck, int tms, int tdi)
{
    buffer_enqueue(
        &tx_buffer,
        (tck ? PIN_TCK : 0) |
        (tms ? PIN_TMS : 0) |
        (tdi ? PIN_TDI : 0) |
        PIN_TRST |
        PIN_SRST
    );
}

static void write_reset_pins(int trst, int srst)
{
    buffer_enqueue(
        &tx_buffer,
        (trst ? 0 : PIN_TRST) |
        (srst ? 0 : PIN_SRST)
    );
}

static void clock_data(int tms, int tdi) {
    write_data_pins(0, tms, tdi);
    write_data_pins(1, tms, tdi);
}

static void idle(void) {
    write_data_pins(0, 0, 0);
}

static void write_tms_pulse_train(uint8_t data, int count)
{
    assert(count <= 8);
    for (int i = 0; i < count; ++i, data >>= 1) {
        clock_data(data & 1, 0);
    }
    idle();
}

static void state_transition(void)
{
    uint8_t data = tap_get_tms_path(tap_get_state(), tap_get_end_state());
    int count = tap_get_tms_path_len(tap_get_state(), tap_get_end_state());

    write_tms_pulse_train(data, count);

    tap_set_state(tap_get_end_state());
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
    __auto_type sm = cmd->cmd.statemove;

    assert(tap_is_state_stable(sm->end_state));
    tap_set_end_state(sm->end_state);

    state_transition();

    return ERROR_OK;
}

static int handle_runtest(struct jtag_command *cmd)
{
    __auto_type rt = cmd->cmd.runtest;
    assert(tap_is_state_stable(rt->end_state));
    tap_set_end_state(rt->end_state);

    if (tap_get_state() != TAP_IDLE) {
        tap_set_end_state(TAP_IDLE);
        state_transition();
    }

    write_tms_pulse_train(0, rt->num_cycles);

    tap_set_end_state(rt->end_state);
    if (tap_get_state() != tap_get_end_state()) {
        state_transition();
    }

    return ERROR_OK;
}

static int handle_reset(struct jtag_command *cmd)
{
    __auto_type reset = cmd->cmd.reset;

    if (reset->trst ||
        (reset->srst && (jtag_get_reset_config() & RESET_SRST_PULLS_TRST))) {
        tap_set_state(TAP_RESET);
    }
    write_reset_pins(reset->trst, reset->srst);

    return ERROR_OK;
}

static int handle_pathmove(struct jtag_command *cmd)
{
    __auto_type pm = cmd->cmd.pathmove;

    for (int i = 0; i < pm->num_states; ++i) {
        int tms = tap_state_transition(tap_get_state(), true) == pm->path[i];
        assert(tms || (tap_state_transition(tap_get_state(), false) == pm->path[i]));

        clock_data(tms, 0);

        tap_set_state(pm->path[i]);
    }

    idle();

    return ERROR_OK;
}

static int handle_sleep(struct jtag_command *cmd)
{
    jtag_sleep(cmd->cmd.sleep->us);
    return ERROR_OK;
}

static int handle_stableclocks(struct jtag_command *cmd)
{
    int tms = tap_get_state() == TAP_RESET;
    int num_cycles = cmd->cmd.stableclocks->num_cycles;
    for (int i = 0; i < num_cycles; ++i)
    {
        clock_data(tms, 0);
    }
    return ERROR_OK;
}

static int handle_tms(struct jtag_command *cmd)
{
    __auto_type tms = cmd->cmd.tms;

    for (unsigned i = 0; i < tms->num_bits; ++i) {
        int bit = (tms->bits[i / 8] >> (i % 8)) & 1;
        clock_data(bit, 0);
    }

    idle();

    tap_set_state(tap_get_end_state());

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
    assert(buffer_empty(&tx_buffer));
    assert(cmd->type < sizeof(jtag_cmd_handlers));
    int rc = jtag_cmd_handlers[cmd->type](cmd);
    flush_buffers();
    log_modem_status();
    return rc;
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
