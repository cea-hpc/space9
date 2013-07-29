/*
 * Copyright CEA/DAM/DIF (2013)
 * Contributor: Dominique Martinet <dominique.martinet@cea.fr>
 *
 * This file is part of the space9 9P userspace library.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with space9.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _SETTINGS_H
#define _SETTINGS_H

// shared with ganesha:
#define _9P_FID_PER_CONN        1024


// default values for conf
#define DEFAULT_UID        0
#define DEFAULT_RECV_NUM   64
#define DEFAULT_MSIZE      64*1024
#define DEFAULT_PORT_RDMA  5640
#define DEFAULT_PORT_TCP   564
#define DEFAULT_MAX_FID    1024
#define DEFAULT_PIPELINE   2
#define DEFAULT_DEBUG      0x01
#define DEFAULT_RDMA_DEBUG 0x01

// max tag = recv_num for ganesha
#define DEFAULT_MAX_TAG  100


#endif
