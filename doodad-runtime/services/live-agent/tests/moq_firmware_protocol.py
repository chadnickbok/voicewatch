"""Explicit native C++ lane: ESP-IDF cJSON plus system OpenSSL, no device keys.

Exercises the same parsers linked into the firmware. Requires an ESP-IDF checkout,
C/C++ compiler and pkg-config openssl. No hardware or firmware write occurs.
"""
import copy
import json
import os
import subprocess
from pathlib import Path

import pytest

from doodad_agent.moq_auth import GrantRegistry, bootstrap_proof

ROOT=Path(__file__).resolve().parents[4]
DEVICE='ultra-test-protocol'
KEY=bytes(range(32))
PROFILE=dict(v=1,revision=1,device_id=DEVICE,host='localhost',control_port=8766,time_port=8767,
             roots_pem='-----BEGIN CERTIFICATE-----\nTEST\n-----END CERTIFICATE-----\n',key_hex=KEY.hex())


@pytest.fixture(scope='module')
def parser(tmp_path_factory):
    output=tmp_path_factory.mktemp('moq-cpp')
    idf=Path(os.environ.get('IDF_PATH',Path.home()/'.espressif/frameworks/esp-idf-v5.5.5'))
    cjson=idf/'components/json/cJSON'
    assert (cjson/'cJSON.c').is_file(), 'Set IDF_PATH to the pinned ESP-IDF checkout'
    source=output/'main.cpp'
    source.write_text(r'''
#include "voice_moq_protocol.hpp"
#include <openssl/hmac.h>
#include <iostream>
#include <string>
#include <cstring>
namespace m=doodad::moq_control;
bool mac(const unsigned char* key,const unsigned char* data,size_t size,unsigned char* out) {
    unsigned count=0; return HMAC(EVP_sha256(),key,32,data,size,out,&count) && count==32;
}
int main(int argc,char** argv) {
    std::string text; std::getline(std::cin,text);
    auto* root=m::json(text.data(),text.size());
    if (!root) { std::cout<<"{\"ok\":false}"; return 0; }
    m::Profile profile{}; bool ok=m::profile(cJSON_GetObjectItemCaseSensitive(root,"profile"),"ultra-test-protocol",profile);
    auto* data=cJSON_GetObjectItemCaseSensitive(root,"data");
    if (ok && argc>1 && std::strcmp(argv[1],"time")==0) {
        auto* nonce=cJSON_GetObjectItemCaseSensitive(root,"nonce");
        auto* elapsed=cJSON_GetObjectItemCaseSensitive(root,"elapsed"); uint64_t utc=0;
        ok=nonce && m::time_proof(data,profile,nonce->valuestring,elapsed?elapsed->valueint:0,mac,utc);
        std::cout<<"{\"ok\":"<<(ok?"true":"false")<<",\"utc\":"<<utc<<"}";
    } else if (ok && argc>1 && std::strcmp(argv[1],"proof")==0) {
        char proof[65]{}; ok=m::bootstrap_proof(profile,data->valuestring,mac,proof);
        std::cout<<"{\"ok\":"<<(ok?"true":"false")<<",\"proof\":\""<<proof<<"\"}";
    } else if (ok && argc>1 && std::strcmp(argv[1],"grant")==0) {
        m::Grant grant{}; ok=m::grant(data,profile,1800000000000ULL,100,101,600100,grant);
        std::cout<<"{\"ok\":"<<(ok?"true":"false")<<"}";
    } else if (ok && argc>1 && std::strcmp(argv[1],"envelope")==0) {
        uint64_t seq=0; ok=m::envelope(data,profile.device,"0123456789abcdef0123456789abcdef",seq);
        std::cout<<"{\"ok\":"<<(ok?"true":"false")<<"}";
    } else std::cout<<"{\"ok\":"<<(ok?"true":"false")<<"}";
    cJSON_Delete(root);
}
''')
    subprocess.run(['cc','-c',str(cjson/'cJSON.c'),'-o',str(output/'cjson.o')],check=True,capture_output=True)
    flags=subprocess.check_output(['pkg-config','--cflags','--libs','openssl'],text=True).split()
    main=ROOT/'doodad-runtime/firmware/main'
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(main/'include'),'-I'+str(cjson),
                    str(source),str(main/'src/voice_moq_protocol.cpp'),str(output/'cjson.o'),*flags,'-o',str(output/'parser')],
                   check=True,capture_output=True)
    def run(mode='profile',data=None,**kw):
        raw=kw.pop('raw',None)
        document=dict(profile=copy.deepcopy(PROFILE),data=data,**kw)
        result=subprocess.run([str(output/'parser'),mode],input=raw or json.dumps(document),text=True,
                              check=True,capture_output=True)
        return json.loads(result.stdout)
    return run


