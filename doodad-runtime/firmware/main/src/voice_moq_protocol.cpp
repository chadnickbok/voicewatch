#include "voice_moq_protocol.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace doodad::moq_control {
void wipe(void* bytes, std::size_t size) {
    auto* p = static_cast<volatile unsigned char*>(bytes);
    while (size--) *p++ = 0;
}
namespace {
const cJSON* get(const cJSON* root, const char* name) { return cJSON_GetObjectItemCaseSensitive(root, name); }
bool equals(const cJSON* v, const char* text) {
    return cJSON_IsString(v) && v->valuestring && std::strcmp(v->valuestring, text) == 0;
}
bool fields(const cJSON* root, std::initializer_list<const char*> names) {
    if (!cJSON_IsObject(root) || static_cast<std::size_t>(cJSON_GetArraySize(root)) != names.size()) return false;
    for (auto name : names) if (!get(root, name)) return false;
    return true;
}
bool tree(const cJSON* value, unsigned depth, unsigned& nodes) {
    if (!value || ++nodes > 256 || depth > 12) return false;
    if (cJSON_IsNumber(value) && !std::isfinite(value->valuedouble)) return false;
    for (const auto* child=value->child; child; child=child->next) {
        if (cJSON_IsObject(value)) {
            if (!child->string) return false;
            for (const auto* other=child->next; other; other=other->next)
                if (other->string && std::strcmp(child->string, other->string)==0) return false;
        }
        if (!tree(child, depth+1, nodes)) return false;
    }
    return true;
}
bool ascii(const char* s, std::size_t minimum, std::size_t maximum, const char* allowed) {
    if (!s) return false;
    const auto size=std::strlen(s);
    if (size<minimum || size>maximum) return false;
    for (std::size_t i=0;i<size;++i) if (!std::strchr(allowed,s[i])) return false;
    return true;
}
constexpr char hex[]="0123456789abcdef";
constexpr char alnum[]="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
constexpr char token[]="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
bool host(const char* s) { return ascii(s,1,253,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-"); }
bool unhex(const char* text, std::uint8_t* out) {
    if (!ascii(text,64,64,hex)) return false;
    for (unsigned i=0;i<32;++i) out[i]=((std::strchr(hex,text[i*2])-hex)<<4)|(std::strchr(hex,text[i*2+1])-hex);
    return true;
}
void tohex(const std::uint8_t* data, char* out) {
    for (unsigned i=0;i<32;++i) { out[i*2]=hex[data[i]>>4]; out[i*2+1]=hex[data[i]&15]; }
    out[64]=0;
}
template<std::size_t N> bool copy(char (&out)[N], const cJSON* value) {
    if (!cJSON_IsString(value) || !value->valuestring || std::strlen(value->valuestring)>=N) return false;
    std::strcpy(out,value->valuestring); return true;
}
bool sign(const Profile& p, Hmac hmac, std::initializer_list<const char*> parts, std::uint8_t* digest) {
    char bytes[320]{}; std::size_t size=0, index=0;
    for (auto part:parts) {
        const auto n=std::strlen(part);
        if (size+n+1>sizeof(bytes)) return false;
        std::memcpy(bytes+size,part,n); size+=n;
        if (++index<parts.size()) bytes[size++]=0;
    }
    const bool result=hmac(p.key,reinterpret_cast<const std::uint8_t*>(bytes),size,digest);
    wipe(bytes,sizeof(bytes)); return result;
}
}
cJSON* json(const char* bytes, std::size_t size, std::size_t limit) {
    if (!bytes || !size || size>limit || std::memchr(bytes,0,size)) return nullptr;
    // Cap parser recursion before cJSON allocates its DOM. Escaped NULs would
    // truncate C strings and must never turn distinct signed fields into aliases.
    bool quoted=false, escaped=false; unsigned depth=0;
    for (std::size_t i=0;i<size;++i) {
        const char c=bytes[i];
        if (escaped) {
            if (c=='u' && i+4<size && std::memcmp(bytes+i+1,"0000",4)==0) return nullptr;
            escaped=false; continue;
        }
        if (quoted && c=='\\') { escaped=true; continue; }
        if (c=='"') { quoted=!quoted; continue; }
        if (quoted) continue;
        if (c=='[' || c=='{') { if (++depth>12) return nullptr; }
        if (c==']' || c=='}') { if (!depth) return nullptr; --depth; }
    }
    if (quoted || escaped || depth) return nullptr;
    const char* end=nullptr;
    auto* root=cJSON_ParseWithLengthOpts(bytes,size,&end,false);
    if (!root) return nullptr;
    while (end<bytes+size && (*end==' ' || *end=='\n' || *end=='\r' || *end=='\t')) ++end;
    unsigned nodes=0;
    if (end!=bytes+size || !tree(root,0,nodes)) { cJSON_Delete(root); return nullptr; }
    return root;
}
bool decimal(const cJSON* v, std::uint64_t& out, bool zero) {
    out=0;
    if (!cJSON_IsString(v) || !v->valuestring) return false;
    const char* s=v->valuestring; const auto n=std::strlen(s);
    if (!n || n>20 || (n>1 && s[0]=='0')) return false;
    for (std::size_t i=0;i<n;++i) {
        if (s[i]<'0' || s[i]>'9' || out>(UINT64_MAX-(s[i]-'0'))/10) return false;
        out=out*10+s[i]-'0';
    }
    return zero || out!=0;
}
bool number(const cJSON* v, std::uint64_t& out, std::uint64_t max) {
    if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) || v->valuedouble<0 ||
        v->valuedouble>static_cast<double>(max) || std::floor(v->valuedouble)!=v->valuedouble) return false;
    out=static_cast<std::uint64_t>(v->valuedouble); return out<=max;
}
bool valid_profile(const Profile& p, const char* device) {
    if (p.version!=1 || !p.revision || !p.control_port || !p.time_port ||
        !std::memchr(p.device,0,sizeof(p.device)) || !std::memchr(p.host,0,sizeof(p.host)) ||
        !std::memchr(p.roots,0,sizeof(p.roots)) || std::strcmp(p.device,device)!=0 || !host(p.host)) return false;
    if (!ascii(p.device,8,64,"abcdefghijklmnopqrstuvwxyz0123456789._:-") || !std::strchr(alnum,p.device[0])) return false;
    if (std::strncmp(p.roots,"-----BEGIN CERTIFICATE-----\n",28)!=0 || !std::strstr(p.roots,"-----END CERTIFICATE-----")) return false;
    for (const char* c=p.roots;*c;++c) if ((*c<32 && *c!='\n' && *c!='\r') || *c>126) return false;
    unsigned any=0; for (auto byte:p.key) any|=byte;
    return any!=0;
}
bool profile(const cJSON* root, const char* device, Profile& out) {
    if (!fields(root,{"v","revision","device_id","host","control_port","time_port","roots_pem","key_hex"})) return false;
    std::uint64_t version=0, revision=0, control=0, time=0;
    if (!number(get(root,"v"),version,1) || version!=1 || !number(get(root,"revision"),revision,UINT32_MAX) ||
        !number(get(root,"control_port"),control,65535) || !number(get(root,"time_port"),time,65535) ||
        !copy(out.device,get(root,"device_id")) || !copy(out.host,get(root,"host")) || !copy(out.roots,get(root,"roots_pem"))) return false;
    const auto* key=get(root,"key_hex");
    if (!cJSON_IsString(key) || !unhex(key->valuestring,out.key)) return false;
    out.version=version; out.revision=revision; out.control_port=control; out.time_port=time;
    return valid_profile(out,device);
}
bool time_proof(const cJSON* root,const Profile& p,const char* nonce,std::uint64_t elapsed,Hmac hmac,std::uint64_t& utc) {
    if (elapsed>3000 || !fields(root,{"v","device_id","nonce","unix_ms","validity_seconds","proof"}) ||
        !equals(get(root,"device_id"),p.device) || !equals(get(root,"nonce"),nonce) || !ascii(nonce,64,64,hex)) return false;
    std::uint64_t v=0,validity=0;
    if (!number(get(root,"v"),v,1) || v!=1 || !number(get(root,"validity_seconds"),validity,600) || validity!=600 ||
        !decimal(get(root,"unix_ms"),utc) || utc<1700000000000ULL || utc>4102444800000ULL) return false;
    const auto* proof=get(root,"proof"); std::uint8_t expected[32]{}, received[32]{};
    if (!cJSON_IsString(proof) || !unhex(proof->valuestring,received) ||
        !sign(p,hmac,{"voicewatch-moq-time-v1",p.device,nonce,get(root,"unix_ms")->valuestring,"600"},expected)) return false;
    unsigned diff=0; for (unsigned i=0;i<32;++i) diff|=received[i]^expected[i];
    wipe(expected,sizeof(expected)); wipe(received,sizeof(received));
    if (diff) return false;
    utc+=elapsed/2; return true;
}
bool bootstrap_proof(const Profile& p,const char* challenge,Hmac hmac,char (&proof)[65]) {
    std::uint8_t digest[32]{};
    if (!ascii(challenge,43,43,token) || !sign(p,hmac,{"voicewatch-moq-bootstrap-v1",p.device,challenge},digest)) return false;
    tohex(digest,proof); wipe(digest,sizeof(digest)); return true;
}
bool renewal_proof(const Profile& p,const char* session,const char* nonce,Hmac hmac,char (&proof)[65]) {
    std::uint8_t digest[32]{};
    if (!ascii(session,32,32,hex) || !ascii(nonce,64,64,hex) ||
        !sign(p,hmac,{"voicewatch-moq-renew-v1",p.device,session,nonce},digest)) return false;
    tohex(digest,proof); wipe(digest,sizeof(digest)); return true;
}
bool renewal(const cJSON* root,const Profile& p,const char* nonce,std::uint64_t next_revision,
             std::uint64_t start,std::uint64_t now,std::uint64_t utc,const Grant& current,Hmac hmac,Renewal& out) {
    if (!fields(root,{"nonce","revision","expires_unix","lease_seconds","time"}) ||
        !equals(get(root,"nonce"),nonce) || now<start || now-start>3000 ||
        current.profile_revision!=p.revision || now>=current.until_ms || now>=current.trusted_until_ms ||
        now>UINT64_MAX-900000) return false;
    std::uint64_t revision=0,expires=0,lease=0,proof_utc=0;
    if (!number(get(root,"revision"),revision,9007199254740991ULL) || !revision || revision!=next_revision ||
        !number(get(root,"expires_unix"),expires,4102444800ULL) ||
        !number(get(root,"lease_seconds"),lease,900) || !lease || expires*1000<=utc ||
        expires*1000>utc+lease*1000 ||
        !time_proof(get(root,"time"),p,nonce,now-start,hmac,proof_utc) ||
        (utc>proof_utc ? utc-proof_utc : proof_utc-utc)>2000) return false;
    Renewal candidate{revision,std::min(start+lease*1000,now+(expires*1000-utc)),start+600000};
    if (candidate.until_ms<=current.until_ms || candidate.trusted_until_ms<=current.trusted_until_ms) return false;
    out=candidate; return true;
}
bool grant(const cJSON* root,const Profile& p,std::uint64_t utc,std::uint64_t start,std::uint64_t now,
           std::uint64_t trust_until,Grant& out) {
    constexpr char setup_prefix[]="/voicewatch/v1?token=";
    constexpr auto prefix_size=sizeof(setup_prefix)-1;
    if (!fields(root,{"session_id","device_id","publish","subscribe","expires_unix","lease_seconds","control_token",
                      "setup_path","media_host","media_port","control_path","transport"}) ||
        !equals(get(root,"device_id"),p.device) || !equals(get(root,"transport"),"moq-lite-05") ||
        !equals(get(root,"control_path"),"/v1/moq/control") || trust_until<=now || now<start || now-start>5000) return false;
    std::uint64_t expires=0,lease=0,port=0;
    if (!number(get(root,"expires_unix"),expires,4102444800ULL) || !number(get(root,"lease_seconds"),lease,900) || !lease ||
        !number(get(root,"media_port"),port,65535) || !port || expires*1000<=utc || expires*1000>utc+lease*1000+2000 ||
        !copy(out.session,get(root,"session_id")) || !ascii(out.session,32,32,hex) || !copy(out.host,get(root,"media_host")) || !host(out.host) ||
        !copy(out.setup,get(root,"setup_path")) || std::strncmp(out.setup,setup_prefix,prefix_size)!=0 || !ascii(out.setup+prefix_size,43,43,token)) return false;
    std::snprintf(out.publish,sizeof(out.publish),"voicewatch/%s/%s/watch",p.device,out.session);
    std::snprintf(out.subscribe,sizeof(out.subscribe),"voicewatch/%s/%s/agent",p.device,out.session);
    const auto* control=get(root,"control_token");
    if (!equals(get(root,"publish"),out.publish) || !equals(get(root,"subscribe"),out.subscribe) ||
        !cJSON_IsString(control) || !ascii(control->valuestring,43,43,token)) return false;
    std::snprintf(out.websocket_url,sizeof(out.websocket_url),"wss://%s:%u/v1/moq/control",p.host,p.control_port);
    std::snprintf(out.headers,sizeof(out.headers),"Authorization: Bearer %s\r\n",control->valuestring);
    std::strcpy(out.roots,p.roots); out.profile_revision=p.revision; out.port=port;
    out.until_ms=std::min(start+lease*1000,now+(expires*1000-utc));
    out.trusted_until_ms=trust_until;
    return out.until_ms>now;
}
bool envelope(const cJSON* root,const char* device,const char* session,std::uint64_t& sequence) {
    std::uint64_t version=0,next=0;
    if (!fields(root,{"v","type","seq","session_id","device_id","payload"}) ||
        !number(get(root,"v"),version,1) || version!=1 || !number(get(root,"seq"),next,9007199254740991ULL) ||
        next!=sequence+1 || !equals(get(root,"device_id"),device) || !equals(get(root,"session_id"),session) ||
        !cJSON_IsString(get(root,"type")) || !cJSON_IsObject(get(root,"payload"))) return false;
    sequence=next; return true;
}
}
