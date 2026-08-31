import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1]/'tools/verify_moq_shell.py'
spec = importlib.util.spec_from_file_location('verify_moq_shell', path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ShellEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.pcm = bytes(38400)  # Synthetic silence, never private microphone data.
        self.serial = '\n'.join(f'ECHOPCM offset={n} hex={bytes(320).hex()}' for n in range(0,19200,160))
        self.serial += '''
[host] app started; instance remains resident
[display] internal_free=120000 internal_min=100000 internal_largest=40000 max_render_us=8000 max_flush_us=1000
I (100) shell-moq-test: event kind=3 session=1 capture=71 response=0 samples=19200 first=0 end=62 cancelled=0 error=0
I (200) shell-moq-test: event kind=7 session=1 capture=71 response=1 samples=19200 first=0 end=61 cancelled=0 error=0
SHELL_MOQ_FINAL pass=1
'''
        self.result = dict(test_pass=True, changed_offset=65536, restoration_required=False)
        self.peer = 'PASS: physical microphone decoded/re-encoded through reference audio; input_samples=19200 response_samples=19200 paced_ms=20'

    def check(self, serial=None, expected=None):
        return module.analyze(self.serial if serial is None else serial, self.pcm if expected is None else expected,
                              self.pcm, self.peer, self.result)

    def test_complete_synthetic_artifact(self):
        self.assertTrue(self.check()['media_exchange_pass'])

    def test_missing_and_duplicate_pcm_rejected(self):
        with self.assertRaises(ValueError): self.check(self.serial.replace('ECHOPCM offset=0 ', 'removed offset=0 '))
        with self.assertRaises(ValueError): self.check(self.serial+'\nECHOPCM offset=0 hex='+bytes(320).hex())

    def test_wrong_owner_or_tail_rejected(self):
        self.assertFalse(self.check(self.serial.replace('capture=71', 'capture=72'))['media_exchange_pass'])
        self.assertFalse(self.check(self.serial.replace('end=62', 'end=61'))['media_exchange_pass'])

    def test_bad_sample_and_incomplete_dma_rejected(self):
        self.assertFalse(self.check(expected=b'\x02\x00'+self.pcm[2:])['media_exchange_pass'])
        self.assertFalse(self.check(self.serial.replace('SHELL_MOQ_FINAL pass=1',''))['media_exchange_pass'])

    def test_memory_shortfall_is_reported_separately(self):
        report=self.check(self.serial.replace('internal_min=100000','internal_min=95000'))
        self.assertTrue(report['media_exchange_pass'])
        self.assertFalse(report['memory']['diagnostic_meets_proposed_floor'])


if __name__ == '__main__': unittest.main()
