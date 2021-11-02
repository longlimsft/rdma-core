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

DECLARE_DRV_CMD(mana_alloc_ucontext, IB_USER_VERBS_CMD_GET_CONTEXT,
		empty, mana_ib_alloc_ucontext_resp);

DECLARE_DRV_CMD(mana_alloc_pd, IB_USER_VERBS_CMD_ALLOC_PD,
		empty, mana_ib_alloc_pd_resp);

DECLARE_DRV_CMD(mana_create_cq, IB_USER_VERBS_CMD_CREATE_CQ,
		mana_ib_create_cq, mana_ib_create_cq_resp);

static const struct verbs_match_ent hca_table[] = {
	VERBS_DRIVER_ID(RDMA_DRIVER_MANA),
	{},
};

struct mana_context *to_mctx(struct ibv_context *ibctx)
{
	return container_of(ibctx, struct mana_context, ibv_ctx.context);
}

int mana_query_device_ex(struct ibv_context *context,
			 const struct ibv_query_device_ex_input *input,
			 struct ibv_device_attr_ex *attr,
			 size_t attr_size)
{
	struct ib_uverbs_ex_query_device_resp resp;
	size_t resp_size = sizeof(resp);
	int ret;

	mana_trace();

	ret = ibv_cmd_query_device_any(context, input, attr, attr_size,
				       &resp, &resp_size);

	mana_dbg("device attr max_qp %d max_qp_wr %d max_cqe %d\n",
		 attr->orig_attr.max_qp, attr->orig_attr.max_qp_wr,
		 attr->orig_attr.max_cqe);

	return ret;
}

int mana_query_port(struct ibv_context *context, uint8_t port,
		    struct ibv_port_attr *attr)
{
	mana_trace();
	attr->state = IBV_PORT_ACTIVE;
	attr->link_layer = IBV_LINK_LAYER_ETHERNET;
	return 0;
}

struct ibv_pd *mana_alloc_pd(struct ibv_context *context)
{

	struct ibv_alloc_pd       cmd;
	struct mana_alloc_pd_resp resp;
	struct mana_pd	   *pd;

	mana_trace();

	pd = calloc(1, sizeof(*pd));
	if (!pd)
		return NULL;

	if (ibv_cmd_alloc_pd(context, &pd->ibv_pd, &cmd, sizeof(cmd),
			     &resp.ibv_resp, sizeof(resp))) {
		free(pd);
		return NULL;
	}

	pd->pdn = resp.pdn;
	mana_dbg("pdn=%d\n", pd->pdn);

	return &pd->ibv_pd;
}

struct ibv_pd *mana_alloc_parent_domain(struct ibv_context *context,
					struct ibv_parent_domain_init_attr *attr)
{
	struct mana_parent_domain *mparent_domain;

	if (ibv_check_alloc_parent_domain(attr))
		return NULL;

	if (!check_comp_mask(attr->comp_mask,
			     IBV_PARENT_DOMAIN_INIT_ATTR_PD_CONTEXT)) {
		errno = EINVAL;
		return NULL;
	}

	mparent_domain = calloc(1, sizeof(*mparent_domain));
	if (!mparent_domain) {
		errno = ENOMEM;
		return NULL;
	}

	mparent_domain->mpd.mprotection_domain =
		container_of(attr->pd, struct mana_pd, ibv_pd);
	ibv_initialize_parent_domain(&mparent_domain->mpd.ibv_pd, attr->pd);

	if (attr->comp_mask & IBV_PARENT_DOMAIN_INIT_ATTR_PD_CONTEXT)
		mparent_domain->pd_context = attr->pd_context;

	return &mparent_domain->mpd.ibv_pd;
}

int mana_free_pd(struct ibv_pd *ibpd)
{
	int ret;
	struct mana_pd *pd = container_of(ibpd, struct mana_pd, ibv_pd);

	mana_trace();

	if (pd->mprotection_domain) {
		struct mana_parent_domain *parent_domain =
			container_of(pd, struct mana_parent_domain, mpd);
		free(parent_domain);
		return 0;
	}

	ret = ibv_cmd_dealloc_pd(ibpd);
	if (ret)
		return ret;

	free(pd);

	return 0;
}

