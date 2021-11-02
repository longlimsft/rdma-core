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

DECLARE_DRV_CMD(mana_create_qp, IB_USER_VERBS_CMD_CREATE_QP,
		mana_ib_create_qp, mana_ib_create_qp_resp);

DECLARE_DRV_CMD(mana_create_qp_ex, IB_USER_VERBS_EX_CMD_CREATE_QP,
		mana_ib_create_qp_rss, mana_ib_create_qp_rss_resp);

static struct ibv_qp *mana_create_qp_raw(struct ibv_pd *ibpd,
				  struct ibv_qp_init_attr *attr)
{
	int ret;
	struct mana_cq *cq;
	struct mana_qp *qp;
	struct mana_pd *pd = container_of(ibpd, struct mana_pd, ibv_pd);
	struct mana_parent_domain *mpd;
	uint32_t port;

	struct mana_create_qp		qp_cmd = {};
	struct mana_create_qp_resp	qp_resp = {};
	struct mana_ib_create_qp	*qp_cmd_drv;
	struct mana_ib_create_qp_resp	*qp_resp_drv;

	struct mana_context *ctx = to_mctx(ibpd->context);

	mana_trace();

	// This is a DPDK QP, pd is a parent domain with port number
	if (!pd->mprotection_domain)
		return NULL;

	mpd = container_of(pd, struct mana_parent_domain, mpd);
	port = (uint32_t)(uint64_t) mpd->pd_context;

	cq = container_of(attr->send_cq, struct mana_cq, ibcq);

	if (attr->cap.max_send_wr > MAX_SEND_BUFFERS_PER_QUEUE) {
		mana_dbg("max_send_wr %d exceeds MAX_SEND_BUFFERS_PER_QUEUE\n",
			 attr->cap.max_send_wr);
		return NULL;
	}

	qp = calloc(1, sizeof(*qp));
	if (!qp)
		return NULL;

	// allocate buffer for send_buffer only as we know this is in DPDK
	qp->send_buf_size = attr->cap.max_send_wr * WQ_ENTRY_SIZE;
	if (ctx->extern_alloc.alloc)
		qp->send_buf = ctx->extern_alloc.alloc(qp->send_buf_size,
						       ctx->extern_alloc.data);

	if (!qp->send_buf) {
		free(qp);
		return NULL;
	}

	qp_cmd_drv = &qp_cmd.drv_payload;
	qp_resp_drv = &qp_resp.drv_payload;

	qp_cmd_drv->sq_buf_addr = (__u64) qp->send_buf;
	qp_cmd_drv->sq_wqe_count = attr->cap.max_send_wr;
	qp_cmd_drv->port = port;

	ret = ibv_cmd_create_qp(ibpd, &qp->ibqp.qp, attr, &qp_cmd.ibv_cmd,
				sizeof(qp_cmd), &qp_resp.ibv_resp,
				sizeof(qp_resp));
	if (ret) {
		mana_dbg("ibv_cmd_create_qp failed ret %d\n", ret);
		if (ctx->extern_alloc.free)
			ctx->extern_alloc.free(qp->send_buf,
					       ctx->extern_alloc.data);
		free(qp);
		return NULL;
	}

	qp->sqid = qp_resp_drv->sqid;
	qp->tx_vp_offset = qp_resp_drv->tx_vp_offset;
	qp->send_wqe_count = qp_cmd_drv->sq_wqe_count;

	cq->cqid = qp_resp_drv->cqid;


	return &qp->ibqp.qp;
}

struct ibv_qp *mana_create_qp(struct ibv_pd *ibpd,
			      struct ibv_qp_init_attr *attr)
{
	switch (attr->qp_type) {
	case IBV_QPT_RAW_PACKET:
		return mana_create_qp_raw(ibpd, attr);

	default:
		mana_dbg("QP type %u\n", attr->qp_type);
		// Handle RC, UC and UD
	}

	return NULL;
}

int mana_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		   int attr_mask)
{
	mana_trace();
	return 0;
}

int mana_destroy_qp(struct ibv_qp *ibqp)
{
	int ret;
	struct mana_qp *qp = container_of(ibqp, struct mana_qp, ibqp.qp);
	struct mana_context *ctx = to_mctx(ibqp->context);

	mana_trace();

	ret = ibv_cmd_destroy_qp(ibqp);
	if (ret) {
		mana_dbg("ibv_cmd_create_qp failed ret %d\n", ret);
		return ret;
	}

	if (qp->send_buf) {
		if (ctx->extern_alloc.free)
			ctx->extern_alloc.free(qp->send_buf,
					       ctx->extern_alloc.data);

		// TODO free QP buffer not allocated through extern_alloc
	}
	free(qp);

	return 0;
}

static struct ibv_qp *mana_create_qp_ex_raw(struct ibv_context *context,
					    struct ibv_qp_init_attr_ex *attr)
{
	struct mana_create_qp_ex	cmd = {};
	struct mana_ib_create_qp_rss	*cmd_drv;
	struct mana_create_qp_ex_resp	resp = {};
	struct mana_ib_create_qp_rss_resp	*cmd_resp;
	struct mana_qp *qp;
	struct mana_pd *pd = container_of(attr->pd, struct mana_pd, ibv_pd);
	struct mana_parent_domain *mpd;
	uint32_t port;
	int ret;

	mana_trace();

	cmd_drv = &cmd.drv_payload;
	cmd_resp = &resp.drv_payload;

	// For DPDK, pd is a parent domain with port number
	if (!pd->mprotection_domain)
		return NULL;

	mpd = container_of(pd, struct mana_parent_domain, mpd);
	port = (uint32_t)(uint64_t) mpd->pd_context;

	qp = calloc(1, sizeof(*qp));
	if (!qp)
		return NULL;

	cmd_drv->rx_hash_fields_mask = attr->rx_hash_conf.rx_hash_fields_mask;
	cmd_drv->rx_hash_function = attr->rx_hash_conf.rx_hash_function;
	memcpy(cmd_drv->rx_hash_key, attr->rx_hash_conf.rx_hash_key, 40);
	cmd_drv->port = port;

	ret = ibv_cmd_create_qp_ex2(context, &qp->ibqp, attr,
				    &cmd.ibv_cmd, sizeof(cmd),
				    &resp.ibv_resp, sizeof(resp));
	if (ret) {
		mana_dbg("ibv_cmd_create_qp_ex ret=%d\n", ret);
		free(qp);
		return NULL;
	}

	if (attr->rwq_ind_tbl) {
		struct mana_rwq_ind_table *ind_table =
			container_of(attr->rwq_ind_tbl,
				     struct mana_rwq_ind_table, ib_ind_table);
		for (int i = 0; i < ind_table->ind_tbl_size; i++) {
			struct mana_wq *wq =
				container_of(ind_table->ind_tbl[i],
					     struct mana_wq, ibwq);
			struct mana_cq *cq =
				container_of(wq->ibwq.cq,
					     struct mana_cq, ibcq);
			wq->wqid = cmd_resp->entries[i].wqid;
			cq->cqid = cmd_resp->entries[i].cqid;
		}
	}

	return &qp->ibqp.qp;
}

struct ibv_qp *mana_create_qp_ex(struct ibv_context *context,
				 struct ibv_qp_init_attr_ex *attr)
{
	switch (attr->qp_type) {
	case IBV_QPT_RAW_PACKET:
		return mana_create_qp_ex_raw(context, attr);
	default:
		// TO implement creating RC, UC and UD QPs
		mana_dbg("QP type %u\n", attr->qp_type);
	}

	return NULL;
}
