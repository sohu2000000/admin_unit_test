/*
 * admin_unit_test.c -- virtio admin command unit test module
 *
 * Copyright (C) 2001,2024 Feng Liu
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 * $Id: admin_unit_test.c,v 0.10 2023/09/25 07:02:43 Feng Liu $
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <linux/virtio.h>
#include <linux/virtio_pci.h>
#include <linux/pci.h>

/* Increment MAX_OPCODE to next value when new opcode is added */
#define VIRTIO_ADMIN_MAX_CMD_OPCODE			0x11

static struct proc_dir_entry *admin_unit_dir = NULL;

MODULE_AUTHOR("Feng Liu <feliu@nvidia.com>");
MODULE_LICENSE("Dual BSD/GPL");

#define DEP_MOD_NUM	(2)

enum admin_cmd_files {
	ADMIN_CMD_LIST_QUERY,
	ADMIN_CMD_MAX
};

struct dev_mgr_s {
	struct pci_dev *pf_pdev;
	struct pci_dev *vf0_pdev;
	struct pci_dev *vf1_pdev;

	int vf0_ctx_sz;
	u8 *vf0_ctx;
	int vf0_ctx_left;
	u8 *vf0_ctx_pos;

	int vf1_ctx_sz;
	u8 *vf1_ctx;
	int vf1_ctx_left;
	u8 *vf1_ctx_pos;

	u8 *op_list_buf;
	int op_list_size;
	u8 *dev_mode;
	int dev_mode_sz;
	u8 *new_dev_mode;
	int new_dev_mode_sz;
	u8 *ctx_sz_res;
	int ctx_sz_res_sz;
	u8 *ctx_sprt_flds;
	int ctx_sprt_flds_sz;
};

struct __packed virtio_admin_cmd_dev_ctx_supported_field {
	__le16 type;
	__u8 reserved[2];
	__le32 length;
};

struct dev_mgr_s g_dev_mgr;

static int admin_unit_cmd_proc_show(struct seq_file *m, void *v)
{
	pr_err("godfeng %s:%d\n",__func__, __LINE__);
	return 0;
}

static int admin_unit_cmd_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, admin_unit_cmd_proc_show, NULL);
}

static int admin_unit_cmd_list_query(struct pci_dev *pdev,
				     u8 *buf, int buf_size)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct virtio_admin_cmd cmd = {};
	struct scatterlist out_sg;

	if (!virtio_dev)
		return -ENOTCONN;

	dev_info(&pdev->dev, "Vf pdev(%s) domain %d bus %#x devfn %#x",
		pci_name(pdev),
		pci_domain_nr(pdev->bus),
		pdev->bus->number, pdev->devfn);
	dev_info(&virtio_dev->dev, "Find out PF(%s)\n", dev_name(&virtio_dev->dev));

	sg_init_one(&out_sg, buf, buf_size);
	cmd.opcode = VIRTIO_ADMIN_CMD_LIST_QUERY;
	cmd.group_type = VIRTIO_ADMIN_GROUP_TYPE_SRIOV;
	cmd.result_sg = &out_sg;

	return vp_modern_admin_cmd_exec(virtio_dev, &cmd);
}

static int admin_unit_cmd_list_query_proc(void)
{
	int i, ret = 0;

	g_dev_mgr.op_list_size =
		DIV_ROUND_UP(VIRTIO_ADMIN_MAX_CMD_OPCODE, 64) * 8;
	g_dev_mgr.op_list_buf =
		kzalloc(g_dev_mgr.op_list_size, GFP_KERNEL);
	if (!g_dev_mgr.op_list_buf) {
		pr_err("Can not alloc op list buffer\n");
		return -ENOMEM;
	}

	pr_err("%s:%d: exec list_query \n",__func__, __LINE__);
	ret = admin_unit_cmd_list_query(g_dev_mgr.vf0_pdev,
					g_dev_mgr.op_list_buf,
					g_dev_mgr.op_list_size);
	if (ret)
		pr_err("Failed to run virtiovf_cmd_list_query ret(%d)\n",
			ret);

	pr_err("Dump out oplist \n");
	for (i = 0; i < g_dev_mgr.op_list_size; i++) {
		pr_err("op_list[%d] = %#x\n",
			i, g_dev_mgr.op_list_buf[i]);
	}

	return ret;
}

static int admin_unit_cmd_dev_mode_get(struct pci_dev *pdev,
				       u8 *buf, int buf_size)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct virtio_admin_cmd cmd = {};
	struct scatterlist out_sg;

	if (!virtio_dev)
		return -ENOTCONN;

	if (!pdev->is_virtfn)
		pr_err("pdev should be a Virtual Function.\n");

	dev_info(&pdev->dev, "Vf pdev(%s) domain %d bus %#x devfn %#x",
		pci_name(pdev),
		pci_domain_nr(pdev->bus),
		pdev->bus->number, pdev->devfn);

	dev_info(&virtio_dev->dev, "Use PF(%s) send cmd for VF id (%d)\n",
		dev_name(&virtio_dev->dev),
		pci_iov_vf_id(pdev));

	sg_init_one(&out_sg, buf, buf_size);
	cmd.opcode = VIRTIO_ADMIN_CMD_DEV_MODE_GET;
	cmd.group_type = VIRTIO_ADMIN_GROUP_TYPE_SRIOV;
	cmd.group_member_id = pci_iov_vf_id(pdev) + 1;
	cmd.result_sg = &out_sg;

	return vp_modern_admin_cmd_exec(virtio_dev, &cmd);
}

static int admin_unit_cmd_dev_mode_get_proc(uint8_t vf_idx)
{
	struct virtio_admin_cmd_dev_mode *dev_mode;
	struct pci_dev *vf_pdev;
	int ret = 0;

	g_dev_mgr.dev_mode_sz = sizeof(struct virtio_admin_cmd_dev_mode);
	if(!g_dev_mgr.dev_mode) {
		g_dev_mgr.dev_mode =
			kzalloc(g_dev_mgr.dev_mode_sz, GFP_KERNEL);
		if (!g_dev_mgr.dev_mode) {
			pr_err("Can not alloc dev_mode buffer\n");
			return -ENOMEM;
		}
	}

	pr_err("%s:%d: exec dev_mode_get \n",__func__, __LINE__);

	vf_pdev = vf_idx == 0 ? g_dev_mgr.vf0_pdev : g_dev_mgr.vf1_pdev;
	ret = admin_unit_cmd_dev_mode_get(vf_pdev,
					  g_dev_mgr.dev_mode,
					  g_dev_mgr.dev_mode_sz);
	if (ret)
		pr_err("Failed to run virtiovf_cmd_list_query ret(%d)\n",
			ret);

	dev_mode = (struct virtio_admin_cmd_dev_mode *)g_dev_mgr.dev_mode;
	pr_err("Dump out dev_mode \n");
	pr_err("dev_mode = %#x\n", dev_mode->mode);

	return ret;
}

