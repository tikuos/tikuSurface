/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * recorder_check.c - Verify Sound Recorder files on the P4X desktop worker.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bridge.h"
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned requested;

void p4x_check_recordings(void)
{
    __atomic_store_n(&requested, 1, __ATOMIC_RELEASE);
}

static uint32_t le32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/** @brief Read one PCM16 mono recording and check its header against its contents. */
static unsigned check_wav(const char *path)
{
    static const uint8_t format[] = {
        'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0,
        1, 0, 1, 0, 0x80, 0x3e, 0, 0, 0, 0x7d, 0, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a'
    };
    uint8_t head[44];
    int16_t block[256];
    int lo = INT16_MAX, hi = INT16_MIN;
    uint32_t samples = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fread(head, 1, sizeof head, f) == sizeof head &&
        !memcmp(head, "RIFF", 4) && !memcmp(head + 8, format, sizeof format)) {
        size_t n;
        while ((n = fread(block, sizeof *block, 256, f)) != 0) {
            samples += n;
            for (size_t i = 0; i < n; i++) {
                if (block[i] < lo) lo = block[i];
                if (block[i] > hi) hi = block[i];
            }
        }
        if (ferror(f) || ftell(f) != (long)(sizeof head + samples * 2u) ||
            lo >= hi || le32(head + 40) != samples * 2u ||
            le32(head + 4) != 36u + samples * 2u) samples = 0;
    }
    fclose(f);
    return samples;
}

void __real_surface_recorder_tick(int64_t now);
void __wrap_surface_recorder_tick(int64_t now)
{
    __real_surface_recorder_tick(now);
    if (!__atomic_load_n(&requested, __ATOMIC_ACQUIRE)) return;
    __atomic_store_n(&requested, 0, __ATOMIC_RELAXED);
    unsigned count = 0, valid = 0, samples = 0;
    const char *folder = "/home/Recordings";
    DIR *dir = opendir(folder);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            size_t len = strlen(entry->d_name);
            if (len < 5 || strcmp(entry->d_name + len - 4, ".wav")) continue;
            count++;
            char path[120];
            int length = snprintf(path, sizeof path, "%s/%s", folder, entry->d_name);
            if (length < 0 || (size_t)length >= sizeof path) continue;
            unsigned n = check_wav(path);
            if (n) valid++;
            samples += n;
        }
        closedir(dir);
    }
    char report[64];
    int n = snprintf(report, sizeof report, "WAV %u %u %u\n", count, valid, samples);
    surface_desktop_log(report, n);
}
