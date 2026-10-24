"""Run port/buffered_audio.c's wrappers against mocked upstream calls.

A buffered (AirPlay 2) packet names its encoding in its SSRC, bytes 8-11. A packet with
SSRC 0 carries no audio (shairport-sync skips it) and must not reach the decoder; any other
packet goes on to the real decryptor exactly as before, with an unknown SSRC or a switch
between known formats said once per run. A frame the decoder refuses is said with its
length, the last packet's SSRC and its first bytes.
Direct run: python tools/tests/test_airplay_buffered_ssrc.py
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

try:
    from .test_airplay_mode import PORT, compiler_command, production
except ImportError:
    from test_airplay_mode import PORT, compiler_command, production


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { int type; uint8_t key[32]; } audio_encrypt_t;
typedef struct { char codec[32]; int sample_rate; int channels; } audio_format_t;
typedef struct audio_stream {
    const void *ops; int type; bool running;
    audio_format_t format; audio_encrypt_t encrypt; void *ctx;
} audio_stream_t;
typedef struct { int kind; } audio_decoder_t;
typedef struct { int channels; } audio_decode_info_t;
static char logs[16][200];
static int nlogs;
#define ESP_LOGW(tag, ...) \
    do { (void)(tag); assert(nlogs < 16); snprintf(logs[nlogs++], 200, __VA_ARGS__); } while (0)
static int decrypts;
static const audio_encrypt_t *decrypt_key;
int __real_audio_crypto_decrypt_buffered(const audio_encrypt_t *encrypt, const uint8_t *packet,
                                         size_t packet_len, uint8_t *output, size_t cap) {
    (void)packet; (void)packet_len; (void)output; (void)cap;
    ++decrypts; decrypt_key = encrypt; return 5;
}
int __real_audio_decoder_decode(audio_decoder_t *decoder, const uint8_t *input, size_t len,
                                int16_t *output, size_t cap, audio_decode_info_t *info) {
    (void)decoder; (void)len; (void)output; (void)cap; (void)info;
    return input && input[0] == 0xEE ? -1 : 10;
}
'''

MAIN = r'''
static audio_stream_t stream;
static uint8_t out[64];
static int send(uint32_t seq, uint32_t ssrc, size_t len) {
    uint8_t p[64] = {0x80, (uint8_t)(seq >> 16), (uint8_t)(seq >> 8), (uint8_t)seq,
                     0, 0, 0, 1,
                     (uint8_t)(ssrc >> 24), (uint8_t)(ssrc >> 16), (uint8_t)(ssrc >> 8),
                     (uint8_t)ssrc};
    return __wrap_audio_crypto_decrypt_buffered(&stream.encrypt, p, len, out, sizeof(out));
}
static void expect(int before, int decrypted, int logged, const char *needle) {
    assert(decrypts == before + decrypted);
    assert(nlogs == logged);
    if (needle) assert(strstr(logs[nlogs - 1], needle));
}
int main(void) {
    strcpy(stream.format.codec, "AAC"); stream.format.sample_rate = 44100;
    int d;
    d = decrypts; assert(send(1, 0x16000000, 40) == 5); expect(d, 1, 0, NULL);
    assert(decrypt_key == &stream.encrypt);              /* the stream's own key goes on */
    /* SSRC 0: refused before decryption, said once for the run. */
    d = decrypts; assert(send(2, 0, 40) == -1); expect(d, 0, 1, "packet 2: SSRC 0");
    d = decrypts; assert(send(3, 0, 40) == -1); expect(d, 0, 1, NULL);
    /* Back to the stream's format: decoded, nothing said. */
    d = decrypts; assert(send(4, 0x16000000, 40) == 5); expect(d, 1, 1, NULL);
    /* Another known format: decoded as before, the switch and SETUP's format said. */
    d = decrypts; assert(send(5, 0x17000000, 40) == 5);
    expect(d, 1, 2, "AAC 48 kHz stereo after AAC 44.1 kHz stereo");
    assert(strstr(logs[1], "SETUP's AAC 44100 Hz"));
    /* Unknown: decoded as before, said once for the run. */
    d = decrypts; assert(send(6, 0x12345678, 40) == 5); expect(d, 1, 3, "0x12345678 is not");
    d = decrypts; assert(send(7, 0x12345678, 40) == 5); expect(d, 1, 3, NULL);
    /* Too short to carry an SSRC: straight to the real one, which refuses it. */
    d = decrypts; assert(send(8, 0, 8) == 5); expect(d, 1, 3, NULL);
    /* Decoder wrapper: a refused frame names itself, a decoded one says nothing. */
    d = decrypts; assert(send(9, 0x16000000, 40) == 5);
    const uint8_t bad[3] = {0xEE, 0x01, 0x02}, good[2] = {0x21, 0x10};
    audio_decoder_t dec = {0};
    int16_t pcm[8];
    assert(__wrap_audio_decoder_decode(&dec, good, sizeof(good), pcm, 8, NULL) == 10);
    assert(nlogs == 4);   /* 0x16000000 after 0x17000000 was the 4th line */
    assert(__wrap_audio_decoder_decode(&dec, bad, sizeof(bad), pcm, 8, NULL) == -1);
    assert(nlogs == 5);
    assert(strstr(logs[4], "frame of 3 bytes refused (last packet 9, SSRC 0x16000000): ee 01 02"));
    return 0;
}
'''


class BufferedSsrcTests(unittest.TestCase):
    def test_ssrc_zero_is_not_decoded_and_the_rest_goes_on(self):
        with tempfile.TemporaryDirectory(prefix="airplay-ssrc-") as temp:
            directory = Path(temp)
            source = directory / "buffered_audio.c"
            source.write_text("\n\n".join([
                PREFIX, production(PORT / "buffered_audio.c"), MAIN]), encoding="utf-8")
            exe = directory / ("buffered_audio" + (".exe" if os.name == "nt" else ""))
            command, env = compiler_command(source, exe)
            built = subprocess.run(command, cwd=directory, env=env,
                                   capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], cwd=directory, env=env,
                                 capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