static int admin_unit_cmd_dev_mode_set(struct pci_dev *pdev, uint8_t mode)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct virtio_admin_cmd_dev_mode *in;
	struct virtio_admin_cmd cmd = {};
	struct scatterlist in_sg;
	int ret;

	if (!virtio_dev)
		return -ENOTCONN;

	if (!pdev->is_virtfn)
		pr_err("pdev should be a Virtual Function.\n");

	dev_info(&pdev->dev, "Vf pdev(%s) domain %d bus %#x devfn %#x",
		pci_name(pdev),
		pci_domain_nr(pdev->bus),
		pdev->bus->number, pdev->devfn);

	dev_info(&virtio_dev->dev, "Use PF(%s) send cmd for VF id (%d)\n",
		dev_name(&virtio_dev->dev),
		pci_iov_vf_id(pdev));

	in = kzalloc(sizeof(*in), GFP_KERNEL);
	if (!in)
		return -ENOMEM;
	in->mode = mode;
	sg_init_one(&in_sg, in, sizeof(*in));
	cmd.opcode = VIRTIO_ADMIN_CMD_DEV_MODE_SET;
	cmd.group_type = VIRTIO_ADMIN_GROUP_TYPE_SRIOV;
	cmd.group_member_id = pci_iov_vf_id(pdev) + 1;
	cmd.data_sg = &in_sg;

	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);
	kfree(in);
	return ret;
}

static int admin_unit_cmd_dev_mode_set_proc(uint8_t vf_idx, uint8_t mode)
{
	struct virtio_admin_cmd_dev_mode *dev_mode;
	struct pci_dev *vf_pdev;
	int ret = 0;

	g_dev_mgr.dev_mode_sz = sizeof(struct virtio_admin_cmd_dev_mode);
	if (!g_dev_mgr.dev_mode) {
		g_dev_mgr.dev_mode =
			kzalloc(g_dev_mgr.dev_mode_sz, GFP_KERNEL);
		if (!g_dev_mgr.dev_mode) {
			pr_err("Can not alloc dev_mode buffer\n");
			return -ENOMEM;
		}
	}

	pr_err("%s:%d: exec dev_mode_get \n",__func__, __LINE__);

	dev_mode = (struct virtio_admin_cmd_dev_mode *)g_dev_mgr.dev_mode;
	dev_mode->mode = mode;
	vf_pdev = vf_idx == 0 ? g_dev_mgr.vf0_pdev : g_dev_mgr.vf1_pdev;

	ret = admin_unit_cmd_dev_mode_set(vf_pdev, dev_mode->mode);
	if (ret)
		pr_err("Failed to run virtiovf_cmd_list_query ret(%d)\n",
			ret);

	pr_err("Dump out ret = %#x\n", ret);
	return ret;
}


static int
admin_unit_cmd_dev_ctx_sz_get(struct pci_dev *pdev, uint8_t freeze_mode,
			      u8 *buf, int buf_size)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct virtio_admin_cmd_dev_ctx_size_get_data *in;
	struct scatterlist in_sg, out_sg;
	struct virtio_admin_cmd cmd = {};
	int ret;

	if (!virtio_dev)
		return -ENOTCONN;

	if (!pdev->is_virtfn)
		pr_err("pdev should be a Virtual Function.\n");

	dev_info(&pdev->dev, "Vf pdev(%s) domain %d bus %#x devfn %#x",
		pci_name(pdev),
		pci_domain_nr(pdev->bus),
		pdev->bus->number, pdev->devfn);

	dev_info(&virtio_dev->dev, "Use PF(%s) send cmd for VF id (%d)\n",
		dev_name(&virtio_dev->dev),
		pci_iov_vf_id(pdev));

	in = kzalloc(sizeof(*in), GFP_KERNEL);
	if (!in)
		return -ENOMEM;
	in->freeze_mode = freeze_mode;

	sg_init_one(&in_sg, in, sizeof(*in));
	sg_init_one(&out_sg, buf, buf_size);

	cmd.opcode = VIRTIO_ADMIN_CMD_DEV_CTX_SIZE_GET;
	cmd.group_type = VIRTIO_ADMIN_GROUP_TYPE_SRIOV;
	cmd.group_member_id = pci_iov_vf_id(pdev) + 1;
	cmd.data_sg = &in_sg;
	cmd.result_sg = &out_sg;

	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);
	kfree(in);
	return ret;
}

static int
admin_unit_cmd_dev_ctx_sz_get_proc(uint8_t vf_idx, uint8_t freeze_mode)
{
	struct virtio_admin_cmd_dev_ctx_size_get_result *res;
	struct pci_dev *vf_pdev;
	int ret = 0;

	g_dev_mgr.ctx_sz_res_sz = sizeof(struct virtio_admin_cmd_dev_ctx_size_get_result);
	if(!g_dev_mgr.ctx_sz_res) {
		g_dev_mgr.ctx_sz_res =
			kzalloc(g_dev_mgr.ctx_sz_res_sz, GFP_KERNEL);
		if (!g_dev_mgr.ctx_sz_res) {
			pr_err("Can not alloc memory \n");
			return -ENOMEM;
		}
	}

	pr_err("%s:%d: exec dev_ctx_sz_get \n",__func__, __LINE__);

	vf_pdev = vf_idx == 0 ? g_dev_mgr.vf0_pdev : g_dev_mgr.vf1_pdev;
	ret = admin_unit_cmd_dev_ctx_sz_get(vf_pdev, freeze_mode,
					    g_dev_mgr.ctx_sz_res,
					    g_dev_mgr.ctx_sz_res_sz);
	if (ret)
		pr_err("Failed to run admin_unit_cmd_dev_ctx_sz_get ret(%d)\n",
			ret);

	res = (struct virtio_admin_cmd_dev_ctx_size_get_result *)g_dev_mgr.ctx_sz_res;
	if (0 == vf_idx) {
		if (!g_dev_mgr.vf0_ctx)
			g_dev_mgr.vf0_ctx_sz = res->size;
		g_dev_mgr.vf0_ctx_left = res->size;
	}
	else {
		if (!g_dev_mgr.vf1_ctx)
			g_dev_mgr.vf1_ctx_sz = res->size;
		g_dev_mgr.vf1_ctx_left = res->size;
	}

	pr_err("Dump out ret %d \n", ret);
	pr_err(" ctx size = %#llx \n", res->size);
	return ret;
}

