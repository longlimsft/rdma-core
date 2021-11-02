/*
 * Copyright (c) 2021 Microsoft Corporation. All rights reserved.
 */

#ifndef _MANA_H_
#define _MANA_H_

#include "mana_dv.h"


#define mana_trace()	fprintf(stderr, "(rdma-core) %s\n", __func__)
#define mana_dbg(...)	fprintf(stderr, __VA_ARGS__)


#define MAX_SEND_BUFFERS_PER_QUEUE 256
#define COMP_ENTRY_SIZE 64
#define WQ_ENTRY_SIZE	32	// Is it the same for both SQ and RQ?

struct mana_context {
	struct verbs_context    ibv_ctx;
	struct manadv_ctx_allocators extern_alloc;
	void *db_page;
	uint32_t memory_key;    // TODO use memory registration
};

struct mana_rwq_ind_table {
	struct ibv_rwq_ind_table ib_ind_table;

	// used to track down wqid
	uint32_t ind_tbl_size;
	/* Each entry is a pointer to a Receive Work Queue */
	struct ibv_wq **ind_tbl;
};

struct mana_qp {
	struct verbs_qp ibqp;

	void *send_buf;
	uint32_t send_buf_size;
	int send_wqe_count;

	void *recv_buf;
	uint32_t recv_buf_size;
	int recv_wqe_count;

	uint32_t sqid;
	uint32_t tx_vp_offset;
};

struct mana_wq {
	struct ibv_wq ibwq;
	void *buf;
	uint32_t buf_size;
	uint32_t wqe;
	uint32_t sge;
	uint32_t wqn;

	uint32_t wqid;
};

struct mana_cq {
	struct ibv_cq ibcq;
	uint32_t cqe;
	void *buf;

	uint32_t cqid;
};

struct mana_device {
	struct verbs_device verbs_dev;
	int driver_abi_ver;
};

struct mana_pd {
	struct ibv_pd	ibv_pd;
	uint32_t	pdn;
	struct mana_pd	*mprotection_domain;
};

struct mana_parent_domain {
	struct mana_pd mpd;
	void *pd_context;
};

struct mana_context *to_mctx(struct ibv_context *ibctx);

int mana_query_device_ex(struct ibv_context *context,
			 const struct ibv_query_device_ex_input *input,
			 struct ibv_device_attr_ex *attr,
			 size_t attr_size);

int mana_query_port(struct ibv_context *context, uint8_t port,
		     struct ibv_port_attr *attr);

struct ibv_pd *mana_alloc_pd(struct ibv_context *context);
struct ibv_pd *mana_alloc_parent_domain(struct ibv_context *context,
					struct ibv_parent_domain_init_attr *attr);

int mana_free_pd(struct ibv_pd *pd);

struct ibv_cq *mana_create_cq(struct ibv_context *context, int cqe,
			      struct ibv_comp_channel *channel,
			      int comp_vector);

int mana_destroy_cq(struct ibv_cq *cq);

struct ibv_wq *mana_create_wq(struct ibv_context *context,
			      struct ibv_wq_init_attr *attr);

int mana_destroy_wq(struct ibv_wq *wq);
int mana_modify_wq(struct ibv_wq *ibwq, struct ibv_wq_attr *attr);

struct ibv_rwq_ind_table
*mana_create_rwq_ind_table(struct ibv_context *context,
			   struct ibv_rwq_ind_table_init_attr *init_attr);

int mana_destroy_rwq_ind_table(struct ibv_rwq_ind_table *rwq_ind_table);

struct ibv_qp *mana_create_qp(struct ibv_pd *pd,
			      struct ibv_qp_init_attr *attr);

struct ibv_qp *mana_create_qp_ex(struct ibv_context *context,
				 struct ibv_qp_init_attr_ex *attr);

int mana_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		   int attr_mask);

int mana_destroy_qp(struct ibv_qp *ibqp);

#endif
