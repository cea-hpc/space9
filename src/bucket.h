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

#ifndef BUCKET_H
#define BUCKET_H

#include <stdlib.h>

typedef struct bucket {
	size_t size;
	size_t first;
	size_t last;
	size_t count;
	size_t alloc_size;
	pthread_mutex_t lock;
	void * array[0];
} bucket_t;

static inline bucket_t *bucket_init(size_t max, size_t alloc_size) {
	bucket_t *bucket;
	bucket = calloc(1, sizeof(bucket_t)+max*sizeof(void*));
	if (bucket) {
		bucket->size = max;
		bucket->first = 0;
		bucket->last = 0;
		bucket->count = 0;
		bucket->alloc_size = alloc_size;
		pthread_mutex_init(&bucket->lock, NULL);
	}

	return bucket;
}

static inline void bucket_destroy(bucket_t *bucket) {
	pthread_mutex_destroy(&bucket->lock);
	free(bucket);
}

static inline void bucket_put(bucket_t *bucket, void **pitem) {
	pthread_mutex_lock(&bucket->lock);
	if (bucket->count >= bucket->size) {
		free(*pitem);
	} else {
		bucket->array[bucket->last] = *pitem;
		bucket->last++;
		bucket->count++;
		if (bucket->last == bucket->size)
			bucket->last = 0;
	}
	pthread_mutex_unlock(&bucket->lock);
	*pitem = NULL;
}


static inline void * bucket_get(bucket_t *bucket) {
	void * item;
	pthread_mutex_lock(&bucket->lock);

	if (bucket->count == 0) {
		item = malloc(bucket->alloc_size);
	} else {
		item = bucket->array[bucket->first];
		bucket->first++;
		bucket->count--;
		if (bucket->first == bucket->size)
			bucket->first = 0;
	}
	pthread_mutex_unlock(&bucket->lock);

	return item;
}

#endif
