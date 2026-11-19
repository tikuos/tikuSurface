/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_services_binding.c - bounded service labels from independent typed status fixtures.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "services.h"
#include <interfaces/wireless/tiku_wireless.h>
#include <services/storage/tiku_media_transfer_server.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
static tiku_wireless_observation_t radio;
static tiku_media_transfer_status_t transfer;
static int owner=1,radio_result,transfer_result;
static unsigned queries;
int surface_os_in_owner(void){return owner;}
int tiku_wireless_observe(tiku_wireless_observation_t *out){++queries;*out=radio;return radio_result;}
int tiku_media_transfer_server_status(tiku_media_transfer_status_t *out){++queries;*out=transfer;return transfer_result;}
/** @brief Require exact text and two read-only provider queries for an admitted observation. */
static void expect(const char *expected)
{
    char text[96];unsigned before=queries;
    surface_os_services(text,sizeof text);
    assert(!strcmp(text,expected) && queries==before+2);
}
int main(void)
{
    expect("Wi-Fi unavailable; transfer stopped");
    radio.up=1;expect("Wi-Fi idle; transfer stopped");
    radio.link_state=TIKU_WIRELESS_LINK_CONNECTING;expect("Wi-Fi connecting; transfer stopped");
    radio.link_state=TIKU_WIRELESS_LINK_JOINED;expect("Wi-Fi joined; transfer stopped");
    radio.link_state=TIKU_WIRELESS_LINK_FAILED;expect("Wi-Fi failed; transfer stopped");
    radio.link_state=4;expect("Wi-Fi unavailable; transfer stopped");
    radio.link_state=99;expect("Wi-Fi unavailable; transfer stopped");
    radio.scan_in_progress=1;expect("Wi-Fi scanning; transfer stopped");
    radio.disconnect_pending=1;expect("Wi-Fi disconnecting; transfer stopped");
    radio=(tiku_wireless_observation_t){.up=1,.link_state=TIKU_WIRELESS_LINK_JOINED};
    transfer.active=1;expect("Wi-Fi joined; transfer console ready");
    transfer.port=9000;expect("Wi-Fi joined; transfer listening");
    transfer.connected=1;expect("Wi-Fi joined; transfer connected");
    transfer.uploading=1;transfer.committed=0x100000000ULL;transfer.expected=0x200000000ULL;
    transfer.accepted=transfer.committed+1024;
    expect("Wi-Fi joined; upload committed 4294967296/8589934592");
    transfer.uploading=0;transfer.reading=1;transfer.read_offset=12;transfer.read_length=64;
    expect("Wi-Fi joined; read prepared 12/64");
    transfer.fault=1;expect("Wi-Fi joined; transfer unavailable");
    transfer.fault=0;transfer_result=-1;expect("Wi-Fi joined; transfer unavailable");
    radio_result=-1;expect("Wi-Fi unavailable; transfer unavailable");
    char small[3]={'a','b','c'};surface_os_services(small,2);assert(!strcmp(small,"W") && small[2]=='c');
    unsigned before=queries;surface_os_services(NULL,12);surface_os_services(small,0);
    owner=0;surface_os_services(small,sizeof small);assert(!small[0] && before==queries);
    puts("Service labels: unavailable radio, listener, durable progress, faults and bounds pass");
    return 0;
}
