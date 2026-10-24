"""Exercise the serial download protocol without hardware or pyserial."""
import io
import struct
import wave

import pytest
from mic_capture import Download


def wav_bytes(pcm):
    stream = io.BytesIO()
    with wave.open(stream, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(48000)
        wav.writeframes(pcm)
    return stream.getvalue()


def end_line(payload):
    checksum = 0x811C9DC5
    for byte in payload:
        checksum = ((checksum ^ byte) * 0x01000193) % (1 << 32)
    return f"@MIC_END fnv1a={checksum:08x}"


def begin(download, payload):
    assert download.feed(f"@MIC_BEGIN bytes={len(payload)} rate=48000") is False


def chunk(download, offset, payload):
    return download.feed(f"@MIC_DATA {offset} {payload.hex()}")


@pytest.fixture
def payload():
    return wav_bytes(struct.pack("<5h", -32768, -1, 0, 1, 32767))


@pytest.mark.parametrize("chunk_size", [1, 7, 44, 256])
def test_valid_wav_completes_only_at_end(payload, chunk_size):
    download = Download()
    assert download.feed("I (10) audio: starting playback") is False
    begin(download, payload)
    for offset in range(0, len(payload), chunk_size):
        # UART logs may surround the protocol and headers may cross chunks.
        line = f"I (20) mic: @MIC_DATA {offset} {payload[offset:offset + chunk_size].hex()}\r\n"
        assert download.feed(line) is False
        assert download.feed("@MIC_STATUS state=ready") is False
    assert download.data == payload
    assert download.feed(end_line(payload)) is True
    assert download.meta["bytes"] == str(len(payload))
    with wave.open(io.BytesIO(download.data), "rb") as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, 48000)
        assert wav.getnframes() == 5
        assert wav.readframes(5) == struct.pack("<5h", -32768, -1, 0, 1, 32767)


@pytest.mark.parametrize("offset", [0, 7, 9, 16, -1], ids=["duplicate", "overlap", "gap", "skipped-chunk", "negative"])
def test_gap_or_out_of_order_chunk_rejected(payload, offset):
    download = Download()
    begin(download, payload)
    assert chunk(download, 0, payload[:8]) is False
    with pytest.raises(ValueError, match="missing or out-of-order capture chunk"):
        chunk(download, offset, payload[8:16])
    assert download.data == payload[:8]


@pytest.mark.parametrize("corrupt_payload", [False, True], ids=["bad-checksum", "altered-sample"])
def test_checksum_corruption_rejected(payload, corrupt_payload):
    download = Download()
    begin(download, payload)
    damaged = payload[:-1] + bytes([payload[-1] ^ 1])
    assert chunk(download, 0, damaged if corrupt_payload else payload) is False
    with pytest.raises(ValueError, match="capture checksum mismatch"):
        download.feed(end_line(payload if corrupt_payload else damaged))


@pytest.mark.parametrize("received", [0, 12, 44, 53])
def test_truncated_download_rejected_at_end(payload, received):
    download = Download()
    begin(download, payload)
    if received:
        assert chunk(download, 0, payload[:received]) is False
    # A matching checksum for the short data must not bypass the size check.
    with pytest.raises(ValueError, match="truncated capture"):
        download.feed(end_line(payload[:received]))


@pytest.mark.parametrize("size", [-1, 0, 43, 192045, 2**64])
def test_declared_size_outside_bounds_rejected(size):
    with pytest.raises(ValueError, match="invalid capture size"):
        Download().feed(f"@MIC_BEGIN bytes={size}")


@pytest.mark.parametrize("samples", [0, 96000], ids=["minimum-44-bytes", "maximum-192044-bytes"])
def test_exact_size_bounds_accepted(samples):
    payload = wav_bytes(b"\x01\x80" * samples)
    assert len(payload) == 44 + samples * 2
    download = Download()
    begin(download, payload)
    for offset in range(0, len(payload), 256):
        assert chunk(download, offset, payload[offset:offset + 256]) is False
    assert download.feed(end_line(payload)) is True
    assert download.data == payload


def test_chunk_exceeding_declared_size_rejected(payload):
    download = Download()
    begin(download, payload)
    assert chunk(download, 0, payload[:-1]) is False
    with pytest.raises(ValueError, match="capture exceeds declared size"):
        chunk(download, len(payload) - 1, payload[-1:] + b"\x00")


@pytest.mark.parametrize("line,message", [
    ("@MIC_DATA 0 00", "data without header"),
    ("@MIC_END fnv1a=811c9dc5", "truncated capture"),
])
def test_header_required(line, message):
    with pytest.raises(ValueError, match=message):
        Download().feed(line)


@pytest.mark.parametrize("offset", [0, 8], ids=["riff", "wave"])
def test_non_wav_rejected_even_with_valid_checksum(payload, offset):
    invalid = bytearray(payload)
    invalid[offset] ^= 1
    download = Download()
    begin(download, invalid)
    assert chunk(download, 0, invalid) is False
    with pytest.raises(ValueError, match="capture is not WAV"):
        download.feed(end_line(invalid))


def test_new_begin_discards_previous_partial_download(payload):
    download = Download()
    begin(download, payload)
    assert chunk(download, 0, payload[:17]) is False
    replacement = wav_bytes(b"\x34\x12")
    begin(download, replacement)
    assert download.data == b""
    assert chunk(download, 0, replacement) is False
    assert download.feed(end_line(replacement)) is True
    assert download.data == replacement
