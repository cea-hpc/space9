/* bucket */

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
	bucket = malloc(sizeof(bucket_t)+max*sizeof(void*));
	if (bucket) {
		memset(bucket, 0, sizeof(bucket_t)+max*sizeof(void*));
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

static inline void bucket_put(bucket_t *bucket, void *item) {
	pthread_mutex_lock(&bucket->lock);
	if (bucket->count >= bucket->size) {
		free(item);
	} else {
		bucket->array[bucket->last] = item;
		bucket->last++;
		bucket->count++;
		if (bucket->last == bucket->size)
			bucket->last = 0;
	}
	pthread_mutex_unlock(&bucket->lock);
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
