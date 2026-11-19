/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_radio_observation.c - GUI radio observation without the vendor RSSI path.
 * SPDX-License-Identifier: Apache-2.0
 */
#define TIKU_WIRELESS_OBSERVE_ENABLE 1
static int interrupt_context;
int espw_arch_in_isr(void) { return interrupt_context; }
#define main transmit_fixture_main
#include <TikuBench/tests/host/kernel/test_esp_wifi_tx.c>
#undef main
#include "services.h"
#include <services/storage/tiku_media_transfer_server.h>
#include <stdlib.h>

/** @brief Trap hardware callbacks retained by sanitizer global registration. */
esp_err_t esp_wifi_clear_ap_list(void) { abort(); }
esp_err_t esp_wifi_connect_internal(void) { abort(); }
esp_err_t esp_wifi_disable_pmf_config(wifi_interface_t interface) { abort(); }
esp_err_t esp_wifi_disconnect_internal(void) { abort(); }
void esp_wifi_internal_free_rx_buffer(void *buffer) { abort(); }
esp_err_t esp_wifi_internal_reg_rxcb(wifi_interface_t interface,wifi_rxcb_t callback) { abort(); }
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number) { abort(); }
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number,wifi_ap_record_t *records) { abort(); }
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config,bool block) { abort(); }
esp_err_t esp_wifi_set_config(wifi_interface_t interface,wifi_config_t *config) { abort(); }
void espw_core_down(uint8_t radio) { abort(); }
void espw_free(void *pointer) { abort(); }
void espw_heap_ext_stats(espw_heap_stats_t *out) { abort(); }
void espw_heap_stats(espw_heap_stats_t *out) { abort(); }
void *espw_malloc(size_t bytes) { abort(); }
void espw_modem_wifi_clock_off(void) { abort(); }
void espw_modem_wifi_inited(int on) { abort(); }
int espw_read_mac(uint8_t *mac,unsigned type) { abort(); }
void espw_wpa_set_pmk(const uint8_t *pmk) { abort(); }
void espw_wpa_unregister(void) { abort(); }
int tiku_kits_crypto_pbkdf2_hmac_sha1(const uint8_t *pass,size_t pass_len,
    const uint8_t *salt,size_t salt_len,uint32_t iterations,uint8_t *out,size_t out_len) { abort(); }
uint8_t tiku_process_post(struct tiku_process *process,tiku_event_t event,tiku_event_data_t data) { abort(); }
int8_t tiku_process_register(const char *name,struct tiku_process *process) { abort(); }

uint32_t espw_irq_count(void) { return 0; }
esp_err_t esp_wifi_sta_get_rssi(int *rssi)
{ (void)rssi;assert(!"GUI observation reached the vendor RSSI lock");return ESP_FAIL; }
int surface_os_in_owner(void) { return kernel&&!interrupt_context; }
int tiku_media_transfer_server_status(tiku_media_transfer_status_t *out)
{ memset(out,0,sizeof *out);return 0; }

/** @brief Check real cached driver state, owner refusal and the joined GUI path. */
int main(void)
{
    kernel=1;espw_up=1;espw_link=TIKU_WIRELESS_LINK_JOINED;
    char label[96];surface_os_services(label,sizeof label);
    assert(!strcmp(label,"Wi-Fi joined; transfer stopped"));
    tiku_wireless_observation_t observed;
    assert(tiku_wireless_observe(NULL)==TIKU_DRV_ERR_INVALID);
    for(unsigned up=0;up<2;up++)for(unsigned scan=0;scan<2;scan++)for(unsigned link=0;link<4;link++){
        espw_up=(uint8_t)up;espw_scanning=(uint8_t)scan;espw_link=(uint8_t)link;
        assert(!tiku_wireless_observe(&observed));
        assert(observed.up==up&&observed.scan_in_progress==scan&&observed.link_state==link&&!observed.disconnect_pending);
        assert(espw_up==up&&espw_scanning==scan&&espw_link==link);
    }
    for(unsigned context=0;context<2;context++){
        kernel=context;interrupt_context=context;memset(&observed,0xff,sizeof observed);
        assert(tiku_wireless_observe(&observed)==TIKU_DRV_ERR_TIMEOUT);
        assert(!observed.up&&!observed.scan_in_progress&&!observed.link_state&&!observed.disconnect_pending);
    }
    assert(!waits&&!calls);
    puts("Radio observation: cached flags, owner refusal and joined GUI without vendor calls pass");
    return 0;
}
