"""Compatibility imports; importing neutral audio/session code never loads aiortc.

New transports import audio/session directly. WebRTC remains an explicit legacy
adapter and is loaded only when its classes are requested.
"""
from .audio import _PacketPacer
from .session import DownlinkUtteranceBinding, WatchActionError
from .host_network import local_ipv4, keep_host_candidate


def __getattr__(name: str):
    if name not in {"DownlinkAudioTrack", "WatchSession", "WatchTransportServer",
                    "WatchActionError", "local_ipv4", "keep_host_candidate"}:
        raise AttributeError(name)
    from . import transport_webrtc
    return getattr(transport_webrtc, name)
