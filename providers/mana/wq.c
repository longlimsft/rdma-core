/*
 * Copyright (c) 2021 Microsoft Corporation. All rights reserved.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <util/compiler.h>
#include <util/util.h>
#include <sys/mman.h>

#include <infiniband/driver.h>

#include <infiniband/kern-abi.h>
#include <rdma/mana-abi.h>
#include <kernel-abi/mana-abi.h>

#include "mana.h"

DECLARE_DRV_CMD(mana_create_wq, IB_USER_VERBS_EX_CMD_CREATE_WQ,
		mana_ib_create_wq, mana_ib_create_wq_resp);

DECLARE_DRV_CMD(mana_create_rwq_ind_table,
		IB_USER_VERBS_EX_CMD_CREATE_RWQ_IND_TBL,
		empty, mana_ib_create_rwq_ind_table_resp);


int mana_modify_wq(struct ibv_wq *ibwq, struct ibv_wq_attr *attr)
{
	mana_trace();
	return 0;
}

struct ibv_wq *mana_create_wq(struct ibv_context *context,
			      struct ibv_wq_init_attr *attr)
{
	int ret;
	struct mana_context *ctx = to_mctx(context);
	struct mana_wq *wq;
	struct mana_create_wq		wq_cmd = {};
	struct mana_create_wq_resp	wq_resp = {};
	struct mana_ib_create_wq	*wq_cmd_drv;
//	struct mana_ib_create_wq_resp	*wq_resp_drv;

	mana_trace();

	if (attr->max_wr > MAX_SEND_BUFFERS_PER_QUEUE) {
		mana_dbg("max_wr %d exceeds MAX_SEND_BUFFERS_PER_QUEUE\n",
			 attr->max_wr);
		return NULL;
	}

	wq = calloc(1, sizeof(*wq));
	if (!wq)
		return NULL;

	wq->sge = attr->max_sge;
	wq->wqe = attr->max_wr;
	wq->buf_size = attr->max_wr * WQ_ENTRY_SIZE;
	if (ctx->extern_alloc.alloc) {
		wq->buf = ctx->extern_alloc.alloc(wq->buf_size,
						  ctx->extern_alloc.data);
		if (!wq->buf) {
			free(wq);
			return NULL;
		}
	}

	wq_cmd_drv = &wq_cmd.drv_payload;
//	wq_resp_drv = &wq_resp.drv_payload;

	wq_cmd_drv->wq_buf_addr = (__u64) wq->buf;
	wq_cmd_drv->wqe = wq->wqe;

	ret = ibv_cmd_create_wq(context, attr, &wq->ibwq,
				&wq_cmd.ibv_cmd, sizeof(wq_cmd),
				&wq_resp.ibv_resp, sizeof(wq_resp));
	mana_dbg("ibv_cmd_create_wq  ret %d wqn %u\n", ret, wq->wqn);

	if (ret) {
		if (wq->buf && ctx->extern_alloc.free)
			ctx->extern_alloc.free(wq->buf, ctx->extern_alloc.data);
		free(wq);
		return NULL;
	}

	return &wq->ibwq;
}

int mana_destroy_wq(struct ibv_wq *ibwq)
{
	struct mana_wq *wq = container_of(ibwq, struct mana_wq, ibwq);
	struct mana_context *ctx = to_mctx(ibwq->context);

	mana_trace();

	if (wq->buf && ctx->extern_alloc.free)
		ctx->extern_alloc.free(wq->buf, ctx->extern_alloc.data);

	free(wq);

	return 0;
}

struct ibv_rwq_ind_table *mana_create_rwq_ind_table(struct ibv_context *context,
						    struct ibv_rwq_ind_table_init_attr *init_attr)
{
	int ret;
	struct mana_rwq_ind_table *ind_table;
	struct mana_create_rwq_ind_table_resp resp = {};
//	struct mana_ib_create_rwq_ind_table_resp *resp_drv;
	int i;

	mana_trace();

	ind_table = calloc(1, sizeof(*ind_table));
	if (!ind_table)
		return NULL;

//	resp_drv = &resp.drv_payload;
	ret = ibv_cmd_create_rwq_ind_table(context, init_attr,
					   &ind_table->ib_ind_table,
					   &resp.ibv_resp, sizeof(resp));
	if (ret) {
		free(ind_table);
		return NULL;
	}

	ind_table->ind_tbl_size = 1 << init_attr->log_ind_tbl_size;
	ind_table->ind_tbl = calloc(ind_table->ind_tbl_size,
				    sizeof(struct ibv_wq *));
	if (!ind_table->ind_tbl) {
		free(ind_table);
		return NULL;
	}
	for (i = 0; i < ind_table->ind_tbl_size; i++)
		ind_table->ind_tbl[i] = init_attr->ind_tbl[i];

	return &ind_table->ib_ind_table;
}

int mana_destroy_rwq_ind_table(struct ibv_rwq_ind_table *rwq_ind_table)
{
	struct mana_rwq_ind_table *ind_table =
		container_of(rwq_ind_table, struct mana_rwq_ind_table,
			     ib_ind_table);

	mana_trace();

	free(ind_table->ind_tbl);
	free(ind_table);

	return 0;
}
