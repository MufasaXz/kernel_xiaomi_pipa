// SPDX-License-Identifier: GPL-2.0-only
/* Test keys/data are public and deterministic; use only disposable VM disks. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fscrypt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

/* Keep this test independent of whether the host headers have Android flags. */
struct android_add_key_arg {
	struct fscrypt_key_specifier key_spec;
	unsigned int raw_size;
	unsigned int key_id;
	unsigned int flags;
	unsigned int reserved[6];
	unsigned int android_flags;
	unsigned char raw[64];
};

_Static_assert(offsetof(struct android_add_key_arg, flags) == 48, "flags ABI");
_Static_assert(offsetof(struct android_add_key_arg, android_flags) == 76, "Android ABI");
_Static_assert(offsetof(struct android_add_key_arg, raw) == 80, "raw key ABI");

static void die(const char *operation)
{
	perror(operation);
	exit(1);
}

static void fill(unsigned char *buffer, size_t size, unsigned int seed)
{
	for (size_t i = 0; i < size; i++) {
		seed = seed * 1664525U + 1013904223U;
		buffer[i] = seed >> 24;
	}
}

static void expect_key_error(int fd, struct android_add_key_arg *key,
			     int expected, const char *label)
{
	errno = 0;
	if (ioctl(fd, FS_IOC_ADD_ENCRYPTION_KEY, key) != -1 || errno != expected) {
		fprintf(stderr, "%s: expected errno %d, got %d\n", label, expected, errno);
		exit(1);
	}
}

static void check_wrapped_key_abi(int mountfd)
{
	struct android_add_key_arg key = {
		.key_spec.type = FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER,
		.raw_size = 64,
	};

	fill(key.raw, sizeof(key.raw), 0x50495041);
	key.android_flags = 2;
	expect_key_error(mountfd, &key, EINVAL, "unknown Android flag");
	key.android_flags = 1;
	key.flags = 1;
	expect_key_error(mountfd, &key, EINVAL, "conflicting wrapped-key flags");
	key.flags = 0;
	key.reserved[0] = 1;
	expect_key_error(mountfd, &key, EINVAL, "nonzero reserved field");
	key.reserved[0] = 0;
	key.key_spec.type = FSCRYPT_KEY_SPEC_TYPE_DESCRIPTOR;
	expect_key_error(mountfd, &key, EINVAL, "wrapped descriptor unsupported");
	key.key_spec.type = FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER;
	/* Both recognized formats must reach the unsupported-hardware check. */
	expect_key_error(mountfd, &key, EOPNOTSUPP, "Android wrapped-key flag accepted");
	key.android_flags = 0;
	key.flags = 1;
	expect_key_error(mountfd, &key, EOPNOTSUPP, "upstream wrapped-key flag accepted");
	puts("PASS legacy/upstream wrapped-key ABI validation (no hardware keys)");
}

static void check_file(const char *path, int create, unsigned int seed)
{
	unsigned char expected[4096], actual[4096];
	int fd = open(path, create ? O_WRONLY | O_CREAT | O_TRUNC : O_RDONLY, 0600);

	if (fd < 0)
		die(path);
	for (unsigned int block = 0; block < 256; block++) {
		fill(expected, sizeof(expected), seed + block);
		if (create) {
			if (write(fd, expected, sizeof(expected)) != sizeof(expected))
				die("write test file");
		} else {
			if (read(fd, actual, sizeof(actual)) != sizeof(actual))
				die("read test file");
			if (memcmp(expected, actual, sizeof(expected))) {
				fprintf(stderr, "data mismatch: %s block %u\n", path, block);
				exit(1);
			}
		}
	}
	if (create && fsync(fd))
		die("fsync test file");
	if (close(fd))
		die("close test file");
}

int main(int argc, char **argv)
{
	if (argc != 3 || (strcmp(argv[1], "prepare") && strcmp(argv[1], "verify"))) {
		fprintf(stderr, "usage: %s prepare|verify mountpoint\n", argv[0]);
		return 2;
	}
	int create = !strcmp(argv[1], "prepare");
	int mountfd = open(argv[2], O_RDONLY | O_DIRECTORY);

	if (mountfd < 0)
		die("open mountpoint");
	if (create)
		check_wrapped_key_abi(mountfd);
	struct fscrypt_add_key_arg *key = calloc(1, sizeof(*key) + 64);

	if (!key)
		die("calloc key");
	key->key_spec.type = FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER;
	key->raw_size = 64;
	fill(key->raw, 64, 0x50495041);
	if (ioctl(mountfd, FS_IOC_ADD_ENCRYPTION_KEY, key))
		die("FS_IOC_ADD_ENCRYPTION_KEY");
	char path[4096];

	snprintf(path, sizeof(path), "%s/encrypted", argv[2]);
	if (create && mkdir(path, 0700))
		die("mkdir encrypted");
	int dirfd = open(path, O_RDONLY | O_DIRECTORY);

	if (dirfd < 0)
		die("open encrypted directory");
	struct fscrypt_policy_v2 policy = {
		.version = FSCRYPT_POLICY_V2,
		.contents_encryption_mode = FSCRYPT_MODE_AES_256_XTS,
		.filenames_encryption_mode = FSCRYPT_MODE_AES_256_CTS,
		.flags = FSCRYPT_POLICY_FLAGS_PAD_16,
	};
	memcpy(policy.master_key_identifier, key->key_spec.u.identifier,
	       FSCRYPT_KEY_IDENTIFIER_SIZE);
	if (create && ioctl(dirfd, FS_IOC_SET_ENCRYPTION_POLICY, &policy))
		die("FS_IOC_SET_ENCRYPTION_POLICY");
	close(dirfd);
	for (unsigned int i = 0; i < 16; i++) {
		snprintf(path, sizeof(path), "%s/encrypted/file-%u", argv[2], i);
		check_file(path, create, 0x12345678 + i * 1024);
	}
	snprintf(path, sizeof(path), "%s/plain", argv[2]);
	check_file(path, create, 0xabcdef);
	close(mountfd);
	free(key);
	printf("fscrypt %s passed: 16 encrypted files and one plain file\n", argv[1]);
	return 0;
}
