"""Authenticated cold-start time: public test keys and fake clocks only."""
import hashlib
import hmac

import pytest
from aiohttp import ClientSession

from doodad_agent.moq_auth import AuthorizationError, GrantRegistry, bootstrap_proof
from doodad_agent.moq_time import MoqTimeServer

DEVICE='ultra-test-time'
KEY=bytes(range(32))


def test_time_proof_is_domain_separated_and_does_not_issue_capabilities():
    registry=GrantRegistry({DEVICE:KEY}, monotonic=lambda:10, wall_clock=lambda:1800000000.125)
    nonce='ab'*32
    response=registry.time_proof(DEVICE,nonce)
    # Build separators separately: Python octal string escapes must not change
    # the intended bytes before a decimal timestamp beginning with a digit.
    body=b'\0'.join([b'voicewatch-moq-time-v1',DEVICE.encode(),nonce.encode(),b'1800000000125',b'600'])
    assert hmac.compare_digest(response['proof'],hmac.new(KEY,body,hashlib.sha256).hexdigest())
    assert response['unix_ms']=='1800000000125' and response['validity_seconds']==600
    assert response['proof']!=bootstrap_proof(KEY,DEVICE,nonce)
    assert not registry._grants and not registry._challenges
    assert registry.time_proof(DEVICE,'cd'*32)['proof']!=response['proof']


@pytest.mark.parametrize('nonce', ['', 'A'*64, 'a'*63, '0'*65, True, 'a'*63+'\n'])
def test_time_proof_rejects_ambiguous_nonces(nonce):
    with pytest.raises(AuthorizationError): GrantRegistry({DEVICE:KEY}).time_proof(DEVICE,nonce)


def test_bad_host_clock_cannot_sign_new_time():
    wall=[1800000000.0]
    registry=GrantRegistry({DEVICE:KEY}, wall_clock=lambda:wall[0])
    wall[0]-=100
    with pytest.raises(AuthorizationError): registry.time_proof(DEVICE,'a'*64)


@pytest.mark.asyncio
async def test_plain_time_listener_has_no_bootstrap_or_protected_routes():
    server=MoqTimeServer(GrantRegistry({DEVICE:KEY}))
    await server.start('127.0.0.1',0)
    base=f'http://127.0.0.1:{server._runner.addresses[0][1]}'
    try:
        async with ClientSession() as client:
            async with client.post(base+'/v1/moq/time',json={'device_id':DEVICE,'nonce':'a'*64}) as response:
                assert response.status==200 and response.headers['Cache-Control']=='no-store'
                assert set(await response.json())=={'v','device_id','nonce','unix_ms','validity_seconds','proof'}
            for route in ('bootstrap','control','challenge'):
                async with client.post(base+'/v1/moq/'+route,json={}) as response: assert response.status==404
            for body in ({'device_id':DEVICE,'nonce':'a'*64,'unix_ms':'0'}, {'device_id':'unknown-device','nonce':'a'*64}):
                async with client.post(base+'/v1/moq/time',json=body) as response: assert response.status==403
            async with client.post(base+'/v1/moq/time?token=public-test',json={'device_id':DEVICE,'nonce':'a'*64}) as response:
                assert response.status==403
            async with client.post(base+'/v1/moq/time',data=b'x'*1025,headers={'Content-Type':'application/json'}) as response:
                assert response.status==413
    finally:
        await server.close()