static int
admin_unit_cmd_dev_ctx_rd(struct pci_dev *pdev, u8 *buf, int buf_size,
			  int *rd_sz, int *remaining_sz)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct virtio_admin_cmd_dev_ctx_rd_result *res = NULL;
	struct virtio_admin_cmd cmd = {};
	struct scatterlist sgs[2];
	unsigned int sg_buf_size;
	int ret = 0;

	if (!virtio_dev)
		return -ENOTCONN;

	if (!pdev->is_virtfn)
		pr_err("pdev should be a Virtual Function.\n");

	dev_info(&pdev->dev, "Vf pdev(%s) domain %d bus %#x devfn %#x",
		pci_name(pdev),
		pci_domain_nr(pdev->bus),
		pdev->bus->number, pdev->devfn);

	dev_info(&virtio_dev->dev, "Use PF(%s) send cmd for VF id (%d)\n",
		dev_name(&virtio_dev->dev),
		pci_iov_vf_id(pdev));

	res = kzalloc(sizeof(struct virtio_admin_cmd_dev_ctx_rd_result),
		      GFP_KERNEL);
	if (!res) {
		dev_err(&virtio_dev->dev,
			"Failed to alloc result header size(%lu)\n",
			sizeof(struct virtio_admin_cmd_dev_ctx_rd_result));
		return -ENOMEM;
	}

	/* prepare sgs */
	sg_init_table(sgs, 2);

	sg_buf_size = sizeof(struct virtio_admin_cmd_dev_ctx_rd_result);
	sg_set_buf(&sgs[0], res, sg_buf_size);

	sg_buf_size = buf_size;
	sg_set_buf(&sgs[1], buf, sg_buf_size);

	cmd.opcode = VIRTIO_ADMIN_CMD_DEV_CTX_READ;
	cmd.group_type = VIRTIO_ADMIN_GROUP_TYPE_SRIOV;
	cmd.group_member_id = pci_iov_vf_id(pdev) + 1;
	cmd.result_sg = sgs;
	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);
	if (ret) {
		dev_err(&virtio_dev->dev,
			"Failed to run command ret(%d)\n", ret);
		goto out;
	}

	*rd_sz = le32_to_cpu(res->size);
	*remaining_sz = le32_to_cpu(res->remaining_ctx_size);

out:
	if (res)
		kfree(res);
	return ret;
}

static int
admin_unit_cmd_dev_ctx_rd_proc(uint8_t vf_idx)
{
	int ret = 0, buf_sz, rd_sz, remaining_sz;
	struct pci_dev *vf_pdev;
	u8 *buf;
	char *str;

	if (vf_idx == 0) {
		if (!g_dev_mgr.vf0_ctx_sz){
			pr_err("Should read ctx sz first");
			return -EINVAL;
		}

		if(!g_dev_mgr.vf0_ctx) {
			g_dev_mgr.vf0_ctx =
				kzalloc(g_dev_mgr.vf0_ctx_sz, GFP_KERNEL);
			if (!g_dev_mgr.vf0_ctx) {
				pr_err("Can not alloc memory \n");
				return -ENOMEM;
			}
		}

		buf = g_dev_mgr.vf0_ctx;
		buf_sz = g_dev_mgr.vf0_ctx_sz;

	} else {
		if (!g_dev_mgr.vf1_ctx_sz){
			pr_err("Should read ctx sz first");
			return -EINVAL;
		}

		if(!g_dev_mgr.vf1_ctx) {
			g_dev_mgr.vf1_ctx =
				kzalloc(g_dev_mgr.vf1_ctx_sz, GFP_KERNEL);
			if (!g_dev_mgr.vf1_ctx) {
				pr_err("Can not alloc memory \n");
				return -ENOMEM;
			}
		}

		buf = g_dev_mgr.vf1_ctx;
		buf_sz = g_dev_mgr.vf1_ctx_sz;
	}

	pr_err("%s:%d: exec dev ctx read \n",__func__, __LINE__);

	vf_pdev = vf_idx == 0 ? g_dev_mgr.vf0_pdev : g_dev_mgr.vf1_pdev;

	ret = admin_unit_cmd_dev_ctx_rd(vf_pdev, buf, buf_sz,
					&rd_sz, &remaining_sz);
	if (ret)
		pr_err("Failed to run admin_unit_cmd_dev_ctx_rd ret(%d)\n",
			ret);

	if (vf_idx == 0) {
		g_dev_mgr.vf0_ctx_pos = g_dev_mgr.vf0_ctx;
		g_dev_mgr.vf0_ctx_left = g_dev_mgr.vf0_ctx_sz;
	} else {

		g_dev_mgr.vf1_ctx_pos = g_dev_mgr.vf1_ctx;
		g_dev_mgr.vf1_ctx_left = g_dev_mgr.vf1_ctx_sz;
	}

	str = (char*) buf;
	pr_err("Dump out ret %d \n", ret);
	pr_err("rd_sz = %#x \n", rd_sz);
	pr_err("remaining_sz = %#x \n", remaining_sz);
	pr_err("Dump out dev ctx \n");
	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_NONE, 16, 4, buf,
		       buf_sz, true);

	return ret;
}

