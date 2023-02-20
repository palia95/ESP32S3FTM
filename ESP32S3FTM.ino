#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_event_loop.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_err.h"

static EventGroupHandle_t s_ftm_event_group;
static const int FTM_REPORT_BIT = BIT0;
static const int FTM_FAILURE_BIT = BIT1;
static wifi_ftm_report_entry_t *s_ftm_report;
static uint8_t s_ftm_report_num_entries;
static uint32_t s_rtt_est, s_dist_est;

#define INTERVAL 10 //scan interval in seconds

const int g_report_lvl =
#ifdef CONFIG_ESP_FTM_REPORT_SHOW_DIAG
    BIT0 |
#endif
#ifdef CONFIG_ESP_FTM_REPORT_SHOW_RTT
    BIT1 |
#endif
#ifdef CONFIG_ESP_FTM_REPORT_SHOW_T1T2T3T4
    BIT2 |
#endif
#ifdef CONFIG_ESP_FTM_REPORT_SHOW_RSSI
    BIT3 |
#endif
0;

//event handler for ESP32
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
  //if (event_id == WIFI_EVENT_FTM_REPORT) {
        wifi_event_ftm_report_t *event = (wifi_event_ftm_report_t *) event_data;

        if (event->status == FTM_STATUS_SUCCESS) {
            s_rtt_est = event->rtt_est;
            s_dist_est = event->dist_est;
            s_ftm_report = event->ftm_report_data;
            s_ftm_report_num_entries = event->ftm_report_num_entries;
            xEventGroupSetBits(s_ftm_event_group, FTM_REPORT_BIT);
        } else {
            if (event->status == FTM_STATUS_UNSUPPORTED){
                      printf("FTM procedure with Peer("MACSTR") failed! (Status - FTM_STATUS_UNSUPPORTED)",
                   MAC2STR(event->peer_mac));
            } else if (event->status == FTM_STATUS_CONF_REJECTED){
                      printf("FTM procedure with Peer("MACSTR") failed! (Status - FTM_STATUS_CONF_REJECTED)",
                   MAC2STR(event->peer_mac));
            } else if (event->status == FTM_STATUS_NO_RESPONSE){
                      printf("FTM procedure with Peer("MACSTR") failed! (Status - FTM_STATUS_NO_RESPONSE)",
                   MAC2STR(event->peer_mac));
            } else if (event->status == FTM_STATUS_FAIL){
                      printf("FTM procedure with Peer("MACSTR") failed! (Status - FTM_STATUS_FAIL)",
                   MAC2STR(event->peer_mac));
            }

            xEventGroupSetBits(s_ftm_event_group, FTM_FAILURE_BIT);
        }
    //}
}

//initializa wifi
void wifiInit() {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(nvs_flash_init());
  //tcpip_adapter_init();
  s_ftm_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    WIFI_EVENT_FTM_REPORT,
                    &event_handler,
                    NULL,
                    NULL));

  wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wifi_config));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());

}

static void ftm_process_report(void)
{
    int i;
    char *log = NULL;

    if (!g_report_lvl)
        return;

    memset(log, 0, 200);
    if (!log) {
        Serial.println("Failed to alloc buffer for FTM report");
        return;
    }

    bzero(log, 200);
    sprintf(log, "%s%s%s%s", g_report_lvl & BIT0 ? " Diag |":"", g_report_lvl & BIT1 ? "   RTT   |":"",
                 g_report_lvl & BIT2 ? "       T1       |       T2       |       T3       |       T4       |":"",
                 g_report_lvl & BIT3 ? "  RSSI  |":"");
    Serial.println("FTM Report:");
    printf("|%s", log);
    for (i = 0; i < s_ftm_report_num_entries; i++) {
        char *log_ptr = log;

        bzero(log, 200);
        if (g_report_lvl & BIT0) {
            log_ptr += sprintf(log_ptr, "%6d|", s_ftm_report[i].dlog_token);
        }
        if (g_report_lvl & BIT1) {
            log_ptr += sprintf(log_ptr, "%7u  |", s_ftm_report[i].rtt);
        }
        if (g_report_lvl & BIT2) {
            log_ptr += sprintf(log_ptr, "%14llu  |%14llu  |%14llu  |%14llu  |", s_ftm_report[i].t1,
                                        s_ftm_report[i].t2, s_ftm_report[i].t3, s_ftm_report[i].t4);
        }
        if (g_report_lvl & BIT3) {
            log_ptr += sprintf(log_ptr, "%6d  |", s_ftm_report[i].rssi);
        }
        printf("|%s", log);
    }
    free(log);
}

