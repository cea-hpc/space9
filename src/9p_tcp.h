#ifndef P9_TCP
#define P9_TCP

void msk_tcp_destroy_trans(msk_trans_t **ptrans);
int msk_tcp_init(msk_trans_t **ptrans, msk_trans_attr_t *attr);

int msk_tcp_connect(msk_trans_t *trans);
int msk_tcp_finalize_connect(msk_trans_t *trans);

struct ibv_mr *msk_tcp_reg_mr(msk_trans_t *trans, void *memaddr, size_t size, int access);
int msk_tcp_dereg_mr(struct ibv_mr *mr);

int msk_tcp_post_n_recv(msk_trans_t *trans, msk_data_t *data, int num_sge, ctx_callback_t callback, ctx_callback_t err_callback, void *callback_arg);
int msk_tcp_post_n_send(msk_trans_t *trans, msk_data_t *data_arg, int num_sge, ctx_callback_t callback, ctx_callback_t err_callback, void *callback_arg);

#endif