static int
admin_unit_cmd_dev_ctx_rd_partial_proc(uint8_t vf_idx, int sz, bool left)
{
	int ret = 0, buf_sz, rd_sz, remaining_sz, total_sz;
	struct pci_dev *vf_pdev;
	u8 *buf, *total_buf;

	if (vf_idx == 0) {
		if (!g_dev_mgr.vf0_ctx_left){
			pr_err("Should read ctx sz first");
			return -EINVAL;
		}

		if(!g_dev_mgr.vf0_ctx) {
			g_dev_mgr.vf0_ctx =
				kzalloc(g_dev_mgr.vf0_ctx_sz, GFP_KERNEL);
			if (!g_dev_mgr.vf0_ctx) {
				pr_err("Can not alloc memory \n");
				return -ENOMEM;
			}
			g_dev_mgr.vf0_ctx_pos = g_dev_mgr.vf0_ctx;
		}

		buf = g_dev_mgr.vf0_ctx_pos;
		buf_sz = g_dev_mgr.vf0_ctx_left;

	} else {
		if (!g_dev_mgr.vf1_ctx_left){
			pr_err("Should read ctx sz first");
			return -EINVAL;
		}

		if(!g_dev_mgr.vf1_ctx) {
			g_dev_mgr.vf1_ctx =
				kzalloc(g_dev_mgr.vf1_ctx_sz, GFP_KERNEL);
			if (!g_dev_mgr.vf1_ctx) {
				pr_err("Can not alloc memory \n");
				return -ENOMEM;
			}
			g_dev_mgr.vf1_ctx_pos = g_dev_mgr.vf1_ctx;
		}

		buf = g_dev_mgr.vf1_ctx_pos;
		buf_sz = g_dev_mgr.vf1_ctx_left;
	}

	if (!left)
		buf_sz = sz;

	pr_err("%s:%d: exec dev ctx read %d byte \n",__func__, __LINE__, buf_sz);

	vf_pdev = vf_idx == 0 ? g_dev_mgr.vf0_pdev : g_dev_mgr.vf1_pdev;

	ret = admin_unit_cmd_dev_ctx_rd(vf_pdev, buf, buf_sz,
					&rd_sz, &remaining_sz);
	if (ret)
		pr_err("Failed to run admin_unit_cmd_dev_ctx_rd ret(%d)\n",
			ret);

	if (vf_idx == 0) {
		g_dev_mgr.vf0_ctx_pos += buf_sz;
		g_dev_mgr.vf0_ctx_left -= buf_sz;
		pr_err("vf0_ctx_left = %#x \n", g_dev_mgr.vf0_ctx_left);
	} else {
		g_dev_mgr.vf1_ctx_pos += buf_sz;
		g_dev_mgr.vf1_ctx_left -= buf_sz;
		pr_err("vf1_ctx_left = %#x \n", g_dev_mgr.vf1_ctx_left);
	}

	/* reset after read all */
	if(left) {
		if (vf_idx == 0) {
			g_dev_mgr.vf0_ctx_pos = g_dev_mgr.vf0_ctx;
			g_dev_mgr.vf0_ctx_left = g_dev_mgr.vf0_ctx_sz;

			total_buf = g_dev_mgr.vf0_ctx_pos;
			total_sz = g_dev_mgr.vf0_ctx_left;
		} else {

			g_dev_mgr.vf1_ctx_pos = g_dev_mgr.vf1_ctx;
			g_dev_mgr.vf1_ctx_left = g_dev_mgr.vf1_ctx_sz;

			total_buf = g_dev_mgr.vf1_ctx;
			total_sz = g_dev_mgr.vf1_ctx_sz;

		}
	}

	pr_err("Dump out ret %d \n", ret);
	pr_err("rd_sz = %#x \n", rd_sz);
	pr_err("remaining_sz = %#x \n", remaining_sz);
	pr_err("Dump out part dev ctx \n");
	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_NONE, 16, 4, buf,
		       buf_sz, true);

	if(left) {
		pr_err("===== Dump out dev ctx ======== \n");
		print_hex_dump(KERN_ERR, "", DUMP_PREFIX_NONE, 16, 4, total_buf,
			       total_sz, true);
	}

	return ret;
}

static int
admin_unit_cmd_dev_ctx_wr(struct pci_dev *pdev, u8 *buf, int buf_size)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct virtio_admin_cmd cmd = {};
	struct scatterlist in_sg;
	u8* in;
	int ret;

	if (!virtio_dev)
		return -ENOTCONN;

	if (!pdev->is_virtfn)
		pr_err("pdev should be a Virtual Function.\n");

	dev_info(&pdev->dev, "Vf pdev(%s) domain %d bus %#x devfn %#x",
		pci_name(pdev),
		pci_domain_nr(pdev->bus),
		pdev->bus->number, pdev->devfn);

	dev_info(&virtio_dev->dev, "Use PF(%s) send cmd for VF id (%d)\n",
		dev_name(&virtio_dev->dev),
		pci_iov_vf_id(pdev));

	in = kzalloc(buf_size, GFP_KERNEL);
	if (!in)
		return -ENOMEM;
	memcpy(in, buf, buf_size);

	sg_init_one(&in_sg, in, buf_size);

	cmd.opcode = VIRTIO_ADMIN_CMD_DEV_CTX_WRITE;
	cmd.group_type = VIRTIO_ADMIN_GROUP_TYPE_SRIOV;
	cmd.group_member_id = pci_iov_vf_id(pdev) + 1;
	cmd.data_sg = &in_sg;
	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);

	return ret;
}

static int
admin_unit_cmd_dev_ctx_wr_proc(uint8_t vf_idx)
{
	// char *str = "Hello Conrtroller, I am godfeng";
	struct pci_dev *vf_pdev;
	int ret = 0, buf_sz;
	u8 *buf;

	if (vf_idx == 0) {
		buf = g_dev_mgr.vf1_ctx;
		if (!buf){
			pr_err("Should read vf1 dev ctx first");
			return -EINVAL;
		}
		buf_sz = g_dev_mgr.vf1_ctx_sz;

		g_dev_mgr.vf1_ctx = NULL;
		g_dev_mgr.vf1_ctx_sz = 0;
		g_dev_mgr.vf1_ctx_pos = NULL;
		g_dev_mgr.vf1_ctx_left = 0;
	} else {
		buf = g_dev_mgr.vf0_ctx;
		if (!buf){
			pr_err("Should read vf0 dev ctx first");
			return -EINVAL;
		}
		buf_sz = g_dev_mgr.vf0_ctx_sz;

		g_dev_mgr.vf0_ctx = NULL;
		g_dev_mgr.vf0_ctx_sz = 0;
		g_dev_mgr.vf0_ctx_pos = NULL;
		g_dev_mgr.vf0_ctx_left = 0;
	}

	pr_err("%s:%d: exec dev ctx write \n",__func__, __LINE__);
	// memcpy(buf, str, strlen(str) + 1);

	vf_pdev = vf_idx == 0 ? g_dev_mgr.vf0_pdev : g_dev_mgr.vf1_pdev;
	ret = admin_unit_cmd_dev_ctx_wr(vf_pdev, buf, buf_sz);
	if (ret)
		pr_err("Failed to run admin_unit_cmd_dev_ctx_rd ret(%d)\n",
			ret);

	kfree(buf);
	return ret;
}

