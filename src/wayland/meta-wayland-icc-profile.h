/*
 * Copyright (C) 2024 SUSE Software Solutions Germany GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Joan Torres <joan.torres@suse.com>
 */

#pragma once

#include <gio/gio.h>
#include <stdint.h>

void meta_wayland_icc_profile_prepare_mem_async (int                 icc_fd,
                                                 uint32_t            offset,
                                                 uint32_t            length,
                                                 GAsyncReadyCallback callback,
                                                 gpointer            user_data);

int meta_wayland_icc_profile_prepare_mem_finish (GAsyncResult  *result,
                                                 int           *icc_fd,
                                                 uint32_t      *length,
                                                 GError       **error);
