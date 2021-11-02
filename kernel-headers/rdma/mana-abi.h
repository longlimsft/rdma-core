/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR Linux-OpenIB) */
/*
 * Copyright (c) 2021, Microsoft Corporation. All rights reserved.
 */

#ifndef MANA_ABI_USER_H
#define MANA_ABI_USER_H

#include <linux/types.h>

struct mana_ib_alloc_ucontext_resp {
	__u32	memory_key;
	__u32	reserved;
};

struct mana_ib_alloc_pd_resp {
	__u32   pdn;
	__u32	reserved;
};

struct mana_ib_create_cq {
	__aligned_u64 buf_addr;
	__u32 cqe;
};

struct mana_ib_create_cq_resp {
	__u32	reserved1;
	__u32	reserved2;
};

struct mana_ib_create_qp {
	__aligned_u64 sq_buf_addr;
	__u32	sq_wqe_count;
	__aligned_u64 rq_buf_addr;
	__u32	rq_wqe_count;
	__u32	port;
};

struct mana_ib_create_qp_resp {
	__u32 sqid;
	__u32 cqid;
	__u32 tx_vp_offset;
};

struct mana_ib_create_wq {
	__aligned_u64 wq_buf_addr;
	__u32	wqe;
};

struct mana_ib_create_wq_resp {
	__u32	reserved1;
	__u32	reserved2;
};

struct mana_ib_create_rwq_ind_table_resp {
	__u32 reserved1;
	__u32 reserved2;
};

struct mana_ib_create_qp_rss {
	__aligned_u64 rx_hash_fields_mask;
	__u8    rx_hash_function;
	__u8    reserved[7];
	__u8    rx_hash_key[40];
	__u32   comp_mask;
	__u32	port;
};

struct rss_resp_entry {
	__u32	cqid;
	__u32	wqid;
};

// TODO assuming we can only handle 64 RSS
struct mana_ib_create_qp_rss_resp {
	__aligned_u64 num_entries;
	struct rss_resp_entry entries[64];
};

#endif
