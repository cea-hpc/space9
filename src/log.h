#define ERROR_LOG(fmt, args...) fprintf(stderr, "ERROR: %s (%d), %s: " fmt "\n", __FILE__, __LINE__, __func__, ##args)
//#define ERROR_LOG(fmt, args...)
#define INFO_LOG(fmt, args...) 	printf("INFO:  %s (%d), %s: " fmt "\n", __FILE__, __LINE__, __func__, ##args)
//#define INFO_LOG(fmt, args...)


#define atomic_inc(x) __sync_fetch_and_add(&x, 1)
#define atomic_postinc(x) __sync_add_and_fetch(&x, 1)
#define atomic_dec(x) __sync_fetch_and_sub(&x, 1)
#define atomic_postdec(x) __sync_sub_and_fetch(&x, 1)
