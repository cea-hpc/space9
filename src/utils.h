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

#include <stdio.h>
#include <errno.h>

#define ERROR_LOG(fmt, args...) fprintf(stderr, "ERROR: %s (%d), %s: " fmt "\n", __FILE__, __LINE__, __func__, ##args)
//#define ERROR_LOG(fmt, args...)
#define INFO_LOG(debug, fmt, args...) if (debug) fprintf(stderr, "INFO:  %s (%d), %s: " fmt "\n", __FILE__, __LINE__, __func__, ##args)
//#define INFO_LOG(fmt, args...)


#define atomic_inc(x) __sync_fetch_and_add(&x, 1)
#define atomic_postinc(x) __sync_add_and_fetch(&x, 1)
#define atomic_dec(x) __sync_fetch_and_sub(&x, 1)
#define atomic_postdec(x) __sync_sub_and_fetch(&x, 1)


static inline int set_size(uint32_t *val, char *unit) {
        switch(unit[0]) {
                case 'k':
                case 'K':
                        *val *= 1024;
                        break;
                case 'm':
                case 'M':
                        *val *= 1024 * 1024;
                        break;
                case 'g':
                case 'G':
                        *val *= 1024 * 1024 * 1024;
                        break;
                default:
                        ERROR_LOG("unknown unit '%c'", unit[0]);
                        return EINVAL;
        }

        return 0;
}

static inline int set_size64(uint64_t *val, char *unit) {
        switch(unit[0]) {
                case 'k':
                case 'K':
                        *val *= 1024L;
                        break;
                case 'm':
                case 'M':
                        *val *= 1024 * 1024L;
                        break;
                case 'g':
                case 'G':
                        *val *= 1024 * 1024 * 1024L;
                        break;
		case 't':
		case 'T':
			*val *= 1024 * 1024 * 1024 * 1024L;
			break;
                default:
                        ERROR_LOG("unknown unit '%c'", unit[0]);
                        return EINVAL;
        }

        return 0;
}


/**
 * @brief canonicalize path
 *
 * @param path to canonicalize, must be null terminated
 * @return length of new path if >= 0, -errno on error
 */
int path_canonicalizer(char *path);

int path_dirname(char *path, char *dst, size_t dst_len);
void path_basename(char *path, char **basename);
int path_split(char *path, char **dirname, char **basename);