static int
admin_unit_cmd_dev_ctx_wr_partial_proc(uint8_t vf_idx, int sz, bool left)
{
	struct pci_dev *vf_pdev;
	int ret = 0, buf_sz;
	u8 *buf, *total_buf;

	if (vf_idx == 0) {
		buf = g_dev_mgr.vf1_ctx_pos;
		if (!buf){
			pr_err("Should read vf1 dev ctx first");
			return -EINVAL;
		}
		if (left) {
			buf_sz = g_dev_mgr.vf1_ctx_left;
			total_buf = g_dev_mgr.vf1_ctx;

			g_dev_mgr.vf1_ctx = NULL;
			g_dev_mgr.vf1_ctx_pos = NULL;
			g_dev_mgr.vf1_ctx_left = 0;
			g_dev_mgr.vf1_ctx_sz = 0;
		} else {
			buf_sz = sz;
			g_dev_mgr.vf1_ctx_pos += sz;
			g_dev_mgr.vf1_ctx_left -= sz;
		}
	} else {
		buf = g_dev_mgr.vf0_ctx_pos;
		if (!buf){
			pr_err("Should read vf0 dev ctx first");
			return -EINVAL;
		}
		if (left) {
			buf_sz = g_dev_mgr.vf0_ctx_left;
			total_buf = g_dev_mgr.vf0_ctx;

			g_dev_mgr.vf0_ctx = NULL;
			g_dev_mgr.vf0_ctx_pos = NULL;
			g_dev_mgr.vf0_ctx_left = 0;
			g_dev_mgr.vf0_ctx_sz = 0;
		} else {
			buf_sz = sz;
			g_dev_mgr.vf0_ctx_pos += sz;
			g_dev_mgr.vf0_ctx_left -= sz;
		}
	}

	pr_err("%s:%d: exec dev ctx write %d bytes on vf%d\n",
		__func__, __LINE__, buf_sz, vf_idx);

	vf_pdev = vf_idx == 0 ? g_dev_mgr.vf0_pdev : g_dev_mgr.vf1_pdev;
	ret = admin_unit_cmd_dev_ctx_wr(vf_pdev, buf, buf_sz);
	if (ret)
		pr_err("Failed to run admin_unit_cmd_dev_ctx_rd ret(%d)\n",
			ret);

	if (left)
		kfree(total_buf);
	return ret;
}

static int
admin_unit_cmd_sprt_field_query(struct pci_dev *pdev,
				u8 *buf, int buf_size)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct scatterlist out_sg;
	struct virtio_admin_cmd cmd = {};
	int ret;

	if (!virtio_dev)
		return -ENOTCONN;

	if (!pdev->is_virtfn)
		pr_err("pdev should be a Virtual Function.\n");

	dev_info(&pdev->dev, "Vf pdev(%s) domain %d bus %#x devfn %#x",
		pci_name(pdev),
		pci_domain_nr(pdev->bus),
		pdev->bus->number, pdev->devfn);

	dev_info(&virtio_dev->dev, "Use PF(%s) send cmd for VF id (%d)\n",
		dev_name(&virtio_dev->dev),
		pci_iov_vf_id(pdev));

	sg_init_one(&out_sg, buf, buf_size);

	cmd.opcode = VIRTIO_ADMIN_CMD_DEV_CTX_FIELDS_QUERY;
	cmd.group_type = VIRTIO_ADMIN_GROUP_TYPE_SRIOV;
	cmd.group_member_id = pci_iov_vf_id(pdev) + 1;
	cmd.result_sg = &out_sg;

	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);
	return ret;
}

#define MAX_SUPPORT_FIELD	15
static int
admin_unit_cmd_sprt_field_query_proc(uint8_t vf_idx)
{
	struct virtio_admin_cmd_dev_ctx_supported_field *fld;
	struct pci_dev *vf_pdev;
	int ret = 0, i;

	g_dev_mgr.ctx_sprt_flds_sz = MAX_SUPPORT_FIELD *
			sizeof(struct virtio_admin_cmd_dev_ctx_supported_field);
	if (!g_dev_mgr.ctx_sprt_flds) {
		g_dev_mgr.ctx_sprt_flds =
			kzalloc(g_dev_mgr.ctx_sprt_flds_sz, GFP_KERNEL);
		if (!g_dev_mgr.ctx_sprt_flds) {
			pr_err("Can not alloc support field buffer\n");
			return -ENOMEM;
		}
	}

	pr_err("%s:%d: exec supported field query on vf%d\n",
						__func__, __LINE__, vf_idx);

	vf_pdev = vf_idx == 0 ? g_dev_mgr.vf0_pdev : g_dev_mgr.vf1_pdev;
	ret = admin_unit_cmd_sprt_field_query(vf_pdev,
					      g_dev_mgr.ctx_sprt_flds,
					      g_dev_mgr.ctx_sprt_flds_sz);
	if (ret)
		pr_err("Failed to run admin_unit_cmd_sprt_field_query ret(%d)\n",
			ret);

	fld = (struct virtio_admin_cmd_dev_ctx_supported_field *)
		g_dev_mgr.ctx_sprt_flds;

	for (i = 0; i < MAX_SUPPORT_FIELD; i++) {
		pr_err("supported_field[%d] type(%#x), length(%d)",
			i, fld[i].type, fld[i].length);
	}

	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_NONE, 16, 4, g_dev_mgr.ctx_sprt_flds,
		       g_dev_mgr.ctx_sprt_flds_sz, true);

	return ret;
}

static int
admin_unit_cmd_discard(struct pci_dev *pdev)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct virtio_admin_cmd cmd = {};
	int ret;

	if (!virtio_dev)
		return -ENOTCONN;

	if (!pdev->is_virtfn)
		pr_err("pdev should be a Virtual Function.\n");

	dev_info(&pdev->dev, "Vf pdev(%s) domain %d bus %#x devfn %#x",
		pci_name(pdev),
		pci_domain_nr(pdev->bus),
		pdev->bus->number, pdev->devfn);

	dev_info(&virtio_dev->dev, "Use PF(%s) send cmd for VF id (%d)\n",
		dev_name(&virtio_dev->dev),
		pci_iov_vf_id(pdev));

	cmd.opcode = VIRTIO_ADMIN_CMD_DEV_CTX_DISCARD;
	cmd.group_type = VIRTIO_ADMIN_GROUP_TYPE_SRIOV;
	cmd.group_member_id = pci_iov_vf_id(pdev) + 1;

	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);
	return ret;
}

