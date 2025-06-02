#include "snapcast_rpc.h"
#include "esp_transport_tcp.h"

#include "esphome/core/log.h"
#include "esphome/components/network/util.h"

namespace esphome {
namespace snapcast {

static const char *const TAG = "snapcast_rpc";

esp_err_t SnapcastControlSession::connect(std::string server, uint32_t port){
    this->server_ = server;
    this->port_ = port;
    
    // Start the notification handling task
    this->notification_task_should_run_ = true;
    xTaskCreate([](void *param) {
        auto *session = static_cast<SnapcastControlSession *>(param);
        session->notification_loop();
        vTaskDelete(nullptr);
    }, "snapcast_notify", 4096, this, 5, &this->notification_task_handle_);

    return ESP_OK;
}

esp_err_t SnapcastControlSession::disconnect(){
    if( this->transport_ != nullptr ){
        esp_transport_close(this->transport_);
        esp_transport_destroy(this->transport_);
        this->transport_ = nullptr;
        this->notification_task_should_run_ = false;
        vTaskDelay(pdMS_TO_TICKS(50));

        // Then delete the task if still running
        if (this->notification_task_handle_ != nullptr) {
            vTaskDelete(this->notification_task_handle_);
            this->notification_task_handle_ = nullptr;
        }
    }
    return ESP_OK;
}


void SnapcastControlSession::update_from_server_obj_(const JsonObject &server_obj){
    ClientState &state = this->client_state_;
    if( state.from_groups_json( server_obj["groups"], get_mac_address_pretty()) ){
        printf( "group_id: %s stream_id: %s\n", state.group_id.c_str(), state.stream_id.c_str() );
    }
    StreamInfo sInfo;
    if( sInfo.from_streams_json( server_obj["streams"], state.stream_id )){
        printf( "stream: %s state: %s\n", sInfo.id.c_str(), sInfo.status.c_str() );
        if (this->on_stream_update_) {
            this->on_stream_update_(sInfo);
        }
    }
}

void SnapcastControlSession::notification_loop() {
  
  // Initialize transport
  this->transport_ = esp_transport_tcp_init();
  if (this->transport_ == nullptr) {
    ESP_LOGE(TAG, "Failed to initialize transport");
    return;
  }

  // Try to connect
  error_t err = esp_transport_connect(this->transport_, this->server_.c_str(), this->port_, -1);
  if (err != 0) {
    ESP_LOGE(TAG, "Connection failed with error: %d", errno);
    return;
  }

  // Send initial request after connecting
  this->send_rpc_request_("Server.GetStatus",
    [](JsonObject params) {
      // no params
    },
    static_cast<uint32_t>(RequestId::GetServerStatus)
  );  

  while (this->notification_task_should_run_) {
    char chunk[128];  // small read buffer
    int len = esp_transport_read(this->transport_, chunk, sizeof(chunk), 100);
    if (len > 0) {
        this->recv_buffer_.append(chunk, len);
        size_t pos;
        while ((pos = this->recv_buffer_.find('\n')) != std::string::npos) {
            std::string json_line = this->recv_buffer_.substr(0, pos);
            printf( "JSON: %s\n", json_line.c_str() );
            this->recv_buffer_.erase(0, pos + 1);

            json::parse_json(json_line, [this](JsonObject root) -> bool {
                if(root.containsKey("result")){
                   uint32_t id = root["id"]; 
                   switch (static_cast<RequestId>(id)) {
                        case RequestId::GetServerStatus:
                            {
                                ClientState &state = this->client_state_;
                                if( state.from_groups_json( root["result"]["server"]["groups"], get_mac_address_pretty()) ){
                                    printf( "group_id: %s stream_id: %s\n", state.group_id.c_str(), state.stream_id.c_str() );
                                }
                                StreamInfo sInfo;
                                if( sInfo.from_streams_json( root["result"]["server"]["streams"], state.stream_id )){
                                    printf( "stream: %s state: %s\n", sInfo.id.c_str(), sInfo.status.c_str() );
                                }
                            }
                            break;
                        default:
                            ESP_LOGW(TAG, "Unknown request ID: %u", id);
                    }           

                } else if(root.containsKey("method")) {
                    std::string method = root["method"].as<std::string>();    
                    if (method == "Server.OnUpdate" ){
                        this->update_from_server_obj_(root["params"]["server"].as<JsonObject>());
                    } else if( method == "Stream.OnUpdate"){
                       
                       JsonObject params = root["params"];
                       if(params["id"].as<std::string>() == this->client_state_.stream_id){
                            StreamInfo sInfo;
                            if( sInfo.from_json( params["stream"])){
                                printf( "stream: %s state: %s\n", sInfo.id.c_str(), sInfo.status.c_str() );
                            }
                            if (this->on_stream_update_) {
                                 this->on_stream_update_(sInfo);
                            }
                       } else {
                        printf( "got id: %s, requested: %s\n", params["id"].as<std::string>().c_str(), this->client_state_.stream_id);
                       }
                    } else if (method == "Stream.OnProperties"){
                       JsonObject params = root["params"];
                       if(params["id"].as<std::string>() == this->client_state_.stream_id){
                            StreamInfo sInfo;
                            if( sInfo.from_stream_properties( params["properties"])){
                                printf( "stream: %s state: %s\n", sInfo.id.c_str(), sInfo.status.c_str() );
                            }
                            if (this->on_stream_update_) {
                                 this->on_stream_update_(sInfo);
                            }
                        }
                    }    
                }
                return true;
            });
        }
    }
  }
}

