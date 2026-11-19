/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * services.c - cached radio and transfer observations for device information.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "services.h"
#include "runtime.h"
#include <interfaces/wireless/tiku_wireless.h>
#include <services/storage/tiku_media_transfer_server.h>
#include <stdio.h>

void surface_os_services(char *out, size_t size)
{
    if (!out || !size) return;
    if (!surface_os_in_owner()) { out[0]=0; return; }
    tiku_wireless_observation_t radio={0};
    const char *wifi="unavailable";
    if (!tiku_wireless_observe(&radio) && radio.up) {
        static const char *const names[]={"idle","connecting","joined","failed"};
        if (radio.disconnect_pending) wifi="disconnecting";
        else if (radio.scan_in_progress) wifi="scanning";
        else if (radio.link_state<sizeof names/sizeof names[0]) wifi=names[radio.link_state];
    }
    tiku_media_transfer_status_t transfer;
    int rc=tiku_media_transfer_server_status(&transfer);
    if (rc || transfer.fault) snprintf(out,size,"Wi-Fi %s; transfer unavailable",wifi);
    else if (transfer.uploading) snprintf(out,size,"Wi-Fi %s; upload committed %llu/%llu",wifi,
        (unsigned long long)transfer.committed,(unsigned long long)transfer.expected);
    else if (transfer.reading) snprintf(out,size,"Wi-Fi %s; read prepared %llu/%llu",wifi,
        (unsigned long long)transfer.read_offset,(unsigned long long)transfer.read_length);
    else snprintf(out,size,"Wi-Fi %s; transfer %s",wifi,!transfer.active ? "stopped" :
        !transfer.port ? "console ready" : transfer.connected ? "connected" : "listening");
}
