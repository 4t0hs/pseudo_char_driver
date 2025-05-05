#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#undef pr_fmt
#define pr_fmt(fmt) "[PCD]%s: " fmt, __func__

#define TOTAL_DEVICES 4

#define DEV1_MAX_BUFFER_SIZE 1024
#define DEV2_MAX_BUFFER_SIZE 512
#define DEV3_MAX_BUFFER_SIZE 256
#define DEV4_MAX_BUFFER_SIZE 128

char dev1_buffer[DEV1_MAX_BUFFER_SIZE];
char dev2_buffer[DEV2_MAX_BUFFER_SIZE];
char dev3_buffer[DEV3_MAX_BUFFER_SIZE];
char dev4_buffer[DEV4_MAX_BUFFER_SIZE];

struct device_data {
	int minor;
	char *buffer;
	uint32_t write_index;
	size_t size;
	fmode_t permission;
	struct device *dev;
};

struct device_data device1 = {
	.buffer = dev1_buffer,
	.size = DEV1_MAX_BUFFER_SIZE,
	.permission = FMODE_READ,
};
struct device_data device2 = {
	.buffer = dev2_buffer,
	.size = DEV2_MAX_BUFFER_SIZE,
	.permission = FMODE_WRITE,
};
struct device_data device3 = {
	.buffer = dev3_buffer,
	.size = DEV3_MAX_BUFFER_SIZE,
	.permission = FMODE_WRITE | FMODE_READ,
};
struct device_data device4 = {
	.buffer = dev4_buffer,
	.size = DEV4_MAX_BUFFER_SIZE,
	.permission = FMODE_WRITE | FMODE_READ,
};

struct driver_data {
	dev_t base_device_number;
	struct class *class;
	struct cdev chrdev;
	struct device_data *devices[TOTAL_DEVICES];
};

struct driver_data driver = {
	.base_device_number = 0,
	.class = NULL,
	.devices = { &device1, &device2, &device3, &device4 },
};

static loff_t pcd_lseek(struct file *file, loff_t offset, int whence) {
	struct device_data *dev = file->private_data;
	loff_t new_pos;

	switch (whence) {
	case SEEK_SET:
		new_pos = offset;
		break;
	case SEEK_CUR:
		new_pos = file->f_pos + offset;
		break;
	case SEEK_END:
		new_pos = (loff_t)dev->size + offset;
		break;
	default:
		pr_err("Invalid whence %d\n", whence);
		return -EINVAL;
	}
	if ((new_pos > dev->size) || (new_pos < 0)) {
		pr_err("Invalid file position %lld\n", new_pos);
		return -EINVAL;
	}
	file->f_pos = new_pos;
	pr_info("New file position %lld\n", new_pos);
	return 0;
}

