/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * desktop_hash.c - report a desktop file's digest with the firmware SHA-256.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../desktop/bridge_os.h"
#include <tikukits/crypto/sha256/tiku_kits_crypto_sha256.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

void surface_desktop_hash_file(const char *path)
{
    tiku_kits_crypto_sha256_ctx_t hash;
    unsigned char buf[1024], digest[TIKU_KITS_CRYPTO_SHA256_DIGEST_SIZE];
    unsigned long long total = 0;
    size_t n;
    int error = 0;
    struct stat st;
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        fprintf(stderr, "hash %s: errno=%d\n", path, errno);
        return;
    }
    if (fstat(fileno(file), &st) < 0) error = errno;
    else if (!S_ISREG(st.st_mode)) error = S_ISDIR(st.st_mode) ? EISDIR : EINVAL;
    tiku_kits_crypto_sha256_init(&hash);
    while (!error && (n = fread(buf, 1, sizeof buf, file)) != 0) {
        tiku_kits_crypto_sha256_update(&hash, buf, n);
        total += n;
    }
    if (ferror(file)) error = errno ? errno : EIO;
    if (fclose(file) != 0 && !error) error = errno ? errno : EIO;
    if (error) {
        fprintf(stderr, "hash %s: errno=%d\n", path, error);
        return;
    }
    tiku_kits_crypto_sha256_final(&hash, digest);
    fprintf(stderr, "hash %s: bytes=%llu sha256=", path, total);
    for (size_t i = 0; i < sizeof digest; i++) fprintf(stderr, "%02x", digest[i]);
    fprintf(stderr, "\n");
}
