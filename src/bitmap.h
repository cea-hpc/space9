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

#ifndef BITMAP_H
#define BITMAP_H

#define ffsll __builtin_ffsll
#define popcountll __builtin_popcountll

typedef uint64_t bitmap_t;
#define BITS_PER_WORD  64
#define WORD_OFFSET(b) ((b) / BITS_PER_WORD)
#define BIT_OFFSET(b)  ((b) % BITS_PER_WORD)
#define BITMAP_SIZE(s)  (

static inline bitmap_t *bitmap_init(int size) {
	return calloc(size/BITS_PER_WORD + ((size % BITS_PER_WORD) == 0 ? 0 : 1), sizeof(bitmap_t));
}

static inline void bitmap_destroy(bitmap_t **pmap) {
	free(*pmap);
	*pmap = NULL;
}

static inline void bitmap_clear(bitmap_t *map, uint32_t max) {
	memset(map, 0, (max/BITS_PER_WORD + ((max % BITS_PER_WORD) == 0 ? 0 : 1)) * sizeof(bitmap_t));
}

static inline void bitmap_foreach(bitmap_t *map, uint32_t max, int (*callback)(void *, uint32_t), void *cb_arg) {
	uint32_t maxw, i, j;

	maxw = max / BITS_PER_WORD + 1;

	for (i=0; i < maxw; i++) {
		if (map[i] == 0LL)
			continue;

		for (j=0; j < BITS_PER_WORD; j++) {
			if (map[i] & (1 << j))
				callback(cb_arg, i*BITS_PER_WORD+j);
		}
	}

	for (j=0; j < BIT_OFFSET(max); j++) {
		if (map[i] & (1 << j))
			callback(cb_arg, i*BITS_PER_WORD+j);
	}
}

static inline void set_bit(bitmap_t *map, int n) { 
	map[WORD_OFFSET(n)] |= (1ULL << BIT_OFFSET(n));
}

static inline void clear_bit(bitmap_t *map, int n) {
	map[WORD_OFFSET(n)] &= ~(1ULL << BIT_OFFSET(n)); 
}

static inline int get_bit(bitmap_t *map, int n) {
	bitmap_t bit = map[WORD_OFFSET(n)] & (1ULL << BIT_OFFSET(n));
	return bit != 0; 
}

static inline uint32_t get_and_set_first_bit(bitmap_t *map, uint32_t max) {
	uint32_t maxw, i;

	maxw = max / BITS_PER_WORD;
	i = 0;

	while (i < maxw && map[i] == ~0LL)
		i++;

	if (i == maxw) {
		if (BIT_OFFSET(max) != 0 && map[i] != ~0LL) {
			i = maxw*BITS_PER_WORD + ffsll(~map[maxw]) - 1;
			if (i < max)
				set_bit(map, i);
			else
				i = max;
		} else {
			i = max;
		}
	} else {
		i = i*BITS_PER_WORD + ffsll(~map[i]) - 1;
		set_bit(map, i);
	}

	return i;
}

static inline uint32_t get_and_clear_first_bit(bitmap_t *map, uint32_t max) {
	uint32_t maxw, i;

	maxw = max / BITS_PER_WORD;
	i = 0;

	while (i < maxw && map[i] == 0LL)
		i++;

	if (i == maxw) {
		if (BIT_OFFSET(max) != 0 && map[i] != 0LL) {
			i = maxw*BITS_PER_WORD + ffsll(map[maxw]) - 1;
			if (i < max)
				clear_bit(map, i);
			else
				i = max;
		} else {
			i = max;
		}
	} else {
		i = i*BITS_PER_WORD + ffsll(map[i]) - 1;
		clear_bit(map, i);
	}

	return i;
}

static inline uint32_t bitcount(bitmap_t *map, uint32_t max) {
	uint32_t count, maxw, i;

	count = 0;
	maxw = max / BITS_PER_WORD;
	i = 0;

	for (i=0; i < maxw; i++)
		count += popcountll(map[i]);

	// Assume the end of the bitmap is padded with 0
	if (BIT_OFFSET(max) != 0)
		count += popcountll(map[maxw]);

	return count;
}

#endif
