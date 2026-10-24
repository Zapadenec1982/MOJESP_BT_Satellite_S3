# Continuous Bluetooth direct PCM

Opt in when building the P4 application:

```sh
idf.py -D MODESP_AUDIO_BT_DIRECT_CONTINUOUS=ON -D MODESP_AUDIO_DIAGNOSTIC_BT_DIRECT=OFF -D MODESP_AUDIO_DIAGNOSTIC_BT_FIXED_RATIO=OFF build
```

The option defaults to OFF. It selects direct PCM at equal announced/output rates,
bypasses ASRC and disables live P4 clock trimming irrespective of the persisted
clock-follow setting. Format changes, DSP, volume and DAC output stay in the normal
path. It has no diagnostic time limit or permanent loss latch. On an underrun it
keeps receiving data and resumes after the ordinary prefill; this can cause a brief
rebuffer gap and does not promise gap-free playback with independent clocks.

The separate BT_DIRECT and BT_FIXED_RATIO diagnostic options retain their bounded
60-second windows. They cannot be combined with continuous playback. Unequal rates
wait for the normal format handshake instead of silently selecting ASRC.

Validation: `python tools/tests/test_bt_diagnostic.py` compiles and executes the
production pull/stop methods in normal, diagnostic direct, fixed-ratio and continuous
modes. Continuous coverage includes 125 simulated seconds, repeated underrun/refill
cycles and format-mismatch recovery, without ASRC or clock-steering calls.

On 28 September 2026 the integrated working-tree firmware was observed for 150 s
at LHDC V5 48 kHz / 32 bits: playback continued beyond 60 s, P4 underruns remained
zero and ASRC blocks stayed zero. This was not a clean-commit or acoustic acceptance
test, and does not establish that the original crackling problem is fixed.
