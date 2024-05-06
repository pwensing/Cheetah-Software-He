"""LCM type definitions
This file automatically generated by lcm.
DO NOT MODIFY BY HAND!!!!
"""

try:
    import cStringIO.StringIO as BytesIO
except ImportError:
    from io import BytesIO
import struct

class rpc_outputs_t(object):
    __slots__ = ["cpu_opt_time_microseconds", "t_sent", "time_start", "dt_pred", "x_opt", "u_opt", "p_opt"]

    __typenames__ = ["double", "double", "double", "double", "double", "double", "double"]

    __dimensions__ = [None, None, None, [5], [60], [120], [12]]

    def __init__(self):
        self.cpu_opt_time_microseconds = 0.0
        self.t_sent = 0.0
        self.time_start = 0.0
        self.dt_pred = [ 0.0 for dim0 in range(5) ]
        self.x_opt = [ 0.0 for dim0 in range(60) ]
        self.u_opt = [ 0.0 for dim0 in range(120) ]
        self.p_opt = [ 0.0 for dim0 in range(12) ]

    def encode(self):
        buf = BytesIO()
        buf.write(rpc_outputs_t._get_packed_fingerprint())
        self._encode_one(buf)
        return buf.getvalue()

    def _encode_one(self, buf):
        buf.write(struct.pack(">ddd", self.cpu_opt_time_microseconds, self.t_sent, self.time_start))
        buf.write(struct.pack('>5d', *self.dt_pred[:5]))
        buf.write(struct.pack('>60d', *self.x_opt[:60]))
        buf.write(struct.pack('>120d', *self.u_opt[:120]))
        buf.write(struct.pack('>12d', *self.p_opt[:12]))

    def decode(data):
        if hasattr(data, 'read'):
            buf = data
        else:
            buf = BytesIO(data)
        if buf.read(8) != rpc_outputs_t._get_packed_fingerprint():
            raise ValueError("Decode error")
        return rpc_outputs_t._decode_one(buf)
    decode = staticmethod(decode)

    def _decode_one(buf):
        self = rpc_outputs_t()
        self.cpu_opt_time_microseconds, self.t_sent, self.time_start = struct.unpack(">ddd", buf.read(24))
        self.dt_pred = struct.unpack('>5d', buf.read(40))
        self.x_opt = struct.unpack('>60d', buf.read(480))
        self.u_opt = struct.unpack('>120d', buf.read(960))
        self.p_opt = struct.unpack('>12d', buf.read(96))
        return self
    _decode_one = staticmethod(_decode_one)

    def _get_hash_recursive(parents):
        if rpc_outputs_t in parents: return 0
        tmphash = (0xc89e670023d70089) & 0xffffffffffffffff
        tmphash  = (((tmphash<<1)&0xffffffffffffffff) + (tmphash>>63)) & 0xffffffffffffffff
        return tmphash
    _get_hash_recursive = staticmethod(_get_hash_recursive)
    _packed_fingerprint = None

    def _get_packed_fingerprint():
        if rpc_outputs_t._packed_fingerprint is None:
            rpc_outputs_t._packed_fingerprint = struct.pack(">Q", rpc_outputs_t._get_hash_recursive([]))
        return rpc_outputs_t._packed_fingerprint
    _get_packed_fingerprint = staticmethod(_get_packed_fingerprint)

    def get_hash(self):
        """Get the LCM hash of the struct"""
        return struct.unpack(">Q", rpc_outputs_t._get_packed_fingerprint())[0]