void setup() {
  Serial.begin(115200);
  wifiInit();
  Serial.println("ESP32S3 initialization done");
}

void wifi_ftm_req (wifi_ap_record_t ap_record) {
  
    wifi_ftm_initiator_cfg_t ftm_cfg = {
        .frm_count = 32,
        .burst_period = 2
    };  

    Serial.println("Inside FTM req");

    EventBits_t bits;

    printf("Requesting FTM session with Frm Count - %d, Burst Period - %dmSec \n", ftm_cfg.frm_count, ftm_cfg.burst_period*100);

    memcpy(ftm_cfg.resp_mac, ap_record.bssid, 6);
    ftm_cfg.channel = ap_record.primary;
    
    //printf("%X:%X:%X:%X:%X:%X\n", ftm_cfg.resp_mac[0], ftm_cfg.resp_mac[1], ftm_cfg.resp_mac[2], ftm_cfg.resp_mac[3], ftm_cfg.resp_mac[4], ftm_cfg.resp_mac[5]);

    if (ESP_OK != esp_wifi_ftm_initiate_session(&ftm_cfg)) {
        Serial.println("Failed to start FTM session");
    }
    
    Serial.println("ftm req sent");
    bits = xEventGroupWaitBits(s_ftm_event_group, FTM_REPORT_BIT | FTM_FAILURE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    
    /* Processing data from FTM session */
    if (bits & FTM_REPORT_BIT) {
        ftm_process_report();
        free(s_ftm_report);
        s_ftm_report = NULL;
        s_ftm_report_num_entries = 0;
        printf("Estimated RTT - %d nSec, Estimated Distance - %d.%02d meters\n", s_rtt_est, s_dist_est / 100, s_dist_est % 100);
    } else {
        /* Failure case */
        Serial.println("FAILED FTM");
    }
}

//wifi scan function
void wifi_perform_scan(){
  wifi_scan_config_t scan_config = { 0 };
  uint8_t i;
  uint16_t g_scan_ap_num = 0;
  uint8_t ftm_ap_num = 0;

  esp_wifi_scan_start(&scan_config, true);

  esp_wifi_scan_get_ap_num(&g_scan_ap_num);
  if (g_scan_ap_num == 0) {
      Serial.println("No matching AP found");
  }
  
  wifi_ap_record_t g_ap_list_buffer[g_scan_ap_num];
  memset(g_ap_list_buffer, 0, sizeof(g_ap_list_buffer));
  
  if (g_ap_list_buffer == NULL) {
      Serial.println("Failed to alloc buffer to print scan results");
  }
    
  if (esp_wifi_scan_get_ap_records(&g_scan_ap_num, g_ap_list_buffer) == ESP_OK) {
    Serial.println("              SSID             | Channel | RSSI | FTM |         MAC        ");
    Serial.println("***************************************************************************");
    for (i = 0; i < g_scan_ap_num; i++) {
        printf("%30s | %7d | %4d | %3d | %02X:%02X:%02X:%02X:%02X:%02X \n", (char *)g_ap_list_buffer[i].ssid, g_ap_list_buffer[i].primary, g_ap_list_buffer[i].rssi, g_ap_list_buffer[i].ftm_responder, g_ap_list_buffer[i].bssid[0], g_ap_list_buffer[i].bssid[1], g_ap_list_buffer[i].bssid[2], g_ap_list_buffer[i].bssid[3], g_ap_list_buffer[i].bssid[4], g_ap_list_buffer[i].bssid[5]);
        if (g_ap_list_buffer[i].ftm_responder == 1){
          ftm_ap_num++;  
          wifi_ftm_req(g_ap_list_buffer[i]);
        }
    }
    Serial.println("***************************************************************************");
    Serial.printf("%d FTM SSID found\n", ftm_ap_num);  
  }  

  Serial.println("WiFi scan done");
}

void loop() {
    Serial.println("Scanning");
    wifi_perform_scan();
    xEventGroupClearBits(s_ftm_event_group, FTM_REPORT_BIT);
    delay(INTERVAL*1000);
    Serial.println(ESP.getFreeHeap());
}