static int
admin_unit_cmd_discard_proc(uint8_t vf_idx)
{
	struct pci_dev *vf_pdev;
	int ret = 0;

	g_dev_mgr.ctx_sprt_flds_sz = MAX_SUPPORT_FIELD *
			sizeof(struct virtio_admin_cmd_dev_ctx_supported_field);
	if (!g_dev_mgr.ctx_sprt_flds) {
		g_dev_mgr.ctx_sprt_flds =
			kzalloc(g_dev_mgr.ctx_sprt_flds_sz, GFP_KERNEL);
		if (!g_dev_mgr.ctx_sprt_flds) {
			pr_err("Can not alloc support field buffer\n");
			return -ENOMEM;
		}
	}

	pr_err("%s:%d: exec supported field query on vf%d\n",
						__func__, __LINE__, vf_idx);

	vf_pdev = vf_idx == 0 ? g_dev_mgr.vf0_pdev : g_dev_mgr.vf1_pdev;
	ret = admin_unit_cmd_discard(vf_pdev);
	if (ret)
		pr_err("Failed to run admin_unit_cmd_sprt_field_query ret(%d)\n",
			ret);

	// if (vf_idx == 0) {
	// 	if (g_dev_mgr.vf0_ctx)
	// 		kfree(g_dev_mgr.vf0_ctx);

	// 	g_dev_mgr.vf0_ctx = NULL;
	// 	g_dev_mgr.vf0_ctx_pos = NULL;
	// 	g_dev_mgr.vf0_ctx_sz = 0;
	// } else {
	// 	if (g_dev_mgr.vf1_ctx)
	// 		kfree(g_dev_mgr.vf1_ctx);

	// 	g_dev_mgr.vf1_ctx = NULL;
	// 	g_dev_mgr.vf1_ctx_pos = NULL;
	// 	g_dev_mgr.vf1_ctx_sz = 0;
	// }

	return ret;
}

#define ADMIN_CMD_LIST_USE			"list_use"
#define ADMIN_CMD_LIST_QUERY			"list_query"

#define ADMIN_CMD_DEV_MODE_GET_VF0		"dev_mode_get_vf0"
#define ADMIN_CMD_DEV_MODE_GET_VF1		"dev_mode_get_vf1"

#define ADMIN_CMD_DEV_MODE_SET_VF0_ACTIVE	"dev_mode_set_vf0_active"
#define ADMIN_CMD_DEV_MODE_SET_VF0_STOP		"dev_mode_set_vf0_stop"
#define ADMIN_CMD_DEV_MODE_SET_VF0_FREEZE	"dev_mode_set_vf0_freeze"
#define ADMIN_CMD_DEV_MODE_SET_VF1_ACTIVE	"dev_mode_set_vf1_active"
#define ADMIN_CMD_DEV_MODE_SET_VF1_STOP		"dev_mode_set_vf1_stop"
#define ADMIN_CMD_DEV_MODE_SET_VF1_FREEZE	"dev_mode_set_vf1_freeze"

#define ADMIN_CMD_DEV_CTX_SZ_GET_VF0_NOFREEZE	"dev_ctx_get_vf0_nofreeze"
#define ADMIN_CMD_DEV_CTX_SZ_GET_VF0_FREEZE	"dev_ctx_get_vf0_freeze"
#define ADMIN_CMD_DEV_CTX_SZ_GET_VF1_NOFREEZE	"dev_ctx_get_vf1_nofreeze"
#define ADMIN_CMD_DEV_CTX_SZ_GET_VF1_FREEZE	"dev_ctx_get_vf1_freeze"

#define ADMIN_CMD_DEV_CTX_RD_VF0		"dev_ctx_rd_vf0"
#define ADMIN_CMD_DEV_CTX_RD_VF1		"dev_ctx_rd_vf1"
#define ADMIN_CMD_DEV_CTX_RD_200B_VF0		"dev_ctx_rd_200B_vf0"
#define ADMIN_CMD_DEV_CTX_RD_200B_VF1		"dev_ctx_rd_200B_vf1"
#define ADMIN_CMD_DEV_CTX_RD_LEFT_VF0		"dev_ctx_rd_left_vf0"
#define ADMIN_CMD_DEV_CTX_RD_LEFT_VF1		"dev_ctx_rd_left_vf1"

#define ADMIN_CMD_DEV_CTX_WR_VF0		"dev_ctx_wr_vf0"
#define ADMIN_CMD_DEV_CTX_WR_VF1		"dev_ctx_wr_vf1"
#define ADMIN_CMD_DEV_CTX_WR_200B_VF0		"dev_ctx_wr_200B_vf0"
#define ADMIN_CMD_DEV_CTX_WR_200B_VF1		"dev_ctx_wr_200B_vf1"
#define ADMIN_CMD_DEV_CTX_WR_LEFT_VF0		"dev_ctx_wr_left_vf0"
#define ADMIN_CMD_DEV_CTX_WR_LEFT_VF1		"dev_ctx_wr_left_vf1"

#define ADMIN_CMD_DISCARD_VF0			"dev_discard_vf0"
#define ADMIN_CMD_DISCARD_VF1			"dev_discard_vf1"

#define ADMIN_CMD_FIELDS_QUERY_VF0		"dev_field_query_vf0"
#define ADMIN_CMD_FIELDS_QUERY_VF1		"dev_field_query_vf1"

