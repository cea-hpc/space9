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
