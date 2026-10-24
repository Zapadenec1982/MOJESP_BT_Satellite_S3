"""Execute production Bluetooth pull/stop methods with mocked ports and ASRC.

Checks diagnostic routing and stop/latch behavior; hardware timing and the DSP/TX
outside these methods remain the bench's responsibility. Runs without pytest:
    python tools/tests/test_bt_diagnostic.py
"""
import glob
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


def compiler(source, exe, mode):
    env = os.environ.copy()
    cxx = shutil.which("c++") or shutil.which("g++")
    defines = [
        "MODESP_AUDIO_DIAGNOSTIC_BT_DIRECT=" + str(mode == "direct").replace("True", "1").replace("False", "0"),
        "MODESP_AUDIO_DIAGNOSTIC_BT_FIXED_RATIO=" + str(mode == "fixed").replace("True", "1").replace("False", "0"),
        "MODESP_AUDIO_BT_DIRECT_CONTINUOUS=" + str(int(mode == "continuous")),
    ]
    if cxx:
        return [cxx, "-std=c++17", "-O0", str(source), "-o", str(exe)] + ["-D"+d for d in defines], env
    bases = [os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
             os.environ.get("ProgramFiles", r"C:\Program Files")]
    setups = sorted(p for base in bases for p in glob.glob(str(Path(base) /
        "Microsoft Visual Studio/*/*/VC/Auxiliary/Build/vcvars64.bat")))
    if not setups:
        raise AssertionError("Bluetooth diagnostic regression requires a native C++ compiler")
    result = subprocess.run(f'call "{setups[-1]}" >nul && set', shell=True,
                            capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    for line in result.stdout.splitlines():
        key, sep, value = line.partition("=")
        if sep and key:
            env[key] = value
    cxx = shutil.which("cl", path=env.get("Path", env.get("PATH", "")))
    if not cxx:
        raise AssertionError("Visual Studio environment did not provide cl.exe")
    return [cxx, "/nologo", "/std:c++17", "/EHsc", "/Od", str(source),
            "/Fe:"+str(exe)] + ["/D"+d for d in defines], env


PREFIX = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

static int64_t now_us = 1;
int64_t esp_timer_get_time() { return now_us; }
struct Diagnostics {
    uint32_t rx_queue_overflows=0, tx_queue_overflows=0;
    uint32_t rx_short_reads=0, tx_short_writes=0, trim_attempts=0, trim_applied=0;
};
struct Port {
    Diagnostics d;
    Diagnostics diagnostics() { return d; }
    bool can_trim() { return true; } // Diagnostic must override even a usable clock servo.
};
struct Health {
    bool underrun() { return false; }
    bool observe(uint32_t,uint32_t,int32_t,uint32_t) { return false; }
};
struct Resample {};
struct ResampleResult { unsigned input_used, output_generated; };
static unsigned process_calls=0, reset_calls=0, steer_calls=0;
static bool short_output=false;
static double process_ratio=0;
void resampleReset(Resample*) { ++reset_calls; }
unsigned resampleGetRequiredSamples(Resample*,int n,double) { return n; }
ResampleResult resampleProcessInterleaved(Resample*,const float* in,int,float* out,int n,double ratio) {
    ++process_calls; process_ratio=ratio;
    unsigned generated=n-(short_output ? 1 : 0);
    std::copy_n(in,generated*2,out);
    return {static_cast<unsigned>(n),generated};
}
static float s_asrc_in[240*4];

struct AudioModule {
    Port rx, tx;
    Port *bt_=&rx, *main_=&tx;
    Resample converter;
    Resample *asrc_=&converter;
    std::vector<float> storage;
    float* bt_ring_;
    size_t bt_ring_frames_=48000;
    std::atomic<size_t> bt_head_{0}, bt_tail_{0};
    std::atomic<uint32_t> bt_fill_low_{UINT32_MAX}, bt_fill_high_{0};
    std::atomic<bool> bt_capture_{true};
    uint32_t bt_fmt_hz_=48000, out_rate_=48000, bt_seeded_hz_=0;
    uint32_t bt_primes_=0, bt_resyncs_=0, bt_short_=0, bt_asrc_blocks_=0;
    uint32_t bt_dropped_=0, bt_diag_dropped_=0, bt_servo_frames_=0;
    bool bt_diag_stopped_=false, bt_primed_=false, bt_clock_recover_=false;
    bool bt_clock_follow_=false, bt_clock_allowed_=true;
    bool bt_trim_dead_=false, bt_clock_unhealthy_=false;
    int64_t bt_diag_start_us_=0;
    Diagnostics bt_diag_rx_, bt_diag_tx_;
    Health bt_clock_health_;
    int32_t bt_trim_ppm_=0, bt_trim_got_=0, bt_err_sum_=0;
    uint16_t bt_err_n_=0;
    uint8_t bt_path_=0;
    float sig_gain_=0, in_rate_est_=48000;
    double asrc_ratio_=1, asrc_base_=0;
    AudioModule() : storage(96000), bt_ring_(storage.data()) {
        for(size_t i=0;i<48000;++i) {
            storage[2*i]=float(i)/65536;
            storage[2*i+1]=-float(i)/65536;
        }
        bt_tail_=47900; bt_head_=7200; // First output wraps the physical ring.
    }
    void reset_out_trim() { bt_trim_ppm_=bt_trim_got_=0; }
    void steer_out_clock(int32_t,uint32_t) { ++steer_calls; }
    bool pull_bt_block(float*,size_t);
    bool stop_bt_diagnostic(uint8_t);
};
'''

MAIN = r'''
int main() {
    float out[480];
    {
        AudioModule a;
        // Startup must prefill, without prematurely starting the 60 second timer.
        a.bt_head_=(a.bt_tail_+1000)%48000;
        assert(!a.pull_bt_block(out,240));
        assert(!a.bt_diag_stopped_ && !a.bt_diag_start_us_ && !a.bt_primes_);
        a.bt_head_=7200;
#if !MODESP_AUDIO_DIAGNOSTIC_BT_DIRECT && !MODESP_AUDIO_DIAGNOSTIC_BT_FIXED_RATIO && !MODESP_AUDIO_BT_DIRECT_CONTINUOUS
        a.bt_clock_allowed_=false;
#endif
        assert(a.pull_bt_block(out,240));
        assert(a.bt_tail_==140);
        for(size_t i=0;i<240;++i) {
            assert(out[2*i]==a.storage[2*((47900+i)%48000)]);
            assert(out[2*i+1]==a.storage[2*((47900+i)%48000)+1]);
        }
        assert(!a.bt_clock_follow_ && !steer_calls);
#if MODESP_AUDIO_DIAGNOSTIC_BT_DIRECT
        assert(a.bt_path_==BT_DIAG_DIRECT && !process_calls && !a.bt_asrc_blocks_);
#elif MODESP_AUDIO_BT_DIRECT_CONTINUOUS
        assert(a.bt_path_==BT_CONTINUOUS_DIRECT && !process_calls && !a.bt_asrc_blocks_);
#elif MODESP_AUDIO_DIAGNOSTIC_BT_FIXED_RATIO
        assert(a.bt_path_==BT_DIAG_FIXED_RATIO && process_calls==1 && process_ratio==1.0);
#else
        assert(a.bt_path_==2 && process_calls==1 && process_ratio!=1.0);
        now_us=60000002;
        assert(a.pull_bt_block(out,240)); // Normal build remains unbounded/adaptive.
#endif
    }
#if MODESP_AUDIO_DIAGNOSTIC_BT_DIRECT || MODESP_AUDIO_DIAGNOSTIC_BT_FIXED_RATIO
    for(int scenario=0;scenario<9;++scenario) {
        now_us=1; short_output=false;
        AudioModule a;
        assert(a.pull_bt_block(out,240));
        uint8_t reason=0;
        switch(scenario) {
        case 0: now_us+=60000000; reason=BT_DIAG_COMPLETE; break;
        case 1: a.out_rate_=44100; reason=BT_DIAG_BAD_RATE; break;
        case 2: a.bt_tail_=a.bt_head_-1; reason=BT_DIAG_UNDERRUN; break;
        case 3: a.bt_head_=(a.bt_tail_+25000)%48000; reason=BT_DIAG_RESYNC; break;
        case 4: ++a.bt_dropped_; reason=BT_DIAG_DROP; break;
        case 5: ++a.rx.d.rx_queue_overflows; reason=BT_DIAG_RX_LOSS; break;
        case 6: ++a.tx.d.tx_queue_overflows; reason=BT_DIAG_TX_LOSS; break;
        case 7: ++a.tx.d.tx_short_writes; reason=BT_DIAG_TX_LOSS; break;
        case 8: ++a.tx.d.trim_attempts; reason=BT_DIAG_TRIM; break;
        }
        const size_t tail=a.bt_tail_;
        assert(!a.pull_bt_block(out,240));
        assert(a.bt_path_==reason && a.bt_diag_stopped_ && !a.bt_primed_ && !a.bt_capture_);
        assert(a.bt_tail_==tail && !a.bt_resyncs_);
        // Refilled input and repaired conditions cannot silently restart the experiment.
        a.out_rate_=48000; a.bt_head_=(a.bt_tail_+8000)%48000;
        assert(!a.pull_bt_block(out,240));
        assert(a.bt_path_==reason && a.bt_primes_==1 && a.bt_tail_==tail);
    }
    {
        AudioModule a; a.bt_fmt_hz_=0;
        assert(!a.pull_bt_block(out,240) && a.bt_path_==BT_DIAG_BAD_RATE && !a.bt_primes_);
    }
#if MODESP_AUDIO_DIAGNOSTIC_BT_FIXED_RATIO
    {
        AudioModule a; now_us=1; short_output=true;
        assert(!a.pull_bt_block(out,240));
        assert(a.bt_diag_stopped_ && a.bt_path_==BT_DIAG_SHORT && a.bt_short_==1);
    }
#endif
#endif
#if MODESP_AUDIO_BT_DIRECT_CONTINUOUS
    {
        AudioModule a; now_us=1;
        assert(a.pull_bt_block(out,240));
        // Feed 125 seconds at 48 kHz: no diagnostic deadline or hidden ASRC work.
        for(int block=0;block<25000;++block) {
            now_us+=5000;
            a.bt_head_=(a.bt_tail_+8000)%48000;
            assert(a.pull_bt_block(out,240));
        }
        assert(now_us>120000000 && !a.bt_diag_stopped_ && a.bt_capture_);
        assert(a.bt_path_==BT_CONTINUOUS_DIRECT && !a.bt_clock_follow_);
        assert(!process_calls && !steer_calls && !a.bt_asrc_blocks_);
        // Ordinary starvation must keep capture running and resume after prefill.
        for(int repeat=0;repeat<3;++repeat) {
            const auto primes=a.bt_primes_;
            a.bt_head_=(a.bt_tail_+239)%48000;
            assert(!a.pull_bt_block(out,240));
            assert(!a.bt_primed_ && !a.bt_diag_stopped_ && a.bt_capture_);
            a.bt_head_=(a.bt_tail_+1000)%48000;
            assert(!a.pull_bt_block(out,240));
            assert(a.bt_primes_==primes);
            a.bt_head_=(a.bt_tail_+8000)%48000;
            assert(a.pull_bt_block(out,240));
            assert(a.bt_primes_==primes+1 && a.bt_path_==BT_CONTINUOUS_DIRECT);
            assert(!a.bt_clock_recover_ && !a.bt_clock_unhealthy_);
        }
        // Unknown/mismatched format never falls back; corrected format can re-prime.
        for(uint32_t bad_rate : {0u,44100u}) {
            const auto tail=a.bt_tail_.load();
            a.bt_fmt_hz_=bad_rate;
            assert(!a.pull_bt_block(out,240) && a.bt_path_==BT_CONTINUOUS_BAD_RATE);
            assert(a.bt_tail_==tail && !a.bt_diag_stopped_ && a.bt_capture_);
            a.bt_fmt_hz_=48000; a.bt_head_=(a.bt_tail_+8000)%48000;
            assert(a.pull_bt_block(out,240));
        }
        // Retain normal resync and continue after counted transport losses.
        a.bt_head_=(a.bt_tail_+25000)%48000;
        ++a.rx.d.rx_queue_overflows; ++a.tx.d.tx_queue_overflows; ++a.bt_dropped_;
        assert(a.pull_bt_block(out,240));
        assert(a.bt_resyncs_==1 && !a.bt_diag_stopped_ && a.bt_capture_);
        assert(!process_calls && !steer_calls && !a.bt_asrc_blocks_);
    }
#endif
    return 0;
}
'''


class BtDiagnosticRegression(unittest.TestCase):
    def test_production_pull_in_all_modes(self):
        source = (ROOT / "modules/audio/src/audio_module.cpp").read_text(encoding="utf-8")
        constants = source[source.index("constexpr bool BT_DIAGNOSTIC"):
                           source.index("\n// The set point")]
        names = ["BT_TARGET_MS", "BT_TRACK_MS", "BT_CAPTURE_MS", "BT_RESYNC_MS",
                 "BT_SERVO_MS", "ASRC_MAX_DEV", "ASRC_CAPTURE"]
        constants += "\n" + "\n".join(next(line for line in source.splitlines()
            if line.startswith("constexpr ") and name+" " in line) for name in names)
        methods = "\n".join(function(source, signature) for signature in [
            "bool AudioModule::stop_bt_diagnostic(",
            "bool AudioModule::pull_bt_block(",
        ])
        with tempfile.TemporaryDirectory(prefix="bt-diagnostic-") as temp:
            folder = Path(temp)
            cpp = folder / "probe.cpp"
            cpp.write_text(PREFIX + constants + "\n" + methods + MAIN, encoding="utf-8")
            for mode in ["normal", "direct", "fixed", "continuous"]:
                with self.subTest(mode=mode):
                    exe = folder / ("probe-"+mode+(".exe" if os.name=="nt" else ""))
                    command, env = compiler(cpp, exe, mode)
                    built = subprocess.run(command, cwd=folder, env=env, capture_output=True,
                                           text=True, timeout=120)
                    self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
                    ran = subprocess.run([str(exe)], cwd=folder, env=env,
                                         capture_output=True, text=True, timeout=30)
                    self.assertEqual(ran.returncode, 0, ran.stdout+ran.stderr)


if __name__ == "__main__":
    unittest.main()