static int admin_unit_cmd_process(const char *buf, int len)
{
	int ret = 0;

	if (!strncmp(buf, ADMIN_CMD_LIST_USE, strlen(ADMIN_CMD_LIST_USE))) {
		pr_err("%s:%d: list_use %s\n",__func__, __LINE__, buf);
		//TOOD
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_LIST_QUERY, strlen(ADMIN_CMD_LIST_QUERY))) {
		ret = admin_unit_cmd_list_query_proc();
		if(ret)
			pr_err("Failed to run list query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_MODE_GET_VF0, strlen(ADMIN_CMD_DEV_MODE_GET_VF0))) {
		ret = admin_unit_cmd_dev_mode_get_proc(0);
		if(ret)
			pr_err("Failed to run list query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_MODE_GET_VF1, strlen(ADMIN_CMD_DEV_MODE_GET_VF1))) {
		ret = admin_unit_cmd_dev_mode_get_proc(1);
		if(ret)
			pr_err("Failed to run list query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_MODE_SET_VF0_ACTIVE, strlen(ADMIN_CMD_DEV_MODE_SET_VF0_ACTIVE))) {
		ret = admin_unit_cmd_dev_mode_set_proc(0, VIRTIO_ADMIN_DEV_MODE_ACTIVE);
		if(ret)
			pr_err("Failed to run list query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_MODE_SET_VF0_STOP, strlen(ADMIN_CMD_DEV_MODE_SET_VF0_STOP))) {
		ret = admin_unit_cmd_dev_mode_set_proc(0, VIRTIO_ADMIN_DEV_MODE_STOP);
		if(ret)
			pr_err("Failed to run list query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_MODE_SET_VF0_FREEZE, strlen(ADMIN_CMD_DEV_MODE_SET_VF0_FREEZE))) {
		ret = admin_unit_cmd_dev_mode_set_proc(0, VIRTIO_ADMIN_DEV_MODE_FREEZE);
		if(ret)
			pr_err("Failed to run list query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_MODE_SET_VF1_ACTIVE, strlen(ADMIN_CMD_DEV_MODE_SET_VF1_ACTIVE))) {
		ret = admin_unit_cmd_dev_mode_set_proc(1, VIRTIO_ADMIN_DEV_MODE_ACTIVE);
		if(ret)
			pr_err("Failed to run list query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_MODE_SET_VF1_STOP, strlen(ADMIN_CMD_DEV_MODE_SET_VF1_STOP))) {
		ret = admin_unit_cmd_dev_mode_set_proc(1, VIRTIO_ADMIN_DEV_MODE_STOP);
		if(ret)
			pr_err("Failed to run list query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_MODE_SET_VF1_FREEZE, strlen(ADMIN_CMD_DEV_MODE_SET_VF1_FREEZE))) {
		ret = admin_unit_cmd_dev_mode_set_proc(1, VIRTIO_ADMIN_DEV_MODE_FREEZE);
		if(ret)
			pr_err("Failed to run list query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_CTX_SZ_GET_VF0_NOFREEZE, strlen(ADMIN_CMD_DEV_CTX_SZ_GET_VF0_NOFREEZE))) {
		ret = admin_unit_cmd_dev_ctx_sz_get_proc(0, 0);
		if(ret)
			pr_err("Failed to run list query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_CTX_SZ_GET_VF0_FREEZE, strlen(ADMIN_CMD_DEV_CTX_SZ_GET_VF0_FREEZE))) {
		ret = admin_unit_cmd_dev_ctx_sz_get_proc(0, 1);
		if(ret)
			pr_err("Failed to run list query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_CTX_SZ_GET_VF1_NOFREEZE, strlen(ADMIN_CMD_DEV_CTX_SZ_GET_VF1_NOFREEZE))) {
		ret = admin_unit_cmd_dev_ctx_sz_get_proc(1, 0);
		if(ret)
			pr_err("Failed to run list query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_CTX_SZ_GET_VF1_FREEZE, strlen(ADMIN_CMD_DEV_CTX_SZ_GET_VF1_FREEZE))) {
		ret = admin_unit_cmd_dev_ctx_sz_get_proc(1, 1);
		if(ret)
			pr_err("Failed to run list query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_CTX_RD_VF0, strlen(ADMIN_CMD_DEV_CTX_RD_VF0))) {
		ret = admin_unit_cmd_dev_ctx_rd_proc(0);
		if(ret)
			pr_err("Failed to run list query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_CTX_RD_VF1, strlen(ADMIN_CMD_DEV_CTX_RD_VF1))) {
		ret = admin_unit_cmd_dev_ctx_rd_proc(1);
		if(ret)
			pr_err("Failed to run list query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_CTX_RD_200B_VF0, strlen(ADMIN_CMD_DEV_CTX_RD_200B_VF0))) {
		ret = admin_unit_cmd_dev_ctx_rd_partial_proc(0, 200, false);
		if(ret)
			pr_err("Failed to run list query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_CTX_RD_200B_VF1, strlen(ADMIN_CMD_DEV_CTX_RD_200B_VF1))) {
		ret = admin_unit_cmd_dev_ctx_rd_partial_proc(1, 200, false);
		if(ret)
			pr_err("Failed to run list query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_CTX_RD_LEFT_VF0, strlen(ADMIN_CMD_DEV_CTX_RD_LEFT_VF0))) {
		ret = admin_unit_cmd_dev_ctx_rd_partial_proc(0, 200, true);
		if(ret)
			pr_err("Failed to run rd 200 on vf0 %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_CTX_RD_LEFT_VF1, strlen(ADMIN_CMD_DEV_CTX_RD_LEFT_VF1))) {
		ret = admin_unit_cmd_dev_ctx_rd_partial_proc(1, 200, true);
		if(ret)
			pr_err("Failed to run rd 200 on vf1 %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_CTX_WR_VF0, strlen(ADMIN_CMD_DEV_CTX_WR_VF0))) {
		ret = admin_unit_cmd_dev_ctx_wr_proc(0);
		if(ret)
			pr_err("Failed to run rd left on vf0 %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_CTX_WR_VF1, strlen(ADMIN_CMD_DEV_CTX_WR_VF1))) {
		ret = admin_unit_cmd_dev_ctx_wr_proc(1);
		if(ret)
			pr_err("Failed to run rd left on vf1 %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_CTX_WR_200B_VF0, strlen(ADMIN_CMD_DEV_CTX_WR_200B_VF0))) {
		ret = admin_unit_cmd_dev_ctx_wr_partial_proc(0, 200, false);
		if(ret)
			pr_err("Failed to run wr 200B on vf0 %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_CTX_WR_200B_VF1, strlen(ADMIN_CMD_DEV_CTX_WR_200B_VF1))) {
		ret = admin_unit_cmd_dev_ctx_wr_partial_proc(1, 200, false);
		if(ret)
			pr_err("Failed to run wr 200B on vf1 %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_CTX_WR_LEFT_VF0, strlen(ADMIN_CMD_DEV_CTX_WR_LEFT_VF0))) {
		ret = admin_unit_cmd_dev_ctx_wr_partial_proc(0, 200, true);
		if(ret)
			pr_err("Failed to run wr left on vf0 %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DEV_CTX_WR_LEFT_VF1, strlen(ADMIN_CMD_DEV_CTX_WR_LEFT_VF1))) {
		ret = admin_unit_cmd_dev_ctx_wr_partial_proc(1, 200, true);
		if(ret)
			pr_err("Failed to run wr left on vf1 %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_FIELDS_QUERY_VF0, strlen(ADMIN_CMD_FIELDS_QUERY_VF0))) {
		ret = admin_unit_cmd_sprt_field_query_proc(0);
		if(ret)
			pr_err("Failed to supported fld query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_FIELDS_QUERY_VF1, strlen(ADMIN_CMD_FIELDS_QUERY_VF1))) {
		ret = admin_unit_cmd_sprt_field_query_proc(1);
		if(ret)
			pr_err("Failed to supported fld query %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DISCARD_VF0, strlen(ADMIN_CMD_DISCARD_VF0))) {
		ret = admin_unit_cmd_discard_proc(0);
		if(ret)
			pr_err("Failed to discard %d", ret);
		return ret;
	}

	if (!strncmp(buf, ADMIN_CMD_DISCARD_VF1, strlen(ADMIN_CMD_DISCARD_VF1))) {
		ret = admin_unit_cmd_discard_proc(1);
		if(ret)
			pr_err("Failed to discard %d", ret);
		return ret;
	}

	pr_err("Unknow admin cmd %s \n", buf);
	return -EINVAL;
}

static ssize_t admin_unit_cmd_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *pos)
{
	char *buf;
	int ret;

	buf = memdup_user_nul(buffer, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	ret = admin_unit_cmd_process(buf, count);
	if(ret)
		pr_err("%s,%d: process cmd(%s) failed %d\n",
			__func__, __LINE__, buf, ret);

	kfree(buf);
	return count;
}

static const struct proc_ops admin_unit_cmd_proc_fops = {
	.proc_open	= admin_unit_cmd_proc_open,
	.proc_read	= seq_read,
	.proc_write	= admin_unit_cmd_proc_write,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

/* the /proc function: allocate everything to allow concurrency */
static int jit_timer_proc_show(struct seq_file *m, void *v)
{
    return 0;
}

static int jit_timer_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, jit_timer_proc_show, NULL);
}

static const struct proc_ops jit_timer_proc_fops = {
	.proc_open	= jit_timer_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};


void admin_unit_prepare_dev(void)
{
	unsigned int domain;
	unsigned int bus_num;
	unsigned int device;
	unsigned int function;
	struct pci_dev * pdev = NULL;

	/* PF */
	domain = 0x0;
	bus_num = 0x83;
	device = 0x0;
	function = 0x2;
	pdev = pci_get_domain_bus_and_slot(domain, bus_num,
					   PCI_DEVFN(device, function));
	if (pdev) {
		dev_info(&pdev->dev,
			"godfeng pf pdev(%s) domain %d bus %#x devfn %#x",
			pci_name(pdev),
			pci_domain_nr(pdev->bus),
			pdev->bus->number, pdev->devfn);
		g_dev_mgr.pf_pdev = pdev;
	} else
		pr_err("Cannot find pf pci device\n");

	/* VF0 */
	domain = 0x0;
	bus_num = 0x83;
	device = 0x4;
	function = 0x4;
	pdev = pci_get_domain_bus_and_slot(domain, bus_num,
					   PCI_DEVFN(device, function));
	if (pdev) {
		dev_info(&pdev->dev,
			"godfeng vf0 pdev(%s) domain %d bus %#x devfn %#x",
			pci_name(pdev),
			pci_domain_nr(pdev->bus),
			pdev->bus->number, pdev->devfn);
		g_dev_mgr.vf0_pdev = pdev;
	} else
		pr_err("Cannot find vf0 pci device\n");

	/* VF1 */
	domain = 0x0;
	bus_num = 0x83;
	device = 0x4;
	function = 0x5;
	pdev = pci_get_domain_bus_and_slot(domain, bus_num,
					   PCI_DEVFN(device, function));
	if (pdev) {
		/* VF1 is a fake device , dont repsonse cci and cvq*/
		struct virtio_device *vdev = virtio_pci_dev_get_vdev(pdev);
		vdev->ignore_cvq = true;
		dev_info(&pdev->dev,
			"godfeng vf1 pdev(%s) domain %d bus %#x devfn %#x",
			pci_name(pdev),
			pci_domain_nr(pdev->bus),
			pdev->bus->number, pdev->devfn);
		g_dev_mgr.vf1_pdev = pdev;
	} else
		pr_err("Cannot find vf1 pci device\n");
}

int __init admin_unit_init(void)
{
	char * depmods[DEP_MOD_NUM] = {"virtio_pci", "virtio_net"};
	umode_t mode = 0644;
	int i, ret;

	for(i = 0; i < DEP_MOD_NUM; i++) {
		ret = request_module(depmods[i]);
		if (ret < 0) {
			pr_err("Failed to load %s: %d\n", depmods[i], ret);
			return ret;
		} else
			pr_info("Loaded %s successfully \n", depmods[i]);
	}

	admin_unit_dir = proc_mkdir("admin_unit", NULL);
	if (!admin_unit_dir)
		return -ENOENT;

	proc_create("cmd_ops", mode, admin_unit_dir, &admin_unit_cmd_proc_fops);

	admin_unit_prepare_dev();

	return 0; /* success */
}

void __exit admin_unit_cleanup(void)
{
	remove_proc_entry("cmd_ops", admin_unit_dir);
	proc_remove(admin_unit_dir);

	if (g_dev_mgr.op_list_buf)
		kfree(g_dev_mgr.op_list_buf);
	if (g_dev_mgr.dev_mode)
		kfree(g_dev_mgr.dev_mode);
	if (g_dev_mgr.new_dev_mode)
		kfree(g_dev_mgr.new_dev_mode);
	if (g_dev_mgr.ctx_sz_res)
		kfree(g_dev_mgr.ctx_sz_res);
	if (g_dev_mgr.vf0_ctx)
		kfree(g_dev_mgr.vf0_ctx);
	if (g_dev_mgr.vf1_ctx)
		kfree(g_dev_mgr.vf1_ctx);
	if (g_dev_mgr.ctx_sprt_flds)
		kfree(g_dev_mgr.ctx_sprt_flds);

	g_dev_mgr.vf0_ctx_pos = NULL;
	g_dev_mgr.vf1_ctx_pos = NULL;
}

module_init(admin_unit_init);
module_exit(admin_unit_cleanup);
