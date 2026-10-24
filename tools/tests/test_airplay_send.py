"""
tools/airplay_send.py builds its ALAC frames by hand — uncompressed "escape" frames, bit by bit —
and a frame that is one bit off is not an error anywhere: the receiver decodes it into noise, or
into silence, and the bench reports "no sound" with nothing to point at. These tests read the
frame back the way Apple's decoder does (ALACDecoder::Decode, the ID_CPE branch) and hold the
layout to it.
"""
import struct
from array import array

import airplay_send as tx  # tools/ is on sys.path (conftest.py)


class Bits:
    def __init__(self, data):
        self.v = int.from_bytes(data, "big")
        self.n = len(data) * 8
        self.pos = 0

    def read(self, k):
        self.pos += k
        return (self.v >> (self.n - self.pos)) & ((1 << k) - 1)


def frame_of(samples):
    return tx.alac_escape_frame(array("h", samples))


def test_frame_is_header_samples_and_end_rounded_to_bytes():
    f = frame_of([0] * (2 * tx.FRAMES))
    assert len(f) == (23 + 16 * 2 * tx.FRAMES + 3 + 7) // 8


def test_header_says_channel_pair_uncompressed_full_frame():
    b = Bits(frame_of([0] * (2 * tx.FRAMES)))
    assert b.read(3) == 1          # ID_CPE
    assert b.read(4) == 0          # element instance
    assert b.read(12) == 0         # unused
    assert b.read(1) == 0          # partial frame: no sample count follows
    assert b.read(2) == 0          # bytes shifted
    assert b.read(1) == 1          # escape: samples follow uncompressed


def test_samples_come_back_left_then_right_and_the_frame_ends():
    src = [(i * 37 - 5000) % 65536 - 32768 for i in range(2 * tx.FRAMES)]
    b = Bits(frame_of(src))
    b.read(23)
    got = []
    for _ in range(2 * tx.FRAMES):
        v = b.read(16)
        got.append(v - 65536 if v >= 32768 else v)
    assert got == src
    assert b.read(3) == 7          # ID_END


def test_sync_packet_is_twenty_bytes_in_the_receivers_layout():
    # audio_stream_realtime.c reads [4] the frame playing, [8] NTP time, [16] the next frame.
    pkt = struct.pack("!BBHIQI", 0x90, 0xD4, 7, 1000, tx.ntp_now(), 1000 + 88200)
    assert len(pkt) == 20 and pkt[1] & 0x7F == 0x54


def test_tone_has_no_seam_between_packets():
    t = tx.Tone(440, 10 * tx.FRAMES)
    a = t.next()
    b = t.next()
    # The last sample of one packet and the first of the next are one sample step apart on
    # the sine, not a restart of it.
    assert abs(b[0] - a[-2]) < 1500
