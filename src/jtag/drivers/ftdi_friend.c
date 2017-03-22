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


static int ftdi_friend_init(void)
{
    return ERROR_OK;
}

static int ftdi_friend_quit(void)
{
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

static int ftdi_friend_execute_queue(void)
{
    return ERROR_OK;
}

static const struct command_registration ftdi_friend_command_handlers[] = {
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
