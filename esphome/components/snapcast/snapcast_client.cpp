#include "snapcast_client.h"
#include "messages.h"

#include "esphome/core/log.h"

#include "esp_transport.h"
#include "esp_transport_tcp.h"

#include "esp_mac.h"
#include "mdns.h"
#include <cstring> 

namespace esphome {
namespace snapcast {

static const char *const TAG = "snapcast_client";


void SnapcastClient::setup(){
    this->connect_via_mdns();
    this->cntrl_session_.set_on_stream_update([this](const StreamInfo &info) {
        this->on_stream_update(info);
    });

}


error_t SnapcastClient::connect_to_server(std::string url, uint32_t port){
    return ESP_OK;
}

void SnapcastClient::on_stream_update(const StreamInfo &info){
  ESP_LOGI(TAG, "Stream updated: status=%s", info.status.c_str());

  if (this->media_player_ != nullptr) {
    if (info.status == "playing" && !this->stream_.is_running()) {
      this->media_player_->play_snapcast_stream("bla");
    }
    else if ( info.status != "playing" && this->stream_.is_running() ) {
      this->media_player_->make_call().set_command(media_player::MediaPlayerCommand::MEDIA_PLAYER_COMMAND_STOP).perform();
      this->stream_.stop_streaming();
    } 
/*    
    else if (info.status == "paused") {
      this->media_player_->pause();
    } else if (info.status == "idle"){
       this->media_player_->stop(); 
    }
*/
  }
}


static const char * if_str[] = {"STA", "AP", "ETH", "MAX"};
static const char * ip_protocol_str[] = {"V4", "V6", "MAX"};

static void mdns_print_results(mdns_result_t *results)
{
    mdns_result_t *r = results;
    mdns_ip_addr_t *a = nullptr;
    int i = 1, t;
    while (r) {
        if (r->esp_netif) {
            printf("%d: Interface: %s, Type: %s, TTL: %" PRIu32 "\n", i++, esp_netif_get_ifkey(r->esp_netif),
                   ip_protocol_str[r->ip_protocol], r->ttl);
        }
        if (r->instance_name) {
            printf("  PTR : %s.%s.%s\n", r->instance_name, r->service_type, r->proto);
        }
        if (r->hostname) {
            printf("  SRV : %s.local:%u\n", r->hostname, r->port);
        }
        if (r->txt_count) {
            printf("  TXT : [%zu] ", r->txt_count);
            for (t = 0; t < r->txt_count; t++) {
                printf("%s=%s(%d); ", r->txt[t].key, r->txt[t].value ? r->txt[t].value : "NULL", r->txt_value_len[t]);
            }
            printf("\n");
        }
        a = r->addr;
        while (a) {
            if (a->addr.type == ESP_IPADDR_TYPE_V6) {
                printf("  AAAA: " IPV6STR "\n", IPV62STR(a->addr.u_addr.ip6));
            } else {
                printf("  A   : " IPSTR "\n", IP2STR(&(a->addr.u_addr.ip4)));
            }
            a = a->next;
        }
        r = r->next;
    }
}



error_t SnapcastClient::connect_via_mdns(){
    
    mdns_result_t * results = nullptr;
    esp_err_t err = mdns_query_ptr( "_snapcast", "_tcp", 3000, 20,  &results);
    if(err){
        ESP_LOGE(TAG, "Query Failed");
        return ESP_OK;
    }
    if(!results){
        ESP_LOGW(TAG, "No results found!");
        return ESP_OK;
    }
    
    mdns_print_results(results);
    
    std::string ma_snapcast_hostname;
    uint32_t port = 0;
    mdns_result_t *r = results;
    while(r){
        if (r->txt_count) {
            for (int t = 0; t < r->txt_count; t++) {
                if( strcmp(r->txt[t].key, "is_mass") == 0){
                    ma_snapcast_hostname = std::string(r->hostname) + ".local";
                    port = r->port;
                    ESP_LOGI(TAG, "MA-Snapcast server found: %s:%d", ma_snapcast_hostname.c_str(), port );
                }
            }
        }
        r = r->next;
    }
    mdns_query_results_free(results);

    if( !ma_snapcast_hostname.empty() ){
        this->stream_.connect( ma_snapcast_hostname, port );
        this->cntrl_session_.connect( ma_snapcast_hostname, 1705);
    }
    
    return ESP_OK;
}





}
}