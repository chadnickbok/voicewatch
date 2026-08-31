"""Local address discovery and legacy ICE candidate filtering."""
import re
import socket
import sys

def local_ipv4() -> str:
    """Return the LAN address used in the host-only ICE answer."""
    if sys.platform == "darwin":
        import subprocess

        wifi = subprocess.run(
            ["ipconfig", "getifaddr", "en0"], check=False, capture_output=True, text=True
        ).stdout.strip()
        if wifi:
            return wifi
        route = subprocess.run(
            ["route", "-n", "get", "default"], check=True, capture_output=True, text=True
        ).stdout
        match = re.search(r"^\s*interface:\s*(\S+)", route, re.MULTILINE)
        if match:
            address = subprocess.run(
                ["ipconfig", "getifaddr", match.group(1)],
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            if address:
                return address
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("192.0.2.1", 9))
        return str(probe.getsockname()[0])
    finally:
        probe.close()


def keep_host_candidate(sdp: str, address: str) -> str:
    filtered: list[str] = []
    kept = False
    for line in sdp.splitlines():
        if not line.startswith("a=candidate:"):
            filtered.append(line)
        elif not kept and f" {address} " in line:
            filtered.append(line)
            kept = True
    return "\r\n".join(filtered) + "\r\n"
