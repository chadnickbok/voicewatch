import asyncio
import importlib.util
import os
import sys
from datetime import datetime,timedelta,timezone
from pathlib import Path
from types import SimpleNamespace

import pytest
from cryptography import x509


def module(name):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).parents[1]/'tools'/f'{name}.py')
    loaded=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


idle=module('moq_idle_soak')
bench=module('moq_ultra_bench')
RAW=('MOQ_STATUS uptime_ms=123456 ready=1 microphone=0 speaker=0 publish=0 receive=0 '
     'leased=0 tx_queued=0 internal_free=164635 internal_min=99256 internal_largest=79872 '
     'psram_free=7014552 audio_stack=6000 network_stack=5000 dns_stack=3000 rx_owned=0 rx_high=2 tx_high=1\n')


@pytest.fixture(autouse=True)
def preserve_monitor_process_settings():
    previous=os.umask(0o077)
    os.umask(previous)
    path=list(sys.path)
    try:yield
    finally:
        os.umask(previous)
        sys.path[:]=path


def test_idle_parser_requires_a_complete_record_and_exports_only_numeric_fields():
    assert idle.statuses(RAW[:-3])==[]
    rows=idle.statuses('unrelated private application text\n'+RAW+'another unrelated line\n')
    assert len(rows)==1 and set(rows[0])==set(idle.STATUS_FIELDS)
    assert all(type(value) is int for value in rows[0].values())
    idle.validate_idle_status(rows[0])


@pytest.mark.parametrize('key',('microphone','speaker','publish','receive','leased','tx_queued','rx_owned'))
def test_idle_gate_rejects_unexpected_audio_or_retained_media_ownership(key):
    row=idle.statuses(RAW)[0]
    row[key]=1
    with pytest.raises(RuntimeError,match='ownership was active'):
        idle.validate_idle_status(row)


@pytest.mark.parametrize('key,value',(('ready',0),('internal_min',98303),('internal_largest',32767),('audio_stack',0),('network_stack',0),('dns_stack',0)))
def test_idle_gate_requires_readiness_and_original_memory_floors(key,value):
    row=idle.statuses(RAW)[0]
    row[key]=value
    with pytest.raises(RuntimeError):idle.validate_idle_status(row)


def test_endurance_certificate_covers_eight_hours_without_disabling_time_checks(tmp_path):
    bench.pki(tmp_path,'127.0.0.1',valid_for_hours=10)
    certificate=x509.load_pem_x509_certificate((tmp_path/'server.pem').read_bytes())
    now=datetime.now(timezone.utc)
    assert certificate.not_valid_before_utc<now
    assert now+timedelta(hours=9)<certificate.not_valid_after_utc<=now+timedelta(hours=10)
    assert (tmp_path/'server.key').stat().st_mode&0o077==0


def test_expired_certificate_fault_remains_expired_in_endurance_configuration(tmp_path):
    bench.pki(tmp_path,'127.0.0.1','expired',valid_for_hours=10)
    certificate=x509.load_pem_x509_certificate((tmp_path/'server.pem').read_bytes())
    assert certificate.not_valid_after_utc<datetime.now(timezone.utc)


@pytest.mark.asyncio
async def test_idle_loop_fails_capture_started_even_without_received_pcm(tmp_path):
    connected=asyncio.Event();connected.set()
    capture_started=asyncio.Event();capture_started.set()
    current=SimpleNamespace(_closed=False,_fault=asyncio.Event(),connected=connected)
    server=SimpleNamespace(sessions={'fixture':current},bridge=SimpleNamespace(unexpected_failures=0),
        bootstrap=SimpleNamespace(unexpected_failures=0))
    result={'microphone_samples':0}
    with pytest.raises(RuntimeError,match='invariant failed'):
        await idle.run_idle(seconds=120,server=server,first=current,ready=asyncio.Queue(),
            device_id='fixture',result=result,directory=tmp_path,native=SimpleNamespace(returncode=None),
            monitor=SimpleNamespace(returncode=None),capture_started=capture_started,mark=lambda *a,**kw:None)
    assert not result['idle']['protocol_pass'] and not result['idle']['snapshots']


def test_idle_cli_refuses_audio_before_touching_hardware(tmp_path,monkeypatch):
    output=tmp_path/'must-not-exist'
    monkeypatch.setattr('sys.argv',['bench','--output',str(output),'--port','not-a-device',
        '--host','127.0.0.1','--idf-python','not-a-python','--idle-seconds','120','--audio'])
    with pytest.raises(SystemExit) as caught:bench.main()
    assert caught.value.code==2 and not output.exists()


