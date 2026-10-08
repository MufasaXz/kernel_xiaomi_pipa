/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Glue between the downstream QSEECOM driver and mainline kernel interfaces.
 *
 * The driver was written against several msm-specific kernel APIs: the
 * downstream SCM driver, the QTEE shared memory bridge allocator, msm_bus
 * bandwidth voting and the arm64 dmac_* cache helpers. This header provides
 * minimal implementations of those on top of their mainline counterparts, so
 * that the driver itself needs as few changes as possible.
 */

#ifndef __QSEECOM_COMPAT_H
#define __QSEECOM_COMPAT_H

#include <linux/device.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/firmware/qcom/qcom_tzmem.h>
#include <linux/gfp.h>
#include <linux/interconnect.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/barrier.h>

/* soc/qcom/scm.h */

#define SCM_SVC_INFO			0x6
#define SCM_SVC_ES			0x10
#define SCM_SVC_MDTP			0x12
#define SCM_SVC_TZSCHEDULER		0xFC

#define SCM_SIP_FNID(s, c) (((((s) & 0xFF) << 8) | ((c) & 0xFF)) | 0x02000000)

#define MAX_SCM_ARGS			10
#define MAX_SCM_RETS			QCOM_SCM_RAW_RETS

#define SCM_ARGS_IMPL(num, a, b, c, d, e, f, g, h, i, j, ...) (\
			(((a) & 0x3) << 4) | \
			(((b) & 0x3) << 6) | \
			(((c) & 0x3) << 8) | \
			(((d) & 0x3) << 10) | \
			(((e) & 0x3) << 12) | \
			(((f) & 0x3) << 14) | \
			(((g) & 0x3) << 16) | \
			(((h) & 0x3) << 18) | \
			(((i) & 0x3) << 20) | \
			(((j) & 0x3) << 22) | \
			((num) & 0xf))

#define SCM_ARGS(...) SCM_ARGS_IMPL(__VA_ARGS__, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)

struct scm_desc {
	u32 arginfo;
	u64 args[MAX_SCM_ARGS];
	u64 ret[MAX_SCM_RETS];
};

/*
 * Downstream SMC IDs carry the owner in bits [29:24], the service in [15:8]
 * and the command in [7:0]. The SMC64 and standard-call bits are added by
 * the mainline SCM driver, which also retries -EBUSY on its own.
 */
static inline int scm_call2(u32 fn_id, struct scm_desc *desc)
{
	return qcom_scm_call_raw((fn_id >> 24) & 0x3f, (fn_id >> 8) & 0xff,
				 fn_id & 0xff, desc->arginfo, desc->args,
				 MAX_SCM_ARGS, desc->ret);
}

static inline int scm_call2_noretry(u32 fn_id, struct scm_desc *desc)
{
	return scm_call2(fn_id, desc);
}

/* soc/qcom/qtee_shmbridge.h */

/* SM8250 requires bridge-registered buffers; ordinary pages are insufficient. */
static struct qcom_tzmem_pool *qseecom_tzmem_pool;

static inline int qseecom_init_tzmem(struct device *dev)
{
	struct qcom_tzmem_pool_config config = {
		.initial_size = SZ_64K,
		.policy = QCOM_TZMEM_POLICY_ON_DEMAND,
		.max_size = SZ_16M,
	};

	qseecom_tzmem_pool = devm_qcom_tzmem_pool_new(dev, &config);
	return PTR_ERR_OR_ZERO(qseecom_tzmem_pool);
}
struct qtee_shm {
	phys_addr_t paddr;
	void *vaddr;
	size_t size;
};

static inline int qtee_shmbridge_allocate_shm(size_t size, struct qtee_shm *shm)
{
	void *va;

	size = PAGE_ALIGN(size);
	va = qcom_tzmem_alloc(qseecom_tzmem_pool, size, GFP_KERNEL);
	if (!va)
		return -ENOMEM;
	/* Legacy listener/SG descriptors still contain 32-bit physical pointers. */
	if (qcom_tzmem_to_phys(va) >= (1ULL << 32) - size) {
		qcom_tzmem_free(va);
		return -ERANGE;
	}

	shm->vaddr = va;
	shm->paddr = qcom_tzmem_to_phys(va);
	shm->size = size;
	return 0;
}

static inline void qtee_shmbridge_free_shm(struct qtee_shm *shm)
{
	if (shm->vaddr) {
		memzero_explicit(shm->vaddr, shm->size);
		qcom_tzmem_free(shm->vaddr);
	}
	memset(shm, 0, sizeof(*shm));
}

