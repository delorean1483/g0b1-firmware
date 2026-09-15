# Bench data sheet — RPM tach vs. dashboard

Goal: decide **noise vs. calibration** for the high/jittery RPM reading, and collect clean data to (a) confirm the `ICFilter` fix and (b) re-fit the constant only if a residual bias remains. Plan: `docs/2026-09-15-rpm-tach-noise-fix.md`.

## Fill first

- Date / operator: ______________________
- Unit: TRUCK-001 (.86)   Firmware: ______ (Unit Info → APU Firmware; baseline = 1.01.01)
- Digital tach model: ______________________
- **Where is the digital tach measuring?** ☐ crank/engine pulley ☐ alternator ☐ belt ☐ other: ________
  *(The firmware constant bundles the alternator pulley ratio, so the reference point matters. We want **engine crank** RPM.)*
- How the engine speed is set: ☐ fixed governed idle ☐ changes under climate/battery load ☐ throttle-adjustable

## Reading the dashboard RPM

Read RPM from the dashboard (Diagnostics), **or** have it captured automatically: while you hold a steady speed, Claude can log `/var/lib/ecofleet/latest.json` `rpm` over SSH for ~30 s and report min / max / mean / spread. One-liner (run from the Mac):

```sh
ssh root@192.168.0.86 'for i in $(seq 1 30); do grep -o "\"rpm\":[0-9]*" /var/lib/ecofleet/latest.json; sleep 1; done' \
  | sed "s/[^0-9]//g" | sort -n | awk '{a[NR]=$1;s+=$1} END{print "n="NR" min="a[1]" max="a[NR]" mean="s/NR" spread="a[NR]-a[1]}'
```

Prefer the auto-capture for the **spread** — one glance hides the jitter.

---

## A. Baseline (BEFORE any firmware change)

At **each steady operating point you can hold** (don't force arbitrary RPMs — use whatever governed/loaded speeds the APU actually runs at), let it settle, then record:

| # | Operating point (e.g. idle / climate load) | Digital tach (true) | Variscite min | Variscite max | Variscite mean | spread (max−min) | bias % = (mean−true)/true |
|---|---|---|---|---|---|---|---|
| 1 |  |  |  |  |  |  |  |
| 2 |  |  |  |  |  |  |  |
| 3 |  |  |  |  |  |  |  |

### A1. Jitter hold — one point, ~30 s
- Operating point: ____________   Digital tach: ______ (steady? ☐)
- Variscite over 30 s: min ______ max ______ mean ______ **spread ______**

---

## Decision (read after Section A)

- **Spread large (≳2%) at a truly steady tach, and/or bias % changes between points → NOISE.** Proceed with the `ICFilter` fix (Lever 1). This matches the 2026-09-15 observation.
- **Spread small (≲1%) and bias % is a flat constant across all points → CALIBRATION.** Don't flash `ICFilter`; instead compute a new constant:
  `new RPM_FREQ_MULT ≈ 6.8125 × mean(true / Variscite-mean)` — and re-fit `109/16` to the nearest clean rational. (Record the ratios here: __________________.)

---

## B. After the `ICFilter` fix (flash, then repeat)

Firmware with `ICFilter=12`: ______ (new version) — flashed ☐

| # | Operating point | Digital tach (true) | Variscite mean | spread | bias % |
|---|---|---|---|---|---|
| 1 |  |  |  |  |  |
| 2 |  |  |  |  |  |
| 3 |  |  |  |  |  |

- **Pass:** spread collapses to ≲1% and bias within ~2%.
- **Residual constant bias after jitter is gone** → *now* re-fit `RPM_FREQ_MULT` from these clean numbers (record: __________).
- **Still jittery** → escalate `ICFilter` (12→15) and/or add Lever 2 (software short-period reject); consider an RC/Schmitt on PA6 (Robb).

---

## Safety

- Engine must be **running** for these readings. Start via the panel (OFF→AUTO as usual). Have someone at the controls; keep clear of the belt/pulley while the digital tach is near it. Don't over-rev to chase data points — use the speeds the APU normally runs at.
- Recording is read-only; no device writes needed for data collection.
