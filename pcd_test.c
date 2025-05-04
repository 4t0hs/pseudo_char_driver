#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#define DEV_NAME "/dev/pcd"

void write_to_pcd(const char *msg) {
	int fd = open(DEV_NAME, O_WRONLY);
	if (fd < 0) {
		perror("open");
		return;
	}
	size_t len = strlen(msg);
	if (write(fd, msg, len) != len) {
		perror("write");
		return;
	}
	printf("wrote %zu bytes\n", len);
	close(fd);
}

void read_from_pcd() {
	int fd = open(DEV_NAME, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return;
	}
	char buf[128] = { 0 };
	if (read(fd, buf, sizeof(buf)) <= 0) {
		perror("read");
		return;
	}
	printf("read: %s\n", buf);
	close(fd);
}

int main() {
	write_to_pcd("hello world");
	read_from_pcd();
	return 0;
}