/* Internal TZ buffers come from coherent, bridge-registered qcom_tzmem. */

static inline void dmac_flush_range(const void *start, const void *end)
{
	/* Cache maintenance on the coherent alias is unnecessary. Order accesses. */
	dma_mb();
}

static inline void *qseecom_alloc_coherent(size_t size, dma_addr_t *paddr)
{
	void *buf;

	if (!size || size > SZ_16M)
		return NULL;
	buf = qcom_tzmem_alloc(qseecom_tzmem_pool, PAGE_ALIGN(size), GFP_KERNEL);
	if (!buf)
		return NULL;
	if (qcom_tzmem_to_phys(buf) >= (1ULL << 32) - PAGE_ALIGN(size)) {
		qcom_tzmem_free(buf);
		return NULL;
	}
	memset(buf, 0, PAGE_ALIGN(size));
	*paddr = qcom_tzmem_to_phys(buf);
	return buf;
}

static inline void qseecom_free_coherent(size_t size, void *buf)
{
	if (buf) {
		memzero_explicit(buf, PAGE_ALIGN(size));
		qcom_tzmem_free(buf);
	}
}

/* linux/msm-bus.h */

/*
 * msm_bus bandwidth votes are mapped to an optional interconnect path.
 * The usecase table is still taken from "qcom,msm-bus,vectors-KBps"
 * (<src dst ab ib> per usecase, one path only), and the path itself from
 * the standard "interconnects" property. Without one, voting is a no-op
 * and only the CE clocks are managed.
 */
struct qseecom_bus_client {
	struct icc_path *path;
	unsigned int num_cases;
	u32 *vectors;	/* num_cases * { ab, ib } in KBps */
};

static struct qseecom_bus_client qseecom_bus_client;

static inline uint32_t msm_bus_scale_register_client(struct device *dev)
{
	struct qseecom_bus_client *bc = &qseecom_bus_client;
	struct device_node *np = dev->of_node;
	u32 num_cases = 0;
	int n, i;

	bc->path = devm_of_icc_get(dev, NULL);
	if (IS_ERR(bc->path)) {
		dev_err(dev, "failed to get interconnect path: %ld\n",
			PTR_ERR(bc->path));
		bc->path = NULL;
		return 0;
	}

	of_property_read_u32(np, "qcom,msm-bus,num-cases", &num_cases);
	n = of_property_count_u32_elems(np, "qcom,msm-bus,vectors-KBps");
	if (bc->path && (n <= 0 || n != num_cases * 4)) {
		dev_err(dev, "invalid qcom,msm-bus,vectors-KBps\n");
		return 0;
	}

	if (bc->path) {
		u32 *raw = kcalloc(n, sizeof(*raw), GFP_KERNEL);

		bc->vectors = kcalloc(num_cases * 2, sizeof(*bc->vectors),
				      GFP_KERNEL);
		if (!raw || !bc->vectors ||
		    of_property_read_u32_array(np, "qcom,msm-bus,vectors-KBps",
					       raw, n)) {
			kfree(raw);
			kfree(bc->vectors);
			bc->vectors = NULL;
			return 0;
		}
		for (i = 0; i < num_cases; i++) {
			bc->vectors[i * 2] = raw[i * 4 + 2];
			bc->vectors[i * 2 + 1] = raw[i * 4 + 3];
		}
		kfree(raw);
		bc->num_cases = num_cases;
	}

	/* Any non-zero handle; there is only one client. */
	return 1;
}

#define msm_bus_cl_get_pdata(pdev)	(&(pdev)->dev)

static inline int msm_bus_scale_client_update_request(uint32_t client,
						      unsigned int index)
{
	struct qseecom_bus_client *bc = &qseecom_bus_client;

	if (!bc->path)
		return 0;
	if (index >= bc->num_cases)
		return -EINVAL;

	return icc_set_bw(bc->path, kBps_to_icc(bc->vectors[index * 2]),
			  kBps_to_icc(bc->vectors[index * 2 + 1]));
}

static inline void msm_bus_scale_unregister_client(uint32_t client)
{
	struct qseecom_bus_client *bc = &qseecom_bus_client;

	kfree(bc->vectors);
	bc->vectors = NULL;
	bc->num_cases = 0;
	/* The path itself is device managed. */
}

#endif /* __QSEECOM_COMPAT_H */