struct ibv_cq *mana_create_cq(struct ibv_context *context, int cqe,
			      struct ibv_comp_channel *channel,
			      int comp_vector)
{
	struct mana_context *ctx = to_mctx(context);
	struct mana_cq *cq;
	struct mana_create_cq	   cmd = {};
	struct mana_create_cq_resp      resp = {};
	struct mana_ib_create_cq	*cmd_drv;
//	struct mana_ib_create_cq_resp   *resp_drv;
	int cq_size;
	int ret;

	mana_trace();

	if (cqe > MAX_SEND_BUFFERS_PER_QUEUE) {
		errno = -ENOMEM;
		return NULL;
	}

	cq = calloc(1, sizeof(*cq));
	if (!cq)
		return NULL;

	cq_size = cqe * COMP_ENTRY_SIZE;
	cq_size = roundup_pow_of_two(cq_size);

	if (ctx->extern_alloc.alloc) {
		cq->buf = ctx->extern_alloc.alloc(cq_size,
						  ctx->extern_alloc.data);
		cq->cqe = cqe;
	} else {
		// TODO support create CQ in the driver for non DPDK case
		return NULL;
	}

	cmd_drv = &cmd.drv_payload;
  //      resp_drv = &resp.drv_payload;

	cmd_drv->buf_addr = (__u64) cq->buf;
	cmd_drv->cqe = cq->cqe;

	ret = ibv_cmd_create_cq(context, cq->cqe, NULL, 0, &cq->ibcq,
				&cmd.ibv_cmd, sizeof(cmd),
				&resp.ibv_resp, sizeof(resp));

	if (ret) {
		mana_dbg("ibv_cmd_create_cq ret=%d\n", ret);
		if (ctx->extern_alloc.free)
			ctx->extern_alloc.free(cq->buf, ctx->extern_alloc.data);

		free(cq);
		return NULL;
	}

	return &cq->ibcq;
}

int mana_destroy_cq(struct ibv_cq *ibcq)
{
	int ret;
	struct mana_cq *cq = container_of(ibcq, struct mana_cq, ibcq);
	struct mana_context *ctx = to_mctx(ibcq->context);

	mana_trace();

	ret = ibv_cmd_destroy_cq(ibcq);
	if (ret)
		return ret;

	if (ctx->extern_alloc.alloc)
		ctx->extern_alloc.free(cq->buf, ctx->extern_alloc.data);

	// TODO free CQ when extern_alloc is not used

	free(cq);

	return ret;
}

static void mana_free_context(struct ibv_context *ibctx)
{
	struct mana_context *context = to_mctx(ibctx);

	mana_trace();

	munmap(context->db_page, 4096);
	verbs_uninit_context(&context->ibv_ctx);
	free(context);
}

static const struct verbs_context_ops mana_ctx_ops = {
	.query_device_ex = mana_query_device_ex,
	.query_port    = mana_query_port,
	.alloc_pd      = mana_alloc_pd,
	.alloc_parent_domain = mana_alloc_parent_domain,
	.dealloc_pd    = mana_free_pd,
	.create_cq     = mana_create_cq,
	.destroy_cq    = mana_destroy_cq,
	.create_wq = mana_create_wq,
	.destroy_wq = mana_destroy_wq,
	.modify_wq = mana_modify_wq,
	.create_rwq_ind_table = mana_create_rwq_ind_table,
	.destroy_rwq_ind_table = mana_destroy_rwq_ind_table,
	.create_qp     = mana_create_qp,
	.create_qp_ex = mana_create_qp_ex,
	.modify_qp     = mana_modify_qp,
	.destroy_qp    = mana_destroy_qp,
	.free_context = mana_free_context,
};

static struct verbs_device *mana_device_alloc(struct verbs_sysfs_dev *sysfs_dev)
{
	struct mana_device *dev;

	mana_trace();

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return NULL;

	dev->driver_abi_ver = sysfs_dev->abi_ver;

	return &dev->verbs_dev;
}

static void mana_uninit_device(struct verbs_device *verbs_device)
{
	struct mana_device *dev =
		container_of(verbs_device, struct mana_device, verbs_dev);

	mana_trace();

	free(dev);
}

static struct verbs_context *mana_alloc_context(struct ibv_device *ibdev,
						int cmd_fd,
						void *private_data)
{
	int ret;
	struct mana_context *context;
	struct mana_alloc_ucontext_resp resp;
	struct ibv_get_context	  cmd;

	context = verbs_init_and_alloc_context(ibdev, cmd_fd, context,
					       ibv_ctx, RDMA_DRIVER_MANA);
	if (!context)
		return NULL;

	ret = ibv_cmd_get_context(&context->ibv_ctx, &cmd, sizeof(cmd),
				  &resp.ibv_resp, sizeof(resp));
	if (ret) {
		free(context);
		return NULL;
	}

	mana_dbg("ret=%d resp memory_key %d\n", ret, resp.memory_key);
	context->memory_key = resp.memory_key;

	verbs_set_ops(&context->ibv_ctx, &mana_ctx_ops);

	// need to figure out how big UAR0 is
	// TODO need to move db_page and memory key to PD
	context->db_page = mmap(NULL, 4096, PROT_WRITE, MAP_SHARED,
				context->ibv_ctx.context.cmd_fd, 0);
	if (context->db_page == MAP_FAILED) {
		free(context);
		return NULL;
	}
	mana_dbg("context->db_page=%p\n", context->db_page);

	return &context->ibv_ctx;
}

static struct verbs_context *mana_import_context(struct ibv_device *ibdev,
						 int cmd_fd)
{
	mana_trace();
	return NULL;
}

static const struct verbs_device_ops mana_dev_ops = {
	.name = "mana",
	.match_min_abi_version = 1,
	.match_max_abi_version = 1,
	.match_table = hca_table,
	.alloc_device = mana_device_alloc,
	.uninit_device = mana_uninit_device,
	.alloc_context = mana_alloc_context,
	.import_context = mana_import_context,
};

PROVIDER_DRIVER(mana, mana_dev_ops);
