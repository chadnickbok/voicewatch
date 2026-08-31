"""Synthetic transcripts only: fixture acceptance must not hide word loss."""
import importlib.util
from pathlib import Path

import pytest

spec = importlib.util.spec_from_file_location('moq_speech_quality',
    Path(__file__).resolve().parents[1] / 'tools/moq_speech_quality.py')
quality = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quality)


def test_exact_baseline_ignores_case_and_punctuation_without_returning_text():
    result = quality.score('PLEASE, read my next exercise set!', impaired=False)
    assert result['word_errors'] == 0 and result['word_error_rate'] == 0
    assert result['pass_'] and result['critical_phrase_matched']
    assert all(value is None or type(value) in (bool, int, float) for value in result.values())


@pytest.mark.parametrize('transcript,errors,impaired_pass', [
    ('read my next exercise set', 1, True),  # deletion
    ('Please read the next exercise set', 1, True),  # substitution
    ('Please just read my next exercise set', 1, True),  # insertion
    ('Please read my last exercise set', 1, False),  # lost target, despite low WER
    ('exercise', 5, False),  # old keyword gate accepted this
    ('set', 5, False),
    ('Please read my exercise next set', 2, False),  # all six keywords, wrong order
    ('next exercise set', 3, False),
    ('', 6, False),
    ('Please read my next exercise set. Please read my next exercise set.', 6, False),
])
def test_insertions_deletions_substitutions_and_request_target(transcript, errors, impaired_pass):
    clean = quality.score(transcript, impaired=False)
    impaired = quality.score(transcript, impaired=True)
    assert clean['word_errors'] == impaired['word_errors'] == errors
    assert not clean['pass_']
    assert impaired['pass_'] is impaired_pass


@pytest.mark.parametrize('transcript', [None, 'x' * 4097, 'set ' * 257])
def test_bounds_fail_closed(transcript):
    result = quality.score(transcript, impaired=True)
    assert not result['pass_'] and result['bounds_exceeded']
    assert result['word_errors'] is None


def test_multiple_completions_cannot_hide_failure_and_new_turn_requires_new_score():
    passed = quality.score(quality.FIXTURE_PHRASE, impaired=False)
    failed = quality.score('set', impaired=False)
    assert quality.all_pass([passed])
    assert not quality.all_pass([])
    assert not quality.all_pass([failed, passed])
    assert not quality.all_pass([passed, failed])