                    /*
                        {"jsonrpc":"2.0", "method":"Stream.OnProperties", 
                            "params": {
                                "id":"Music Assistant - satellite14cc8ec",
                                "properties": {
                                    "canControl":true,
                                    "canGoNext":true,
                                    "canGoPrevious":true,
                                    "canPause":true,
                                    "canPlay":true,
                                    "canSeek":true,
                                    "loopStatus":"none",
                                    "metadata": {"album":"25 Blarney Roses","albumSort":"25 blarney roses","artUrl":"http://192.168.178.57:8098/imageproxy?path=https%3A%2F%2Fi.scdn.co%2Fimage%2Fab67616d0000b273907dba34e0f4dd35ca73a36b&provider=spotify--QTvNApMa&size=512","artist":["Mark Twain","Bettina Reifschneider","Fiddler's Green"],"artistSort":["mark twain","bettina reifschneider","fiddler's green"],"duration":270.0,"title":"Greens and Fellows","trackId":"library://track/3228"},
                                    "mute":false,
                                    "playbackStatus":"playing",
                                    "position":125.1409683227539,
                                    "rate":1.0,
                                    "shuffle":false,
                                    "volume":0
                                }
                            }
                        }
                    */



/*
{"id":1,"jsonrpc":"2.0","result":{
    "server":{
        "groups":[ { 
                "clients":
                [{
                    "config": {
                        "instance":1,
                        "latency":0,
                        "name":"",
                        "volume": {"muted":false,"percent":25}
                    },
                    "connected":false,
                    "host":{
                        "arch":"web",
                        "ip":"192.168.178.101",
                        "mac":"00:00:00:00:00:00",
                        "name":"Snapweb client",
                        "os": "MacIntel"
                    },
                    "id":"de83cd49-17ed-4044-a262-3b15a16f317c",
                    "lastSeen":{ "sec":1748677822,"usec":974526},
                    "snapclient": {
                        "name":"snapweb",
                        "protocolVersion":2,
                        "version":"0.8.0"
                    }
                }],
                "id":"d9fb277b-fd9b-6b02-b7c1-82acdd0148a1",
                "muted":false,
                "name":"",
                "stream_id":"Music Assistant - satellite1local"
                }, 
            {
                "clients":
                [{ 
                    "config": {
                        "instance":1,
                        "latency":0,
                        "name":"",
                        "volume":{"muted":false,"percent":31}
                    },
                    "connected":false,
                    "host":{ 
                            "arch":"ESP32-S3",
                            "ip":"192.168.178.133",
                            "mac":"D8:3B:DA:4E:19:F0",
                            "name":"satellite1-4e19f0",
                            "os":"FutureProofHomes"
                    },
                    "id":"D8:3B:DA:4E:19:F0",
                    "lastSeen": {"sec":1748765743,"usec":300255},
                    "snapclient": {
                        "name":"Satellite1 4e19f0",
                        "protocolVersion":2,
                        "version": "0.17.1"
                    }
                },{
                    "config": { 
                        "instance":1,
                        "latency":0,
                        "name":"",
                        "volume":{"muted":false,"percent":30}
                    },
                    "connected":true,
                    "host":{
                        "arch":"ESP32-S3",
                        "ip":"192.168.178.129",
                        "mac":"D8:3B:DA:4E:1A:90",
                        "name":"satellite1-4e1a90",
                        "os":"FutureProofHomes"
                    },
                    "id":"D8:3B:DA:4E:1A:90",
                    "lastSeen":{"sec":1748794838,"usec":907154},
                    "snapclient":{
                        "name":"Satellite1 4e1a90",
                        "protocolVersion":2,
                        "version":"0.17.1"
                    }
                }],
                "id":"a9697c0b-ff81-6452-f11b-683eaa0e177c",
                "muted":false,
                "name":"",
                "stream_id":"Music Assistant - satellite14e19f0"}
                ],
                "server": { 
                    "host": {
                        "arch":"aarch64",
                        "ip":"",
                        "mac":"",
                        "name":"rasp4home",
                        "os":"Alpine Linux v3.21"
                    }, 
                    "snapserver": {
                        "controlProtocolVersion":1,
                        "name":"Snapserver",
                        "protocolVersion":1,
                        "version": "0.29.0"
                    }
                },
                "streams": [{ 
                    "id":"default",
                    "properties":{ 
                        "canControl":false,
                        "canGoNext":false,
                        "canGoPrevious":false,
                        "canPause":false,
                        "canPlay":false,
                        "canSeek":false},
                        "status":"idle",
                        "uri":{ 
                            "fragment":"",
                            "host":"",
                            "path":"/tmp/snapfifo",
                            "query": { 
                                "chunk_ms":"30",
                                "codec":"flac",
                                "name":"default",
                                "sampleformat":"48000:16:2"
                            },
                            "raw": "pipe:////tmp/snapfifo?chunk_ms=30&codec=flac&name=default&sampleformat=48000:16:2",
                            "scheme":"pipe"
                        }
                    },{
                        "id":"Music Assistant - satellite14e1a90",
                        "properties": { 
                            "canControl":true,
                            "canGoNext":true,
                            "canGoPrevious":true,
                            "canPause":true,
                            "canPlay":true,
                            "canSeek":true,
                            "loopStatus":"none",
                            "metadata": {
                                "album":"Atomic",
                                "albumSort":"atomic",
                                "artUrl":"http://192.168.178.57:8097/imageproxy?path=https%3A%2F%2Fi.scdn.co%2Fimage%2Fab67616d0000b27367e9865ae62ca74cfc9e3ee4&provider=spotify--QTvNApMa&size=512",
                                "artist":["Mark Twain","Bettina Reifschneider","Mogwai"],
                                "artistSort":["mark twain","bettina reifschneider","mogwai"],
                                "duration":291.0,
                                "title":"Bitterness Centrifuge","trackId":"library://track/3212"},
                                "mute":false,
                                "playbackStatus":
                                "playing","position":0.12323290854692459,
                                "rate":1.0,
                                "shuffle":false,
                                "volume":0
                            },"status":"idle",
                            "uri":{
                                "fragment":"",
                                "host":"0.0.0.0:5084",
                                "path":"",
                                "query":{
                                    "chunk_ms":"30",
                                    "codec":"flac",
                                    "controlscript":"/app/venv/lib/python3.13/site-packages/music_assistant/providers/snapcast/control.py",
                                    "controlscriptparams":"--queueid=syncgroup_pjybcffe --api-port=8095 --streamserver-ip=192.168.178.57 --streamserver-port=8097",
                                    "idle_threshold":"60000",
                                    "name":"Music Assistant - satellite14e1a90",
                                    "sampleformat":"48000:16:2"
                                },
                                "raw":"tcp://0.0.0.0:5084/?chunk_ms=30&codec=flac&controlscript=/app/venv/lib/python3.13/site-packages/music_assistant/providers/snapcast/control.py&controlscriptparams=--queueid=syncgroup_pjybcffe --api-port=8095 --streamserver-ip=192.168.178.57 --streamserver-port=8097&idle_threshold=60000&name=Music Assistant - satellite14e1a90&sampleformat=48000:16:2",
                                "scheme":"tcp"
                            }
                        },{
                            "id":"Music Assistant - satellite14e19f0",
                            "properties":{
                                "canControl":true,
                                "canGoNext":true,
                                "canGoPrevious":true,
                                "canPause":true,
                                "canPlay":true,
                                "canSeek":true,
                                "loopStatus":"none",
                                "metadata": { 
                                    "album":"Atomic",
                                    "albumSort":"atomic",
                                    "artUrl":"http://192.168.178.57:8097/imageproxy?path=https%3A%2F%2Fi.scdn.co%2Fimage%2Fab67616d0000b27367e9865ae62ca74cfc9e3ee4&provider=spotify--QTvNApMa&size=512",
                                    "artist":["Mark Twain","Bettina Reifschneider","Mogwai"],
                                    "artistSort":["mark twain","bettina reifschneider","mogwai"],
                                    "duration":291.0,
                                    "title":"Bitterness Centrifuge","trackId":"library://track/3212"},
                                    "mute":false,
                                    "playbackStatus":"playing",
                                    "position":0.12323290854692459,
                                    "rate":1.0,
                                    "shuffle":false,
                                    "volume":0
                                },
                                "status":"playing",
                                "uri":{
                                    "fragment":"",
                                    "host":"0.0.0.0:4991",
                                    "path":"",
                                    "query": {
                                        "chunk_ms":"30",
                                        "codec":"flac",
                                        "controlscript":"/app/venv/lib/python3.13/site-packages/music_assistant/providers/snapcast/control.py",
                                        "controlscriptparams":"--queueid=syncgroup_pjybcffe --api-port=8095 --streamserver-ip=192.168.178.57 --streamserver-port=8097",
                                        "idle_threshold":"60000",
                                        "name":"Music Assistant - satellite14e19f0",
                                        "sampleformat":"48000:16:2"
                                    },
                                    "raw":"tcp://0.0.0.0:4991/?chunk_ms=30&codec=flac&controlscript=/app/venv/lib/python3.13/site-packages/music_assistant/providers/snapcast/control.py&controlscriptparams=--queueid=syncgroup_pjybcffe --api-port=8095 --streamserver-ip=192.168.178.57 --streamserver-port=8097&idle_threshold=60000&name=Music Assistant - satellite14e19f0&sampleformat=48000:16:2",
                                    "scheme":"tcp"
                                }
                            }]}}}

*/



void SnapcastControlSession::handle_json_rpc(JsonObject root) {
  if (!root.containsKey("jsonrpc")) return;

  if (root.containsKey("method")) {
    std::string method = root["method"].as<std::string>();

    if (method == "Client.OnConnect") {
      ESP_LOGI(TAG, "Client.OnConnect received");
    } else if (method == "Server.OnUpdate") {
      ESP_LOGI(TAG, "Server.OnUpdate received");
    } else {
      ESP_LOGW(TAG, "Unhandled RPC method: %s", method.c_str());
    }
  } else if (root.containsKey("id") && root.containsKey("result")) {
    uint32_t id = root["id"].as<uint32_t>();
    ESP_LOGI(TAG, "Received response to RPC ID %u", id);
  }
}


void SnapcastControlSession::send_rpc_request_(const std::string &method, std::function<void(JsonObject)> fill_params, uint32_t id) {
  StaticJsonDocument<512> doc;
  doc["jsonrpc"] = "2.0";
  doc["id"] = id;
  doc["method"] = method;
  JsonObject params = doc.createNestedObject("params");
  fill_params(params);

  char json_buf[512];
  size_t len = serializeJson(doc, json_buf, sizeof(json_buf));
  json_buf[len++] = '\n';
  esp_transport_write(this->transport_, json_buf, len, 1000);
}


}
}


                       /* {"jsonrpc":"2.0","method":"Stream.OnUpdate",
                                "params": {  
                                            "id":"Music Assistant - satellite14e19f0",
                                            "stream": {
                                                "id":"Music Assistant - satellite14e19f0",
                                                "properties" : { 
                                                    "canControl":true,
                                                    "canGoNext":false,
                                                    "canGoPrevious":true,
                                                    "canPause":true,
                                                    "canPlay":true,
                                                    "canSeek":true,
                                                    "loopStatus":"none",
                                                    "metadata":  {
                                                        "album": "The Lord of the Rings: The Two Towers (Original Motion Picture Soundtrack)",
                                                        "albumSort":"lord of the rings: the two towers (original motion picture soundtrack), the",
                                                        "artUrl":"http://192.168.178.57:8097/imageproxy?path=https%3A%2F%2Fi.scdn.co%2Fimage%2Fab67616d0000b273dae458513b856d6255f857a7&provider=spotify--QTvNApMa&size=512",
                                                        "artist":["Howard Shore","Hape Kerkeling"],
                                                        "artistSort": ["howard shore","hape kerkeling"],
                                                        "duration":276.0,
                                                        "title":"Farewell to Lorien",
                                                        "trackId":"library://track/188"
                                                    },
                                                        "mute":false,
                                                        "playbackStatus":"unknown",
                                                        "position":0.0,
                                                        "rate":1.0,
                                                        "shuffle":false,
                                                        "volume":0},
                                                        "status":"playing",
                                                        "uri": { 
                                                            "fragment":"",
                                                            "host":"0.0.0.0:4991",
                                                            "path":"",
                                                            "query": {
                                                                "chunk_ms":"30",
                                                                "codec":"flac",
                                                                "controlscript":"/app/venv/lib/python3.13/site-packages/music_assistant/providers/snapcast/control.py",
                                                                "controlscriptparams":"--queueid=syncgroup_pjybcffe --api-port=8095 --streamserver-ip=192.168.178.57 --streamserver-port=8097",
                                                                "idle_threshold":"60000",
                                                                "name":"Music Assistant - satellite14e19f0",
                                                                "sampleformat":"48000:16:2"
                                                            },
                                                            "raw":"tcp://0.0.0.0:4991/?chunk_ms=30&codec=flac&controlscript=/app/venv/lib/python3.13/site-packages/music_assistant/providers/snapcast/control.py&controlscriptparams=--queueid=syncgroup_pjybcffe --api-port=8095 --streamserver-ip=192.168.178.57 --streamserver-port=8097&idle_threshold=60000&name=Music Assistant - satellite14e19f0&sampleformat=48000:16:2",
                                                            "scheme":"tcp"
                                                        }
                                                    }}}*/