def test_firmware_bootstrap_proof_matches_python_byte_contract(parser):
    assert parser()['ok']
    challenge='A'*43
    assert parser('proof',challenge)['proof']==bootstrap_proof(KEY,DEVICE,challenge)


@pytest.mark.parametrize('fault',['none','nonce','device','timestamp','validity','proof','slow','nul'])
def test_firmware_verifies_nonce_time_and_round_trip(parser,fault):
    registry=GrantRegistry({DEVICE:KEY},monotonic=lambda:10,wall_clock=lambda:1800000000.125)
    nonce='a'*64; data=registry.time_proof(DEVICE,nonce)
    elapsed=200
    if fault=='nonce': data['nonce']='b'*64
    elif fault=='device': data['device_id']='another-device'
    elif fault=='timestamp': data['unix_ms']='1800000000126'
    elif fault=='validity': data['validity_seconds']=900
    elif fault=='proof': data['proof']='0'*64
    elif fault=='slow': elapsed=3001
    elif fault=='nul': data['nonce']+='\0ignored'
    result=parser('time',data,nonce=nonce,elapsed=elapsed)
    assert result['ok']==(fault=='none')
    if fault=='none': assert result['utc']==1800000000225


@pytest.mark.parametrize('fault',['none','scope','lease','device','transport','token','extra'])
def test_firmware_grant_rejects_scope_protocol_and_lifetime_confusion(parser,fault):
    registry=GrantRegistry({DEVICE:KEY},monotonic=lambda:10,wall_clock=lambda:1800000000)
    nonce=registry.challenge(DEVICE)
    data={**registry.issue(DEVICE,nonce,bootstrap_proof(KEY,DEVICE,nonce)).document(),
          'media_host':'localhost','media_port':4443,'control_path':'/v1/moq/control','transport':'moq-lite-05'}
    if fault=='scope': data['subscribe']='voicewatch/another-device/agent'
    elif fault=='lease': data['expires_unix']+=1000
    elif fault=='device': data['device_id']='another-device'
    elif fault=='transport': data['transport']='webrtc'
    elif fault=='token': data['setup_path']+='&legacy=true'
    elif fault=='extra': data['unknown']=True
    assert parser('grant',data)['ok']==(fault=='none')


@pytest.mark.parametrize('fault',['none','session','sequence','boolean','extra'])
def test_firmware_envelope_matches_authenticated_identity(parser,fault):
    data=dict(v=1,type='welcome',seq=1,session_id='0123456789abcdef0123456789abcdef',device_id=DEVICE,payload={})
    if fault=='session': data['session_id']='watch-uplink'
    elif fault=='sequence': data['seq']=2
    elif fault=='boolean': data['seq']=True
    elif fault=='extra': data['extra']=1
    assert parser('envelope',data)['ok']==(fault=='none')


@pytest.mark.parametrize('raw',['{"x":1,"x":2}','{} trailing', '['*13+'0'+']'*13,'{"x":"a\\u0000b"}','{"x":1e9999}'])
def test_firmware_json_rejects_duplicates_depth_trailing_nul_and_nonfinite(parser,raw):
    assert not parser(raw=raw)['ok']