def test_serial_monitor_sends_only_read_only_queries_and_keeps_logs_private(tmp_path,monkeypatch):
    sent=[]
    clock=[0]
    class Link:
        def __enter__(self):return self
        def __exit__(self,*args):pass
        def read(self,size):
            clock[0]+=1
            return RAW.encode()
    monkeypatch.setitem(sys.modules,'moq_enroll',SimpleNamespace(connect=lambda port:Link(),send=lambda link,raw:sent.append(raw)))
    monkeypatch.setattr(idle.time,'monotonic',lambda:clock[0])
    output=tmp_path/'serial.log'
    idle.serial_monitor(SimpleNamespace(output=output,port='fixture',seconds=11))
    assert sent==[b'VWMOQ1 STATS\n']*3
    assert output.stat().st_mode&0o077==0 and output.read_bytes()==RAW.encode()*11


def test_serial_monitor_detects_a_fault_split_across_reads(tmp_path,monkeypatch):
    chunks=iter((b'I (1) safe\nGuru Med',b'itation Error\n'))
    class Link:
        def __enter__(self):return self
        def __exit__(self,*args):pass
        def read(self,size):return next(chunks)
    monkeypatch.setitem(sys.modules,'moq_enroll',SimpleNamespace(connect=lambda port:Link(),send=lambda *args:None))
    with pytest.raises(RuntimeError,match='firmware fault'):
        idle.serial_monitor(SimpleNamespace(output=tmp_path/'serial.log',port='fixture',seconds=1))


def test_serial_monitor_refuses_to_follow_an_output_symlink(tmp_path,monkeypatch):
    target=tmp_path/'existing';target.write_bytes(b'keep')
    output=tmp_path/'serial.log';output.symlink_to(target)
    def no_connect(port):raise AssertionError('must fail before opening serial')
    monkeypatch.setitem(sys.modules,'moq_enroll',SimpleNamespace(connect=no_connect,send=lambda *args:None))
    with pytest.raises(OSError):
        idle.serial_monitor(SimpleNamespace(output=output,port='fixture',seconds=1))
    assert target.read_bytes()==b'keep'


@pytest.mark.asyncio
@pytest.mark.parametrize('seconds,expected_reconnects',((120,1),(28800,7)))
async def test_idle_schedule_renews_and_reconnects_without_claiming_heap_recovery(seconds,expected_reconnects,monkeypatch):
    # Virtual elapsed time tests the bench's full control loop, not the network
    # or firmware. No subprocess or hardware is touched by this model.
    now=[0.0]
    queue=asyncio.Queue()
    sessions=[]
    server=SimpleNamespace(sessions={},bridge=SimpleNamespace(unexpected_failures=0),
        bootstrap=SimpleNamespace(unexpected_failures=0))
    class Session:
        def __init__(self):
            self.born=now[0];self.session_id=len(sessions)+1;self._closed=False
            self._fault=asyncio.Event();self.connected=asyncio.Event();self.connected.set()
            sessions.append(self)
        @property
        def renewals_completed(self):return int((now[0]-self.born)//20)
        async def close(self,**kwargs):
            self._closed=True;self.connected.clear();now[0]+=1
            replacement=Session();server.sessions['fixture']=replacement
            queue.put_nowait(replacement)
    class Log:
        def exists(self):return True
        def read_text(self,**kwargs):return RAW.replace('uptime_ms=123456',f'uptime_ms={100000+int(now[0]*1000)}')
        def stat(self):return SimpleNamespace(st_mtime=idle.time.time())
    class Directory:
        def __truediv__(self,name):assert name=='serial.log';return Log()
    async def pause(delay):now[0]+=delay
    async def rss(process=None):return 16384 if process else 32768
    monkeypatch.setattr(idle,'process_rss',rss)
    first=Session();server.sessions['fixture']=first
    result={'microphone_samples':0}
    def mark(kind,**fields):
        # The real bench supplies its own elapsed_ms; a helper must use a
        # different name for its phase-relative duration.
        return dict(kind=kind,elapsed_ms=0,**fields)
    last=await idle.run_idle(seconds=seconds,server=server,first=first,ready=queue,
        device_id='fixture',result=result,directory=Directory(),native=SimpleNamespace(returncode=None),
        monitor=SimpleNamespace(returncode=None),capture_started=asyncio.Event(),mark=mark,
        clock=lambda:now[0],pause=pause)
    state=result['idle']
    assert last is sessions[-1] and all(s._closed for s in sessions[:-1])
    assert len(state['reconnects'])==expected_reconnects and len(sessions)==expected_reconnects+1
    assert len(state['renewals_per_session'])==len(sessions) and all(state['renewals_per_session'])
    assert state['protocol_pass'] and state['elapsed_ms']>=seconds*1000
    assert not state['cumulative_heap_recovery_verified'] and len(state['snapshots'])<1024
    assert all(r['duration_ms']==1000 for r in state['reconnects'])
