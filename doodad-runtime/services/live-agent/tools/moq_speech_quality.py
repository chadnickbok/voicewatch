"""Fixed-fixture recognition gate; returns no provider text or audio.

Policy v1 follows the physical no-loss baseline's six recognized words. Require
zero word errors without impairment; allow one error with impairment, while
always retaining the ordered request target. This is a narrow fixture gate,
not a general intelligibility score or a replacement for acoustic measurements.
"""
import re

FIXTURE_PHRASE = 'Please read my next exercise set.'
REFERENCE_WORDS = ('please', 'read', 'my', 'next', 'exercise', 'set')
MAX_CHARACTERS = 4096
MAX_WORDS = 256


def score(transcript, *, impaired):
    """Bounded word-level Levenshtein distance, ignoring case/punctuation.

    Empty, non-text and oversized responses fail closed. A match to one keyword
    or a best-of-multiple-transcripts selection cannot establish fixture quality.
    The caller must require every admitted completion of the turn to pass.
    """
    result = dict(policy_version=1, reference_words=len(REFERENCE_WORDS),
                  hypothesis_words=0, word_errors=None, word_error_rate=None,
                  max_word_errors=int(bool(impaired)), critical_phrase_matched=False,
                  bounds_exceeded=False, pass_=False)
    if not isinstance(transcript, str) or len(transcript) > MAX_CHARACTERS:
        result['bounds_exceeded'] = True
        return result
    words = re.findall(r'[a-z0-9]+', transcript.lower())
    result['hypothesis_words'] = len(words)
    if len(words) > MAX_WORDS:
        result['bounds_exceeded'] = True
        return result
    # Six columns regardless of hypothesis length; no alignment or words escape.
    previous = list(range(len(REFERENCE_WORDS) + 1))
    for row, word in enumerate(words, 1):
        current = [row]
        for column, reference in enumerate(REFERENCE_WORDS, 1):
            current.append(min(previous[column] + 1, current[-1] + 1,
                               previous[column - 1] + (word != reference)))
        previous = current
    errors = previous[-1]
    critical = any(tuple(words[i:i + 3]) == REFERENCE_WORDS[-3:]
                   for i in range(len(words) - 2))
    result.update(word_errors=errors, word_error_rate=round(errors / len(REFERENCE_WORDS), 5),
                  critical_phrase_matched=critical,
                  pass_=critical and errors <= result['max_word_errors'])
    return result


def all_pass(scores):
    """Missing results and any failed current-turn completion fail the gate."""
    return bool(scores) and all(value['pass_'] for value in scores)
