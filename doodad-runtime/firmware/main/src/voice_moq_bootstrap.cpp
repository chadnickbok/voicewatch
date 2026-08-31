#include "voice_moq_bootstrap.hpp"
#include "board.hpp"
#include "network_service.hpp"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/md.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#include "sdkconfig.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>

#if !CONFIG_MBEDTLS_HAVE_TIME_DATE
#error "Authenticated MoQ bootstrap requires mbedTLS certificate expiry checks"
#endif

namespace doodad::moq_control {
namespace {
constexpr char kNamespace[]="moq_enroll";
constexpr char kTag[]="moq_bootstrap";
Profile* saved=nullptr;
SemaphoreHandle_t mutex=nullptr;
bool initialized=false;
std::atomic<std::uint32_t> revision{0};
std::atomic<std::uint64_t> clock_mono{0}, clock_utc{0}, clock_until{0};
bool rejected=false;
std::uint64_t mono_ms() { return esp_timer_get_time()/1000; }
std::uint64_t utc_ms() { timeval t{}; gettimeofday(&t,nullptr); return t.tv_sec*1000ULL+t.tv_usec/1000; }
bool hmac(const std::uint8_t* key,const std::uint8_t* data,std::size_t size,std::uint8_t* digest) {
    const auto* type=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return type && mbedtls_md_hmac(type,key,32,data,size,digest)==0;
}
void free_json(cJSON* root) {
    if (!root) return;
    auto wipe_values=[](auto&& self,cJSON* node)->void {
        if (node->valuestring) wipe(node->valuestring,std::strlen(node->valuestring));
        for (auto* child=node->child;child;child=child->next) self(self,child);
    };
    wipe_values(wipe_values,root); cJSON_Delete(root);
}
struct Reply { char text[2049]{}; std::size_t size=0; bool overflow=false; };
esp_err_t http_event(esp_http_client_event_t* event) {
    auto* reply=static_cast<Reply*>(event->user_data);
    if (event->event_id==HTTP_EVENT_ON_DATA && event->data_len>0) {
        if (reply->size+event->data_len>=sizeof(reply->text)) { reply->overflow=true; return ESP_FAIL; }
        std::memcpy(reply->text+reply->size,event->data,event->data_len); reply->size+=event->data_len;
    }
    return ESP_OK;
}
cJSON* post(const Profile& p,bool secure,const char* path,const char* body) {
    char url[320]{};
    std::snprintf(url,sizeof(url),"%s://%s:%u%s",secure?"https":"http",p.host,secure?p.control_port:p.time_port,path);
    auto* reply=static_cast<Reply*>(heap_caps_calloc(1,sizeof(Reply),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (!reply) return nullptr;
    esp_http_client_config_t config{};
    config.url=url; config.method=HTTP_METHOD_POST; config.timeout_ms=3000;
    config.cert_pem=secure?p.roots:nullptr; config.skip_cert_common_name_check=false;
    config.disable_auto_redirect=true; config.event_handler=http_event; config.user_data=reply;
    config.buffer_size=1024; config.buffer_size_tx=512;
    auto client=esp_http_client_init(&config);
    cJSON* parsed=nullptr;
    if (client) {
        const auto started=mono_ms();
        if (esp_http_client_set_header(client,"Content-Type","application/json")==ESP_OK &&
            esp_http_client_set_post_field(client,body,std::strlen(body))==ESP_OK &&
            esp_http_client_perform(client)==ESP_OK && esp_http_client_get_status_code(client)==200 &&
            !reply->overflow && mono_ms()-started<=5000) parsed=json(reply->text,reply->size,2048);
        int tls_flags=0,tls_code=0;
        esp_http_client_get_and_clear_last_tls_error(client,&tls_code,&tls_flags);
        const int status=esp_http_client_get_status_code(client);
        // With KEEP_PEER_CERTIFICATE disabled, IDF cannot recover verify flags
        // after the handshake. The X.509 failure code still identifies a
        // terminal certificate rejection; do not retry it indefinitely.
        const bool certificate=tls_flags || tls_code==MBEDTLS_ERR_X509_CERT_VERIFY_FAILED ||
            tls_code==-MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
        if (secure && (certificate || status==401 || status==403)) {
            rejected=true;
            ESP_LOGW(kTag,"%s rejected",certificate?"certificate":"authorization");
        }
        esp_http_client_cleanup(client);
    }
    wipe(reply,sizeof(*reply)); heap_caps_free(reply); return parsed;
}
bool install(const char* bytes,std::size_t size) {
    auto* root=json(bytes,size,8192);
    auto* candidate=static_cast<Profile*>(heap_caps_calloc(1,sizeof(Profile),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    bool ok=root && candidate && profile(root,doodad::board::identity().device_id,*candidate);
    free_json(root);
    if (ok) {
        mbedtls_x509_crt roots;
        mbedtls_x509_crt_init(&roots);
        ok=mbedtls_x509_crt_parse(&roots,reinterpret_cast<const unsigned char*>(candidate->roots),
                                  std::strlen(candidate->roots)+1)==0;
        for (const auto* root=&roots;ok && root;root=root->next) ok=mbedtls_x509_crt_get_ca_istrue(root);
        mbedtls_x509_crt_free(&roots);
    }
    if (ok && xSemaphoreTake(mutex,pdMS_TO_TICKS(1000))==pdTRUE) {
        ok=candidate->revision>revision.load();
        nvs_handle_t handle{};
        if (ok) {
            ok=nvs_open(kNamespace,NVS_READWRITE,&handle)==ESP_OK;
            if (ok) {
                ok=nvs_set_blob(handle,"profile",candidate,sizeof(*candidate))==ESP_OK && nvs_commit(handle)==ESP_OK;
                nvs_close(handle);
            }
        }
        if (ok) {
            clock_until.store(0); wipe(saved,sizeof(*saved)); *saved=*candidate;
            revision.store(candidate->revision);
        }
        xSemaphoreGive(mutex);
    } else ok=false;
    if (candidate) { wipe(candidate,sizeof(*candidate)); heap_caps_free(candidate); }
    return ok;
}
void usb_enrollment(void* argument) {
    // Physical USB console only, no remote provisioning listener and no echo.
    // This task has an internal stack because NVS writes disable flash cache.
    auto* line=static_cast<char*>(argument);
    std::size_t used=0; bool overflow=false; std::uint64_t started=0;
    while (true) {
        char input[64]{}; const auto count=read(STDIN_FILENO,input,sizeof(input));
        for (int i=0;i<count;++i) {
            const char c=input[i];
            if (!used) started=mono_ms();
            if (c=='\r') continue;
            if (c=='\n') {
                if (!overflow && std::strcmp(line,"VWMOQ1 INFO")==0)
                    std::printf("\nVWMOQ1 INFO device=%s revision=%u\n",doodad::board::identity().device_id,static_cast<unsigned>(revision.load()));
                else if (!overflow && used>11 && std::memcmp(line,"VWMOQ1 SET ",11)==0) {
                    const bool ok=install(line+11,used-11);
                    std::printf("\nVWMOQ1 %s revision=%u\n",ok?"OK":"DENIED",static_cast<unsigned>(revision.load()));
                }
                wipe(line,8193); used=0; overflow=false;
            } else if (!overflow && used<8192) line[used++]=c;
            else overflow=true;
        }
        wipe(input,sizeof(input));
        if ((used || overflow) && mono_ms()-started>5000) { wipe(line,8193); used=0; overflow=false; }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
}
bool init() {
    if (initialized) return true;
#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    return false; // Never route enrollment through a network/UART console.
#else
    saved=static_cast<Profile*>(heap_caps_calloc(1,sizeof(Profile),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    mutex=xSemaphoreCreateMutex();
    auto* line=heap_caps_calloc(1,8193,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    // IDF's nonblocking VFS reports availability from the installed driver's
    // ring; without that driver it reports zero even when the hardware FIFO
    // contains bytes. Install it before enabling nonblocking console reads.
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config{}; config.tx_buffer_size=256; config.rx_buffer_size=512;
        if (usb_serial_jtag_driver_install(&config)!=ESP_OK) {
            heap_caps_free(line); heap_caps_free(saved); saved=nullptr;
            if (mutex) vSemaphoreDelete(mutex);
            mutex=nullptr; return false;
        }
    }
    usb_serial_jtag_vfs_use_driver();
    const auto flags=fcntl(STDIN_FILENO,F_GETFL,0);
    auto cleanup=[&] {
        if (saved) { wipe(saved,sizeof(*saved)); heap_caps_free(saved); saved=nullptr; }
        if (mutex) { vSemaphoreDelete(mutex); mutex=nullptr; }
        heap_caps_free(line); revision.store(0);
    };
    if (!saved || !mutex || !line || flags<0 || fcntl(STDIN_FILENO,F_SETFL,flags|O_NONBLOCK)<0) {
        cleanup(); return false;
    }
    nvs_handle_t handle{};
    if (nvs_open(kNamespace,NVS_READONLY,&handle)==ESP_OK) {
        std::size_t size=sizeof(*saved);
        if (nvs_get_blob(handle,"profile",saved,&size)!=ESP_OK || size!=sizeof(*saved) ||
            !valid_profile(*saved,doodad::board::identity().device_id)) wipe(saved,sizeof(*saved));
        nvs_close(handle);
    }
    revision.store(saved->revision);
    if (xTaskCreatePinnedToCore(usb_enrollment,"moq_enroll",6144,line,3,nullptr,0)!=pdPASS) {
        cleanup(); return false;
    }
    initialized=true;
    ESP_LOGI(kTag,"USB enrollment ready; profile %s",revision.load()?"present":"absent");
    return true;
#endif
}
std::uint32_t profile_revision() { return revision.load(); }
bool authorization_rejected() { return rejected; }
bool clock_valid() {
    const auto now=mono_ms(), since=clock_mono.load(), until=clock_until.load();
    if (!until || now<since || now>=until) return false;
    const auto expected=clock_utc.load()+now-since, actual=utc_ms();
    return actual>=expected-2000 && actual<=expected+2000;
}
bool acquire(Grant& out) {
    rejected=false;
    wipe(&out,sizeof(out));
    if (!saved || !revision.load() || !network_service_connected()) return false;
    auto* p=static_cast<Profile*>(heap_caps_calloc(1,sizeof(Profile),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (!p) return false;
    if (xSemaphoreTake(mutex,pdMS_TO_TICKS(1000))!=pdTRUE) { heap_caps_free(p); return false; }
    *p=*saved; xSemaphoreGive(mutex);
    bool ok=false;
    char nonce[65]{}, proof[65]{}, body[384]{}; std::uint8_t random[32]{};
    esp_fill_random(random,sizeof(random));
    for (unsigned i=0;i<32;++i) std::snprintf(nonce+i*2,3,"%02x",random[i]);
    wipe(random,sizeof(random));
    const auto began=mono_ms();
    std::snprintf(body,sizeof(body),"{\"device_id\":\"%s\",\"nonce\":\"%s\"}",p->device,nonce);
    auto* reply=post(*p,false,"/v1/moq/time",body);
    std::uint64_t utc=0;
    if (reply && time_proof(reply,*p,nonce,mono_ms()-began,hmac,utc) && p->revision==revision.load()) {
        timeval time{}; time.tv_sec=utc/1000; time.tv_usec=(utc%1000)*1000;
        if (settimeofday(&time,nullptr)==0) {
            clock_mono.store(mono_ms()); clock_utc.store(utc); clock_until.store(began+600000);
            ok=true;
            ESP_LOGI(kTag,"authenticated time accepted");
        }
    }
    free_json(reply); reply=nullptr;
    if (ok && clock_valid()) {
        std::snprintf(body,sizeof(body),"{\"device_id\":\"%s\"}",p->device);
        reply=post(*p,true,"/v1/moq/challenge",body);
        auto* challenge=cJSON_GetObjectItemCaseSensitive(reply,"challenge");
        ok=reply && cJSON_GetArraySize(reply)==1 && cJSON_IsString(challenge) &&
            bootstrap_proof(*p,challenge->valuestring,hmac,proof);
        if (ok) std::snprintf(body,sizeof(body),"{\"device_id\":\"%s\",\"challenge\":\"%s\",\"proof\":\"%s\"}",p->device,challenge->valuestring,proof);
        free_json(reply); reply=nullptr;
        if (ok && clock_valid()) {
            const auto request=mono_ms(); reply=post(*p,true,"/v1/moq/bootstrap",body);
            ok=reply && clock_valid() && p->revision==revision.load() &&
                grant(reply,*p,utc_ms(),request,mono_ms(),clock_until.load(),out);
        } else ok=false;
    } else ok=false;
    free_json(reply); wipe(body,sizeof(body)); wipe(proof,sizeof(proof)); wipe(nonce,sizeof(nonce));
    wipe(p,sizeof(*p)); heap_caps_free(p);
    if (!ok) wipe(&out,sizeof(out));
    ESP_LOGI(kTag,"bootstrap %s",ok?"accepted":"failed");
    return ok;
}
}
