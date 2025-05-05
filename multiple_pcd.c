#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#undef pr_fmt
#define pr_fmt(fmt) "[PCD]%s: " fmt, __func__

#define DEV_MEM_SIZE 512
#define NUM_DEVICES 2

char device_buffers[NUM_DEVICES][DEV_MEM_SIZE];
dev_t base_device_number = 0;
dev_t device_numbers[NUM_DEVICES];
struct cdev pcd_cdev;

static loff_t pcd_lseek(struct file *file, loff_t offset, int whence) {
	loff_t new_pos;
	switch (whence) {
	case SEEK_SET:
		new_pos = offset;
		break;
	case SEEK_CUR:
		new_pos = file->f_pos + offset;
		break;
	case SEEK_END:
		new_pos = DEV_MEM_SIZE + offset;
		break;
	default:
		pr_err("Invalid whence %d\n", whence);
		return -EINVAL;
	}
	if ((new_pos > DEV_MEM_SIZE) || (new_pos < 0)) {
		pr_err("Invalid file position %lld\n", new_pos);
		return -EINVAL;
	}
	file->f_pos = new_pos;
	pr_info("New file position %lld\n", new_pos);
	return 0;
}

static ssize_t pcd_read(struct file *file, char __user *buff, size_t count, loff_t *pos) {
	char *buffer = device_buffers[MINOR(file->f_inode->i_rdev)];
	pr_info("<%d:%d>Read requested for %zu bytes, current file position %lld\n", MAJOR(file->f_inode->i_rdev), MINOR(file->f_inode->i_rdev), count, *pos);
	if (*pos >= DEV_MEM_SIZE) {
		return 0;
	}
	if (*pos + count > DEV_MEM_SIZE) {
		count = DEV_MEM_SIZE - *pos;
	}
	/* カーネル空間のバッファからユーザー空間のバッファへコピー */
	if (copy_to_user(buff, buffer + *pos, count)) {
		return -EFAULT;
	}
	/* ファイルポジションを更新 */
	*pos += (loff_t)count;
	pr_info("%zu bytes read, current file position %lld\n", count, *pos);
	return (ssize_t)count;
}

static ssize_t pcd_write(struct file *file, const char __user *buff, size_t count, loff_t *pos) {
	char *buffer = device_buffers[MINOR(file->f_inode->i_rdev)];
	pr_info("<%d:%d>Write requested for %zu bytes, current file position %lld\n", MAJOR(file->f_inode->i_rdev), MINOR(file->f_inode->i_rdev), count, *pos);
	if (*pos >= DEV_MEM_SIZE) {
		// より具体的なエラーメッセージに変更
		pr_err("No space left on device. Current position %lld, device size %d\n", *pos, DEV_MEM_SIZE);
		return -ENOSPC;
	}
	if (*pos + count > DEV_MEM_SIZE) {
		count = DEV_MEM_SIZE - *pos;
		// 部分的な書き込みになることを警告するメッセージを追加しても良いかもしれません
		pr_warn("Partial write: requested %llu bytes, but only %zu bytes available\n",
			count + (*pos + count > DEV_MEM_SIZE ? (*pos + count - DEV_MEM_SIZE) : 0) /* 元のcount */, count);
	}
	/* ユーザー空間のバッファからカーネル空間のバッファへコピー */
	if (copy_from_user(buffer + *pos, buff, count)) {
		pr_err("Failed to copy data from user space\n"); // copy_from_user失敗時のメッセージも追加
		return -EFAULT;
	}
	/* ファイルポジションを更新 */
	*pos += (loff_t)count;
	pr_info("%zu bytes written, current file position %lld\n", count, *pos);
	// writeシステムコールは成功した場合、書き込んだバイト数を返すのが一般的です
	// 0を返すと、書き込みが成功したにも関わらず0バイト書き込んだと解釈される可能性があります
	return (ssize_t)count;
}

static int pcd_open(struct inode *inode, struct file *file) {
	pr_info("Device opened\n");
	return 0;
}
static int pcd_release(struct inode *inode, struct file *file) {
	pr_info("Device closed\n");
	return 0;
}

struct file_operations pcd_fopes = {
	.owner = THIS_MODULE,
	.llseek = pcd_lseek,
	.read = pcd_read,
	.write = pcd_write,
	.open = pcd_open,
	.release = pcd_release,
};

struct class *pcd_class;
/* デバイスファイル生成時のコールバック */
static char *pcd_devnode(const struct device *device, umode_t *mode) {
	pr_info("pcd_devnode called\n");
	/* 権限を"rw-rw-rw-"へ */
	if (mode) {
		*mode = 0666;
	}
	return NULL;
}
struct device *pcd_devices[NUM_DEVICES];

static int __init pcd_driver_init(void) {
	alloc_chrdev_region(&base_device_number, 0, NUM_DEVICES, "pcd_devices");
	pr_info("Device number is %d:%d-%d(major:minor)", MAJOR(base_device_number), MINOR(base_device_number), NUM_DEVICES - 1);

	// cdevの初期化とファイル操作関数群を設定する
	cdev_init(&pcd_cdev, &pcd_fopes);
	// デバイスの登録
	pcd_cdev.owner = THIS_MODULE;
	cdev_add(&pcd_cdev, base_device_number, NUM_DEVICES);

	/* デバイスクラスを生成 */
	pcd_class = class_create("pcd_class");
	pcd_class->devnode = pcd_devnode;

	for (int i = 0; i < NUM_DEVICES; i++) {
		struct device *d = pcd_devices[i];
		device_numbers[i] = MKDEV(MAJOR(base_device_number), MINOR(base_device_number) + i);
		/* デバイスファイルを生成 */
		d = device_create(pcd_class, NULL, device_numbers[i], NULL, "pcd%d", i);
		pr_info("pcd%d was created\n", i);
	}

	pr_info("Module init was successful\n");
	return 0;
}

static void __exit pcd_driver_cleanup(void) {
	for (int i = 0; i < NUM_DEVICES; i++) {
		device_destroy(pcd_class, device_numbers[i]);
	}
	class_destroy(pcd_class);
	cdev_del(&pcd_cdev);
	unregister_chrdev_region(base_device_number, NUM_DEVICES);
	pr_info("Module unloaded.\n");
}

module_init(pcd_driver_init);
module_exit(pcd_driver_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ATOHS");
MODULE_DESCRIPTION("A pseudo character driver.");
