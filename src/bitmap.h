/* bitmap */

#define ffsll __builtin_ffsll
#define popcount __builtin_popcount

typedef uint64_t bitmap_t;
#define BITS_PER_WORD  64
#define WORD_OFFSET(b) ((b) / BITS_PER_WORD)
#define BIT_OFFSET(b)  ((b) % BITS_PER_WORD)
#define BITMAP_SIZE(s)  (

static inline void set_bit(bitmap_t *map, int n) { 
	map[WORD_OFFSET(n)] |= (1 << BIT_OFFSET(n));
}

static inline void clear_bit(bitmap_t *map, int n) {
	map[WORD_OFFSET(n)] &= ~(1 << BIT_OFFSET(n)); 
}

static inline int get_bit(bitmap_t *map, int n) {
	bitmap_t bit = map[WORD_OFFSET(n)] & (1 << BIT_OFFSET(n));
	return bit != 0; 
}

static inline uint32_t get_and_set_first_bit(bitmap_t *map, uint32_t max) {
	uint32_t maxw, i;

	maxw = max / BITS_PER_WORD;

	while (map[i] == ~0L && i < maxw)
		i++;

	if (i == maxw) {
		if (BIT_OFFSET(max) != 0)
			i = maxw*BITS_PER_WORD + ffsll(~map[maxw]);
		else
			i = max;

		return i;
	}

	i = i*BITS_PER_WORD + ffsll(~map[i]);
	set_bit(map, i);
	return i;
}

static inline uint32_t bitcount(bitmap_t *map, uint32_t max) {
	uint32_t count, maxw, i;

	count = 0;
	maxw = max / BITS_PER_WORD;

	for (i=0; i < maxw; i++)
		count += popcount(map[i]);

	// Assume the end of the bitmap is padded with 0
	if (BIT_OFFSET(max) != 0)
		count += popcount(map[maxw]);

	return count;
}
