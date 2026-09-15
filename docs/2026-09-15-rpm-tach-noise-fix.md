# RPM reads high + jittery — diagnosis & fix (TIM3 input-capture filter)

**Status:** Lever-1 change **APPLIED in-branch** (`main.c` + `.ioc`), host suite 65/65 green. **Not yet built/flashed** — flashing needs bench/SWD, and the before/after spread check (`docs/bench/2026-09-15-rpm-tach-bench-datasheet.md`) rides along on that same bench visit. Justified now because the 2026-09-15 readings already prove noise (75 RPM jitter at a constant true RPM), and the filter is one-directional (rejects only sub-2µs glitches, cannot drop a real edge, doesn't touch the math or the `6.8125` constant). The **`6.8125` recalibration stays HELD** until a clean de-noised reading exists.
**Reported:** 2026-09-15 bench, unit TRUCK-001 (.86), fw 1.01.01 (`apu_fw_version` 10101).

---

## Symptom

Digital tachometer vs. dashboard (Variscite) at a **steady** engine speed:

| Digital tach (true) | Variscite shows | error |
|---|---|---|
| 2452.8 | 2663 | +8.6% |
| 2453.4 | 2738 | +11.6% |

True RPM was essentially constant (0.6 apart), but the Variscite reading was **9–12% high** and **jittered 75 RPM (2.8%)** between the two samples. **Bias + jitter at constant RPM.**

---

## The measurement chain (verified)

1. **Agent** passes firmware **reg 38** straight through as `rpm` — *no rescaling* (`gobi-agent/main.c:434`, `RD(REG_RPM); t->rpm = v;`). So the number originates in the STM32.
2. **Firmware math** (`App/services/rpm_calc.c` / `.h`): `RPM = 6.8125 × freq`, `freq = 500 kHz / period_ticks`. The `6.8125` (= 109/16, implying **~8.81 pulses/rev**) was a **bench re-fit on 2026-09-02**, bumped up from the PIC's original integer `6`.
3. **Period measurement** (`cube/Core/Src/drv_rpm_tim3.c`, `MX_TIM3_Init` at `cube/Core/Src/main.c:474-477`):
   - TIM3_CH1 (PA6) input-capture, **captures every rising edge** (`ICPrescaler = DIV1`).
   - **`ICFilter = 0` — no hardware glitch filter.**
   - Light EMA (¼ weight per new sample) and a single guard that drops only *exact same-tick* double-captures (`d == 0`). Any spurious edge ≥1 tick after a real one is folded in as a real (short) period.

## Why this is noise, not calibration

- A wrong constant is **stable** — it cannot produce 2.8% jitter at a constant true RPM. So the **period measurement itself is unstable** ⇒ spurious/extra edges are being captured.
- `RPM = k / period` is **convex**: extra *short* intervals drag the averaged period **down**, which biases RPM **up**. So the *same* unfiltered-edge noise produces **both** the high bias and the jitter.
- Back-calculation: the firmware measured ≈391 Hz and ≈402 Hz, but the 8.81-pulse/rev calibration expects ≈360 Hz at 2453 RPM — i.e. it is seeing **~8–12% more edges** than there should be. Consistent with spurious edges on an unfiltered input.
- `ICFilter = 0` + capture-every-edge + only a same-tick guard is exactly the setup that lets EMI/ringing on the BJT-buffered PA6 tach line inflate and destabilize the reading.

**Do NOT just re-tweak `6.8125`.** Re-fitting to today's two points would (a) fit noisy data, (b) not remove the jitter, and (c) repeat the 2026-09-02 move that produced the odd 8.81-pulse/rev constant. Fix the noise first; only then re-check the constant against clean readings.

---

## Fix — Lever 1: enable the TIM3 input-capture digital filter

The IC filter requires the input to be stable for **N** samples at **f_SAMPLING** before validating an edge — a hardware debounce. It delays *every* edge equally, so the edge-to-edge **period is unchanged** (no new bias), while sub-window glitches are rejected.

**Assumptions:** TIM3 kernel clock = 64 MHz (APB timer prescaler 1); `ClockDivision = DIV1` ⇒ `f_DTS = 64 MHz`. (If the APB timer clock differs, the µs figures below scale accordingly.)

| `ICFilter` | sampling | N | rejection window | note |
|---|---|---|---|---|
| `0` (now) | — | — | none | current: unfiltered |
| `0x7` | f_DTS/4 = 16 MHz | 8 | **0.5 µs** | mild |
| **`0xC` (12)** | f_DTS/16 = 4 MHz | 8 | **2 µs** | **recommended start** |
| `0xF` (15) | f_DTS/32 = 2 MHz | 8 | 4 µs | stronger, if jitter persists |

**Margin:** even at a generous few-thousand RPM the tach pulse spacing is >1 ms and the high time is hundreds of µs; a 2 µs window is <0.5% of that, so real edges are never rejected. Above `ENGINE_RPM_LOW_LIMIT` (1000) the period is ≥6 ms.

### Change (generated code)

`cube/Core/Src/main.c`, in `MX_TIM3_Init()`:

```c
-  sConfigIC.ICFilter = 0;
+  sConfigIC.ICFilter = 12;   /* f_DTS/16, N=8 -> ~2 us glitch reject; see docs/2026-09-15-rpm-tach-noise-fix.md */
```

### Keep it across CubeMX regen (`.ioc`)

`MX_TIM3_Init` is regenerated from `cube/g0b1-apu.ioc`. To stop a future regen from reverting the filter to 0, add the channel filter to the TIM3 block:

```
 TIM3.Channel-Input_Capture1_from_TI1=TIM_CHANNEL_1
-TIM3.IPParameters=Channel-Input_Capture1_from_TI1,Prescaler
+TIM3.IPParameters=Channel-Input_Capture1_from_TI1,Prescaler,ICFilter_CH1
 TIM3.Prescaler=127
+TIM3.ICFilter_CH1=12
```

> ⚠️ Prior bring-up found TIM3 input-capture needed the **CubeMX GUI** (hand-drafted `.ioc` for TIM3-IC was unreliable). The authoritative path if CubeMX is reopened: set **Channel1 → Input Filter = 12** in the GUI and regenerate. The `.ioc` lines above are a best-effort hand-edit; the `main.c` one-liner is what actually runs on the current build.

---

## Fix — Lever 2 (host-testable follow-up, only if Lever 1 isn't enough)

A software reject for implausibly-short periods, in the capture ISR (`drv_rpm_tim3.c`), once `s_seeded`: if a new delta `d` is far below the running average (e.g. `d < s_period * 5 / 8`), treat it as a spurious extra edge and **skip** it (don't fold into the EMA, don't advance `s_last_ccr` past a real edge — design carefully). This is unit-testable against a synthetic edge stream in `Tests/` (extend `test_rpm_calc.c` / add a driver-logic test). Deferred until the bench shows Lever 1 alone is insufficient.

---

## Verification

- The RPM **math** (`rpm_from_period_ticks`) is unchanged, so the existing host suite (`Tests/test_rpm_calc.c`, `test_rpm.c`) stays green — run `Tests/` to confirm no regression from the `main.c`/`.ioc` edit.
- **Bench:** fill `docs/bench/2026-09-15-rpm-tach-bench-datasheet.md` *before* the change (baseline) and *after* flashing (with-filter). Success = the Variscite reading tracks the digital tach with **spread collapsed to ≲1%** and bias within a few %. If a residual constant bias remains after the jitter is gone, *then* re-fit `RPM_FREQ_MULT` from the clean data.
- Definition of done: dashboard RPM within ~2% of the digital tach and stable across the run, at every steady operating point.
