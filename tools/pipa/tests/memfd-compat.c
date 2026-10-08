// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/* Android's public ashmem ioctl numbers; usable without kernel headers. */
#define ASHMEM_NAME_LEN 256
#define ASHMEM_GET_NAME _IOR(0x77, 2, char[ASHMEM_NAME_LEN])
#define ASHMEM_SET_SIZE _IOW(0x77, 3, size_t)
#define ASHMEM_GET_SIZE _IO(0x77, 4)
#define ASHMEM_SET_PROT_MASK _IOW(0x77, 5, unsigned long)
#define ASHMEM_GET_PROT_MASK _IO(0x77, 6)
#define ASHMEM_GET_FILE_ID _IOR(0x77, 11, unsigned long)

static void check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL memfd: %s (errno=%d)\n", message, errno);
		exit(EXIT_FAILURE);
	}
}

int main(void)
{
	char name[ASHMEM_NAME_LEN] = { 0 };
	unsigned long id = 0;
	char *mapping, *new_mapping;
	int fd = memfd_create("pipa-compat", MFD_CLOEXEC | MFD_ALLOW_SEALING);

	check(fd >= 0, "create");
	check(ftruncate(fd, 4096) == 0, "size");
	check(fcntl(fd, F_ADD_SEALS, F_SEAL_GROW | F_SEAL_SHRINK) == 0,
	      "seal size like Android libcutils");
	check(ioctl(fd, ASHMEM_GET_SIZE) == 4096, "legacy size");
	check(ioctl(fd, ASHMEM_GET_NAME, name) == 0 &&
	      strcmp(name, "pipa-compat") == 0, "legacy name");
	check(ioctl(fd, ASHMEM_GET_FILE_ID, &id) == 0 && id != 0,
	      "legacy file identifier");
	errno = 0;
	check(ioctl(fd, ASHMEM_GET_NAME, NULL) == -1 && errno == EFAULT,
	      "reject invalid name pointer");
	errno = 0;
	check(ioctl(fd, ASHMEM_SET_SIZE, 8192UL) == -1 && errno == EINVAL,
	      "reject resizing through legacy ioctl");
	errno = 0;
	check(ioctl(fd, _IO(0x77, 127)) == -1 && errno == ENOTTY,
	      "reject unknown ioctl");
	check(ioctl(fd, ASHMEM_GET_PROT_MASK) == (PROT_READ | PROT_WRITE | PROT_EXEC),
	      "initial permissions");
	mapping = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	check(mapping != MAP_FAILED, "initial writable map");
	check(ioctl(fd, ASHMEM_SET_PROT_MASK, (unsigned long)PROT_READ) == 0,
	      "remove future write permission");
	check(fcntl(fd, F_GET_SEALS) & F_SEAL_FUTURE_WRITE, "future write seal");
	check(!(ioctl(fd, ASHMEM_GET_PROT_MASK) & PROT_WRITE), "sealed permissions");
	mapping[0] = 42;
	check(mapping[0] == 42, "existing writable mapping remains usable");
	errno = 0;
	new_mapping = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	check(new_mapping == MAP_FAILED && errno == EPERM, "reject new writable map");
	errno = 0;
	check(ioctl(fd, ASHMEM_SET_PROT_MASK, (unsigned long)(PROT_READ | PROT_WRITE)) == -1 &&
	      errno == EINVAL, "reject restoring write permission");
	check(munmap(mapping, 4096) == 0 && close(fd) == 0, "cleanup");
	puts("PASS memfd ashmem compatibility: name, size, file ID and write sealing");
	return 0;
}
