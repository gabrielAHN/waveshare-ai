"""mDNS/DNS-SD advertisement ``_waveshare-ai._tcp`` so boards can find this bridge from Settings.

TXT: ``v=1``, ``name=<display name>``, ``fp=<SHA-256 of the TLS leaf certificate DER, hex>``.
The fingerprint is public (it identifies, it does not authenticate); the board pins it and the
operator confirms the 6-digit comparison code before any board is trusted.
"""
import socket

SERVICE_TYPE = '_waveshare-ai._tcp.local.'


def _label(name):
    text = ''.join(ch for ch in name if 0x20 <= ord(ch) < 0x7f and ch not in '.\\').strip()
    return text[:32] or 'Waveshare AI bridge'


def service_info(name, ip, port, fingerprint):
    from zeroconf import ServiceInfo
    label = _label(name)
    host = 'waveshare-ai-' + fingerprint[:4].hex() + '.local.'
    return ServiceInfo(SERVICE_TYPE, f'{label}.{SERVICE_TYPE}', addresses=[socket.inet_aton(ip)], port=port,
                       properties={'v': '1', 'name': label, 'fp': fingerprint.hex()}, server=host)


class Advertiser:
    """Registers the service on the bind interface only; ``close()`` unregisters."""

    def __init__(self, name, ip, port, fingerprint):
        self.info = service_info(name, ip, port, fingerprint)
        self.ip = ip
        self.zc = None

    async def start(self):
        from zeroconf import IPVersion
        from zeroconf.asyncio import AsyncZeroconf
        self.zc = AsyncZeroconf(interfaces=[self.ip], ip_version=IPVersion.V4Only)
        await self.zc.async_register_service(self.info, allow_name_change=True)
        return self

    async def close(self):
        if self.zc is not None:
            try:
                await self.zc.async_unregister_service(self.info)
            finally:
                await self.zc.async_close()
                self.zc = None