static ssize_t pcd_read(struct file *file, char __user *buff, size_t count, loff_t *pos) {
	struct device_data *dev = file->private_data;
	char *buffer = dev->buffer;
	pr_info("<%d:%d>Read requested for %zu bytes, current file position %lld\n", MAJOR(file->f_inode->i_rdev), MINOR(file->f_inode->i_rdev), count, *pos);

	if (dev->write_index == 0) {
		pr_info("No data to read\n");
		return 0;
	}
	if (*pos >= dev->size) {
		return 0;
	}
	if (*pos + count > dev->write_index) {
		count = dev->write_index - *pos;
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
	struct device_data *dev = file->private_data;
	char *buffer = dev->buffer;
	pr_info("<%d:%d>Write requested for %zu bytes, current file position %lld\n", MAJOR(file->f_inode->i_rdev), MINOR(file->f_inode->i_rdev), count, *pos);
	if (*pos >= dev->size) {
		// より具体的なエラーメッセージに変更
		pr_err("No space left on device. Current position %lld, device size %zu\n", *pos, dev->size);
		return -ENOSPC;
	}
	if (*pos + count > dev->size) {
		count = dev->size - *pos;
		pr_warn("Partial write: requested %llu bytes, but only %zu bytes available\n",
			count + (*pos + count > dev->size ? (*pos + count - dev->size) : 0) /* 元のcount */, count);
	}
	/* ユーザー空間のバッファからカーネル空間のバッファへコピー */
	if (copy_from_user(buffer + *pos, buff, count)) {
		pr_err("Failed to copy data from user space\n");
		return -EFAULT;
	}
	/* ファイルポジションを更新 */
	*pos += (loff_t)count;
	dev->write_index = *pos;
	pr_info("%zu bytes written, updated file position %lld\n", count, *pos);
	return (ssize_t)count;
}

#define IS_WRONRY(m) (((m) & FMODE_WRITE) && !((m) & FMODE_READ))
#define IS_RONLY(m) (((m) & FMODE_READ) && !((m) & FMODE_WRITE))
#define IS_RDWR(m) (((m) & FMODE_READ) && ((m) & FMODE_WRITE))

static int check_permittion(const struct device_data *dev, const struct file *file) {
	if (IS_RDWR(dev->permission)) {
		return 0;
	}
	if (IS_WRONRY(dev->permission) && IS_WRONRY(file->f_mode)) {
		return 0;
	}
	if (IS_RONLY(dev->permission) && IS_RONLY(file->f_mode)) {
		return 0;
	}
	return -EPERM;
}

static int pcd_open(struct inode *inode, struct file *file) {
	struct device_data *priv = driver.devices[MINOR(inode->i_rdev)];
	int ret = check_permittion(priv, file);
	if (ret < 0) {
		pr_err("Permission denied\n");
		return ret;
	}
	file->private_data = priv;
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

/* デバイスファイル生成時のコールバック */
static char *pcd_devnode(const struct device *device, umode_t *mode) {
	/* 権限を"rw-rw-rw-"へ */
	if (mode && *mode != 0666) {
		/* pr_info("Permission granted<%s:rw-rw-rw->\n", device->init_name); */
		*mode = 0666;
	}
	return NULL;
}

static int __init pcd_driver_init(void) {
	int ret = alloc_chrdev_region(&driver.base_device_number, 0, TOTAL_DEVICES, "pcd_devices");
	if (ret < 0) {
		pr_err("alloc_chrdev_region failed\n");
		return ret;
	}
	pr_info("Device region is <major>%d:<minor>%d-%d",
		MAJOR(driver.base_device_number), MINOR(driver.base_device_number), TOTAL_DEVICES - 1);


	// cdevの初期化とファイル操作関数群を設定する
	cdev_init(&driver.chrdev, &pcd_fopes);
	// デバイスの登録
	driver.chrdev.owner = THIS_MODULE;
	ret = cdev_add(&driver.chrdev, driver.base_device_number, TOTAL_DEVICES);
	if (ret < 0) {
		pr_err("cdev_add failed\n");
		goto unregister_region;
	}

	/* デバイスクラスを生成 */
	driver.class = class_create("pcd_class");
	if (IS_ERR(driver.class)) {
		pr_err("class_create failed\n");
		ret = (int)PTR_ERR(driver.class);
		goto del_cdev;
	}
	driver.class->devnode = pcd_devnode;

	for (int i = 0; i < TOTAL_DEVICES; i++) {
		/* デバイスファイルを生成 */
		driver.devices[i]->dev = device_create(driver.class, NULL, driver.base_device_number + i, NULL, "pcd%d", i);
		pr_info("pcd%d was created\n", i);
	}

	pr_info("Module init was successful\n");
	return 0;
del_cdev:
	cdev_del(&driver.chrdev);
unregister_region:
	unregister_chrdev_region(driver.base_device_number, TOTAL_DEVICES);
	return ret;
}

static void __exit pcd_driver_cleanup(void) {
	for (int i = 0; i < TOTAL_DEVICES; i++) {
		device_destroy(driver.class, driver.base_device_number + i);
		pr_info("pcd%d was destroyed\n", i);
	}
	class_destroy(driver.class);
	cdev_del(&driver.chrdev);
	unregister_chrdev_region(driver.base_device_number, TOTAL_DEVICES);
	pr_info("Module unloaded.\n");
}

module_init(pcd_driver_init);
module_exit(pcd_driver_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ATOHS");
MODULE_DESCRIPTION("Multiple pseudo character driver.");
