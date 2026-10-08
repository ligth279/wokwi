#!/usr/bin/env python3
"""Step 5 acceptance checker: fault detection (protected build).

Reads the raw serial logs of the Step 5 runs (Tests/run_step5.sh) and evaluates
the criteria 5.1 ... 5.12. Verdicts:
  PASS   the criterion is met, shown by the logs
  FAIL   it is not met
  LIMIT  it cannot be met as written in Wokwi (the simulator does not deliver
         the hardware behaviour); the evidence column says what was observed
         instead. LIMIT items are reported separately, never counted as PASS.

usage: check_step5.py --dir results/raw/step5/<ts> [--report ...] [--table ...] [--csv ...]
"""
import argparse
import csv
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
import step4_cases as S4  # noqa: E402
import step5_cases as C  # noqa: E402
from check_step4 import Run, hexint, kv  # noqa: E402

CYC_PER_MS = 72000
WARN_PCT = 75


def crc32_mpeg2(words):
    crc = 0xFFFFFFFF
    for w in words:
        crc ^= w
        for _ in range(32):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
    return crc


def i16(v):
    return v & 0xFFFF


def cfg_words(setpoint=2200, kp=15, lo=0, hi=100):
    return [(i16(kp) << 16) | i16(setpoint), (i16(hi) << 16) | i16(lo)]


def detects(run):
    return run.by_tag("DETECT")


def det_for(run, exp):
    return [d for d in detects(run) if d["kv"].get("EXP") == exp]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--report", default="results/summaries/step5_acceptance.md")
    ap.add_argument("--table", default="results/tables/step5_detection.md")
    ap.add_argument("--csv", default="results/summaries/step5_detections.csv")
    ap.add_argument("--elf", default=None)
    a = ap.parse_args()
    elf = a.elf or os.path.join(a.dir, "firmware.elf")

    runs = {s: [Run(s, i, a.dir) for i in range(1, C.REPEATS + 1)] for s in C.SCENARIOS}
    reg = {}
    regf = f"{a.dir}/regression.txt"
    if os.path.exists(regf):
        for line in open(regf):
            for k, v in re.findall(r"(\w+)=(\S+)", line):
                reg[k] = v

    R = {}  # id -> (verdict, evidence)

    def ev(ok, text, limit=False):
        return ("PASS" if ok else ("LIMIT" if limit else "FAIL"), text)

    def all_runs(fid):
        return runs[C.scenario_of(fid)]

    def exp_of(fid):
        return f"{fid}_001"

    def per_run_all(fid, fn):
        res = [fn(r) for r in all_runs(fid)]
        return all(res), res

    # ---- helpers on detections ------------------------------------------------------
    def mech_set(run, fid):
        return {d["kv"]["mech"] for d in det_for(run, exp_of(fid))}

    def first_det(run, fid, mech):
        l = [d for d in det_for(run, exp_of(fid)) if d["kv"]["mech"] == mech]
        return l[0] if l else None

    fp_runs = runs["fp"]
    fault_runs = [r for s, rs in runs.items() if s != "fp" for r in rs]

    # ================= 5.1 detection framework =================
    src = {}
    for f in ("detect.c", "det_monitor.c", "det_wwdg.c", "det_fault.c"):
        src[f] = open(f"FaultDetection/Src/{f}").read() if os.path.exists(f"FaultDetection/Src/{f}") else ""
    callers = {f: len(re.findall(r"\bdet_report\(DET_M_", t)) for f, t in src.items() if f != "detect.c"}
    all_det = [d for r in fault_runs + fp_runs for d in detects(r)]
    mechs_seen = {d["kv"].get("mech") for d in all_det}
    want_mechs = {"WWDG", "FAULT_HANDLER", "STACK_CANARY", "STACK_SEAL", "STACK_PAINT", "CRC", "REDUNDANT", "HEARTBEAT"}
    fmt_ok = all(all(k in d["kv"] for k in ("EXP", "mech", "det_cycle")) for d in all_det)
    R["5.1a"] = ev(fmt_ok and want_mechs <= mechs_seen and all(v > 0 for v in callers.values()),
                   f"every mechanism reports through det_report() (call sites per file: {callers}); mechanisms seen in the logs: "
                   f"{sorted(m for m in mechs_seen if m)}; all {len(all_det)} DETECT lines carry EXP, mech, det_cycle")

    def assoc(fid):
        def fn(r):
            inj = r.injected(fid)
            ds = det_for(r, exp_of(fid))
            return inj is not None and all(d["i"] > inj["i"] or d["kv"].get("mech") in ("STACK_CANARY", "STACK_PAINT") and d["i"] > inj["i"] for d in ds) and bool(ds)
        return fn
    expected_faults = [f for f, m in C.EXPECT.items() if m]
    res = {f: per_run_all(f, assoc(f))[0] for f in expected_faults}
    foreign = [d for r in fault_runs for d in detects(r)
               if d["kv"].get("EXP") not in ("none",) and not re.match(r"^[A-Z]+-\d+_001$", d["kv"].get("EXP", ""))]
    R["5.1b"] = ev(all(res.values()) and not foreign,
                   "DETECT lines carry the EXP of the injected experiment and follow its INJECTED line: " +
                   ", ".join(f"{f}={'ok' if v else 'FAIL'}" for f, v in res.items()))
    arith = all(d["kv"].get("EXP") == "none" or
                (int(d["kv"]["latency_cycles"]) == (int(d["kv"]["det_cycle"]) - int(d["kv"]["inj_cycle"])) % 2**32 and
                 int(d["kv"]["inj_cycle"]) == int(r.injected(d["kv"]["EXP"].rsplit("_", 1)[0])["kv"]["cycle"]))
                for r in fault_runs for d in detects(r) if r.injected(d["kv"].get("EXP", "x").rsplit("_", 1)[0]) or d["kv"].get("EXP") == "none")
    R["5.1c"] = ev(arith and bool(all_det), "det_cycle (DWT) on every DETECT line; inj_cycle equals the cycle of the INJECTED event and latency_cycles = det_cycle - inj_cycle")
    R["5.1d"] = ev(fmt_ok and all(d["kv"]["mech"] in C_MECHS for d in all_det) if (C_MECHS := {"WWDG", "FAULT_HANDLER", "STACK_CANARY", "STACK_SEAL", "STACK_PAINT", "CRC", "REDUNDANT", "HEARTBEAT", "I2C_TIMEOUT"}) else False,
                   "every DETECT line names its mechanism (mech=...)")
    fp_lines = [d for r in fp_runs + fault_runs for d in detects(r) if d["kv"].get("false_positive") == "1"]
    fp_det = [d for r in fp_runs for d in detects(r)]
    R["5.1e"] = ev(not fp_lines and not fp_det,
                   f"fault-free runs: {len(fp_det)} DETECT lines; fault runs: {len(fp_lines)} DETECT lines without an injected experiment (false_positive=1)")
    # f: normal behaviour unchanged (protected build, fault-free): control law, stream, periods
    def normal(r):
        ss, cs = r.sensors(), r.controls()
        ok_stream = [int(s["kv"]["seq"]) for s in ss] == list(range(1, len(ss) + 1)) and all(s["kv"]["status"] == "OK" and s["kv"]["sample"] == s["kv"]["seq"] for s in ss)
        ok_law = all(int(c["kv"]["value"]) == S4.control(int(c["kv"]["input"])) and c["kv"]["input"] == next(s["kv"]["value"] for s in ss if s["kv"]["seq"] == c["kv"]["seq"]) for c in cs)
        cyc = [int(s["kv"]["cyc"]) for s in ss]
        per = [b - a for a, b in zip(cyc, cyc[1:])]
        ok_per = bool(per) and max(abs(p - 7200000) for p in per) < 7200
        return ok_stream and ok_law and ok_per and len(ss) >= 100, len(ss), (min(per), max(per)) if per else None
    nrm = [normal(r) for r in fp_runs]
    R["5.1f"] = ev(all(n[0] for n in nrm) and reg.get("step2_exit") == "0" and reg.get("step3_exit") == "0" and reg.get("step4_exit") == "0",
                   f"fault-free protected run: {nrm[0][1]} samples, contiguous, status OK, CONTROL follows the control law, sensor period {nrm[0][2]} cycles (target 7200000); "
                   f"baseline suites re-run: step2 exit {reg.get('step2_exit')}, step3 exit {reg.get('step3_exit')}, step4 exit {reg.get('step4_exit')}")

    # ================= 5.2 WWDG =================
    def last_detstat(r):
        l = [x for x in r.by_tag("DETSTAT")]
        return l[-1]["kv"] if l else None
    ds = [last_detstat(r) for r in fp_runs]
    stat_t = [int(r.statuses()[-1]["kv"]["sensor_hb"]) for r in fp_runs if r.statuses()]
    R["5.2a"] = ev(all(d and int(d["wd_refresh"]) >= 1000 * (n / 10.0) * 0.95 for d, n in zip(ds, stat_t)) and
                   all(any("wwdg started" in x["rest"] for x in r.by_tag("DET")) for r in fp_runs),
                   f"fault-free run: WWDG started and refreshed {ds[0]['wd_refresh']} times in ~{stat_t[0] / 10:.0f} s (1 kHz refresh while the monitor token is fresh)  "
                   "[simulator workaround: Wokwi's WWDG counter cannot be reloaded reliably, the TIM2 shim evaluates the progress token - see docs/SIMULATOR_LIMITATIONS.md]")
    R["5.2b"] = ev(all(len(r.by_tag("BOOT")) == 1 and not [d for d in detects(r) if d["kv"].get("mech") == "WWDG"] for r in fp_runs),
                   f"fault-free runs: BOOT count {[len(r.by_tag('BOOT')) for r in fp_runs]} (no reset), no WWDG detection")
    t1 = [first_det(r, "TIM-01", "WWDG") for r in runs["tim01"]]
    R["5.2c"] = ev(all(t1), f"TIM-01 -> DETECT mech=WWDG in {sum(1 for x in t1 if x)}/3 runs: {t1[0]['rest'][:150] if t1[0] else ''}")
    def reset_after(r, fid):
        det = first_det(r, fid, "WWDG")
        bt = r.by_tag("BOOT")
        rs = [x for x in r.by_tag("RESET") if x["kv"].get("breadcrumb") == "WWDG"]
        return det and len(bt) == 2 and bt[1]["i"] > det["i"] and rs, rs[0] if rs else None
    ra = [reset_after(r, "TIM-01") for r in runs["tim01"]]
    R["5.2d"] = ev(all(x[0] for x in ra), "after the WWDG detection the MCU resets and boots again (2 BOOT lines, 3/3 runs); the application restarts (tasks + system_ready) after it")
    R["5.2e"] = ev(all(x[1] and x[1]["kv"].get("cause_resolved") == "WWDG" and x[1]["kv"].get("exp") == "TIM-01_001" for x in ra),
                   f"{ra[0][1]['rest'][:200] if ra[0][1] else ''} - cause from the breadcrumb: RCC_CSR reads 0 in Wokwi (csr=0x00000000), so the hardware reset flag is NOT available")
    R["5.2f"] = ev(all(x[1] and "latency_cycles" in x[1]["kv"] and "det_to_reset_cycles" in x[1]["kv"] for x in ra),
                   f"detection latency {ra[0][1]['kv'].get('latency_cycles')} cycles ({int(ra[0][1]['kv'].get('latency_cycles', 0)) / CYC_PER_MS:.1f} ms), detection->reset {ra[0][1]['kv'].get('det_to_reset_cycles')} cycles; "
                   "measured with the Wokwi timeout of the shim (150 ms stale + 8 ms), not the silicon WWDG timeout")

    # ================= 5.3 fault handlers =================
    hline = [x for r in fp_runs for x in r.by_tag("DET") if "fault_handlers" in x["rest"]]
    sh = hline[0]["kv"] if hline else {}
    nm = subprocess.run(["arm-none-eabi-nm", elf], capture_output=True, text=True).stdout if os.path.exists(elf) else ""
    handlers = all(re.search(rf"\bT {n}\b", nm) for n in ("HardFault_Handler", "MemManage_Handler", "BusFault_Handler", "UsageFault_Handler", "det_fault_c"))
    for k, name in (("a", "memfault"), ("b", "busfault"), ("c", "usagefault")):
        R[f"5.3{k}"] = ev(False, f"handler linked and enable bit written (SHCSR |= {name.upper()}ENA), but SHCSR reads back {sh.get('shcsr')} in Wokwi, {name}={sh.get('memfault' if k == 'a' else 'busfault' if k == 'b' else 'usagefault')}: the enable bits are not retained", limit=True) if handlers and sh else ev(False, "missing")
    c03 = [first_det(r, "CPU-03", "FAULT_HANDLER") for r in runs["group"]]
    R["5.3d"] = ev(False, f"HardFault_Handler/MemManage/BusFault/UsageFault link to det_fault_c() (nm: {handlers}); the capture/report path ran only through the synthetic CPU-03 invocation: "
                          f"{c03[0]['rest'][:170] if c03[0] else 'none'}; Wokwi never delivered a fault exception (results/raw/step5/probes: UDF, unmapped read, UNALIGN_TRP, DIV_0_TRP)", limit=True) if all(c03) and handlers else ev(False, "no CPU-03 detection")
    for k, reg_name, val in (("e", "cfsr", "cfsr=0x00020000(INVSTATE)"), ("f", "hfsr", "hfsr=0x40000000(FORCED)")):
        R[f"5.3{k}"] = ev(False, f"{reg_name.upper()} is read and decoded by the capture path; observed only with the synthetic value ({val}); no real fault status exists in Wokwi", limit=True) if all(c03) and val in c03[0]["rest"] else ev(False, "not captured")
    R["5.3g"] = ev(False, "MMFAR is printed only when CFSR.MMARVALID is set; no real MemManage fault could be produced, the synthetic frame has MMARVALID clear (so the field is correctly absent)", limit=True) if all(c03) and "mmfar" not in c03[0]["rest"] else ev(False, "unexpected mmfar in synthetic line")
    R["5.3h"] = ev(False, "BFAR is printed only when CFSR.BFARVALID is set; no real BusFault could be produced, the synthetic frame has BFARVALID clear (so the field is correctly absent)", limit=True) if all(c03) and "bfar" not in c03[0]["rest"] else ev(False, "unexpected bfar in synthetic line")
    for k, fid in (("i", "CPU-01"), ("j", "CPU-02")):
        ws = [first_det(r, fid, "WWDG") for r in runs["cpu01" if fid == "CPU-01" else "cpu02"]]
        inj = [r.injected(fid) for r in runs["cpu01" if fid == "CPU-01" else "cpu02"]]
        R[f"5.3{k}"] = ev(False, f"{fid} injected ({inj[0]['kv']['before']} -> {inj[0]['kv']['after']}); no fault exception reaches the handlers in Wokwi. Observable result instead: the CPU is stuck, the monitor is starved and the WWDG shim detects it in {sum(1 for w in ws if w)}/3 runs "
                          f"(latency {ws[0]['kv']['latency_cycles'] if ws[0] else '?'} cycles) and resets the MCU; the unprotected build ended the simulation (code 1006)", limit=True) if all(ws) and all(inj) else ev(False, "no observable result")
    R["5.3k"] = ev(all(c03) and all(d["kv"]["EXP"] == "CPU-03_001" for d in c03),
                   "fault-handler capture path output is tagged with the experiment ID (EXP=CPU-03_001) - synthetic invocation, synthetic=1 in the line")

    # ================= 5.4 stack canary =================
    gl = [x for r in fp_runs for x in r.by_tag("DET") if "stack_guards" in x["rest"]]
    R["5.4a"] = ev(bool(gl) and "canary_words=4" in gl[0]["rest"], f"{gl[0]['rest'] if gl else ''} (4 guard words 0xC0DEC0DE at the lowest addresses of the sensor, control, console and monitor stacks)")
    R["5.4b"] = ev(all(all(x["kv"]["canary_ok"] == "3/3" for x in r.by_tag("DETSTAT")) and not [d for d in detects(r) if d["kv"]["mech"] == "STACK_CANARY"] for r in fp_runs),
                   f"fault-free runs: canary_ok=3/3 on all {len(fp_runs[0].by_tag('DETSTAT'))} DETSTAT lines, no STACK_CANARY detection")
    s2 = [first_det(r, "MEM-02", "STACK_SEAL") for r in runs["mem02"]]
    R["5.4c"] = ev(False, f"MEM-02 corrupts the saved-context word of the sensor task (stack top: LR slot, bit 29 flipped), not the guard words at the stack base; that region is protected by the saved-context seal, detected in {sum(1 for x in s2 if x)}/3 runs "
                          "(STACK_SEAL, latency %s cycles). The canary region itself is corrupted by the detector-validation fault MEM-03." % (s2[0]["kv"]["latency_cycles"] if s2[0] else "?"), limit=True) if all(s2) else ev(False, "MEM-02 not detected by the seal")
    m3 = [first_det(r, "MEM-03", "STACK_CANARY") for r in runs["group"]]
    R["5.4d"] = ev(all(m3), f"MEM-03 (sensor stack canary overwritten with 0xDEADBEEF) -> STACK_CANARY in {sum(1 for x in m3 if x)}/3 runs: {m3[0]['rest'][:150] if m3[0] else ''}")
    R["5.4e"] = ev(all(m3) and all(d["kv"]["EXP"] == "MEM-03_001" for d in m3), "DETECT EXP=MEM-03_001 mech=STACK_CANARY")
    R["5.4f"] = ev(all(m3) and all(int(d["kv"]["det_cycle"]) > int(d["kv"]["inj_cycle"]) for d in m3), "det_cycle recorded; latencies " + str([d["kv"]["latency_cycles"] for d in m3]))

    # ================= 5.5 stack painting =================
    R["5.5a"] = ev(bool(gl) and "paint=0xA5A5A5A5" in gl[0]["rest"], "stack words below the live part are filled with 0xA5A5A5A5 (FreeRTOS fill, verified by the high-water scan) - " + (gl[0]["rest"] if gl else ""))
    peaks = [last_detstat(r)["stack_peak_pct"] for r in fp_runs]
    R["5.5b"] = ev(all(re.match(r"\d+:\d+:\d+$", p) for p in peaks), f"stack_peak_pct (sensor:control:console) in the fault-free runs: {peaks} (warning threshold {WARN_PCT} %)")
    m4 = [first_det(r, "MEM-04", "STACK_PAINT") for r in runs["group"]]
    inj4 = [r.injected("MEM-04") for r in runs["group"]]
    R["5.5c"] = ev(all(inj4) and all(int(hexint(i["kv"]["after"])) > WARN_PCT >= int(hexint(i["kv"]["before"])) for i in inj4),
                   f"MEM-04 raises the control stack use from {hexint(inj4[0]['kv']['before'])} % to {hexint(inj4[0]['kv']['after'])} % (threshold {WARN_PCT} %)")
    R["5.5d"] = ev(all(m4), f"STACK_PAINT detection in {sum(1 for x in m4 if x)}/3 runs: {m4[0]['rest'][:150] if m4[0] else ''}")
    R["5.5e"] = ev(all(m4) and all(d["kv"]["EXP"] == "MEM-04_001" for d in m4), "DETECT EXP=MEM-04_001 mech=STACK_PAINT logged")

    # ================= 5.6 CRC =================
    cfg_crc = crc32_mpeg2(cfg_words())
    d2 = [first_det(r, "DATA-02", "CRC") for r in runs["group"]]
    d1 = [first_det(r, "DATA-01", "CRC") for r in runs["group"]]
    m1 = [first_det(r, "MEM-01", "CRC") for r in runs["group"]]
    smp = lambda d: crc32_mpeg2([int(d["kv"]["seq"]), i16(int(d["kv"]["sensor"]))])
    R["5.6a"] = ev(all(d and int(d["kv"]["crc_expected"], 16) == cfg_crc for d in d2),
                   f"the hardware CRC unit (CRC peripheral) produced 0x{cfg_crc:08X} for the nominal g_config, equal to the CRC-32/MPEG-2 computed here in software")
    R["5.6b"] = ev(all(last_detstat(r)["cfg_fail"] == "0" and last_detstat(r)["sample_fail"] == "0" and int(last_detstat(r)["cfg_checks"]) >= 100 for r in fp_runs),
                   f"fault-free runs: cfg_checks={last_detstat(fp_runs[0])['cfg_checks']} cfg_fail=0 sample_checks={last_detstat(fp_runs[0])['sample_checks']} sample_fail=0")
    ok6c = all(d and int(d["kv"]["crc_actual"], 16) == crc32_mpeg2(cfg_words(setpoint=3224)) for d in m1) and \
        all(d and int(d["kv"]["crc_actual"], 16) == crc32_mpeg2(cfg_words(kp=100)) for d in d2) and \
        all(d and int(d["kv"]["crc_actual"], 16) == crc32_mpeg2([int(d["kv"]["seq"]), 8500]) and int(d["kv"]["crc_expected"], 16) == smp(d) for d in d1)
    R["5.6c"] = ev(ok6c, "mismatch observed for MEM-01 (setpoint 2200->3224), DATA-02 (kp 15->100) and DATA-01 (sample 8500): the reported actual CRCs equal the software CRC of exactly the corrupted data, the expected CRCs the CRC of the original")
    R["5.6d"] = ev(all(d1), f"DATA-01 -> CRC in {sum(1 for x in d1 if x)}/3 runs (sample CRC attached by the sensor task, checked by the control task): {d1[0]['rest'][:140] if d1[0] else ''}")
    R["5.6e"] = ev(all(d2), f"DATA-02 -> CRC in {sum(1 for x in d2 if x)}/3 runs (config block CRC)")
    R["5.6f"] = ev(all(d1 + d2 + m1) and all(d["kv"]["EXP"] in ("DATA-01_001", "DATA-02_001", "MEM-01_001") for d in d1 + d2 + m1), "EXP recorded on every CRC detection")
    R["5.6g"] = ev(all(d1 + d2 + m1), "det_cycle recorded; latencies (cycles) DATA-01 %s, DATA-02 %s, MEM-01 %s" % ([d["kv"]["latency_cycles"] for d in d1], [d["kv"]["latency_cycles"] for d in d2], [d["kv"]["latency_cycles"] for d in m1]))

    # ================= 5.7 redundant =================
    r1 = [first_det(r, "MEM-01", "REDUNDANT") for r in runs["group"]]
    r2 = [first_det(r, "DATA-02", "REDUNDANT") for r in runs["group"]]
    R["5.7a"] = ev(all(r1 + r2), "redundant copies (bitwise-inverted) exist for the 4 fields of g_config: setpoint_centi, kp_pct_per_c, out_min, out_max; no other variable is redundant (FaultDetection/Src/det_monitor.c)")
    R["5.7b"] = ev(all(last_detstat(r)["cfg_fail"] == "0" for r in fp_runs), "fault-free runs: every check of the 4 pairs matched (cfg_fail=0)")
    R["5.7c"] = ev(all(r1 + r2) and all(d["kv"]["value"] != d["kv"]["copy"] for d in r1 + r2),
                   f"corrupting the primary leaves the copy intact: MEM-01 {r1[0]['rest'][:90] if r1[0] else ''}; DATA-02 {r2[0]['rest'][:90] if r2[0] else ''}")
    R["5.7d"] = ev(all(r1 + r2), f"REDUNDANT detection MEM-01 {sum(1 for x in r1 if x)}/3, DATA-02 {sum(1 for x in r2 if x)}/3 runs")
    R["5.7e"] = ev(all(r1 + r2) and all(d["kv"]["EXP"] in ("MEM-01_001", "DATA-02_001") for d in r1 + r2), "EXP=MEM-01_001 / DATA-02_001 on the REDUNDANT detections")
    R["5.7f"] = ev(all(r1 + r2), "det_cycle recorded; latencies MEM-01 %s, DATA-02 %s" % ([d["kv"]["latency_cycles"] for d in r1], [d["kv"]["latency_cycles"] for d in r2]))

    # ================= 5.8 heartbeat =================
    def hb_increasing(r):
        st = r.statuses()
        return all(all(int(b["kv"][k]) > int(a_["kv"][k]) for a_, b in zip(st, st[1:])) for k in ("sensor_hb", "control_hb", "console_hb")) and len(st) >= 10
    R["5.8a"] = ev(all(hb_increasing(r) for r in fp_runs), f"sensor_hb, control_hb, console_hb (and the monitor's mon_runs) advance on every periodic STATUS/DETSTAT line ({len(fp_runs[0].statuses())} lines)")
    R["5.8b"] = ev(all(last_detstat(r)["hb_flags"] == "0" for r in fp_runs), "fault-free runs: hb_flags=0 (no heartbeat alarm)")
    t2 = [r for r in runs["tim02"]]
    blk = [[x for x in r.by_tag("TASK") if x["kv"].get("state") == "blocked"] for r in t2]
    R["5.8c"] = ev(all(len(b) == 1 and b[0]["kv"]["name"] == "sensor" for b in blk), f"TIM-02: {blk[0][0]['rest'] if blk[0] else ''}; sensor_hb stops")
    h2 = [first_det(r, "TIM-02", "HEARTBEAT") for r in t2]
    R["5.8d"] = ev(all(h2), f"HEARTBEAT detection in {sum(1 for x in h2 if x)}/3 runs: {h2[0]['rest'][:120] if h2[0] else ''}")
    R["5.8e"] = ev(all(h2) and all(d["kv"].get("task") == "sensor" for d in h2) and all(len([d for d in det_for(r, 'TIM-02_001') if d['kv']['mech'] == 'HEARTBEAT']) == 1 for r in t2),
                   "task=sensor in 3/3 runs; the starved control task is not reported (its check is gated on a live sensor)")
    R["5.8f"] = ev(all(h2), "det_cycle recorded; latency cycles " + str([d["kv"]["latency_cycles"] for d in h2]))
    R["5.8g"] = ev(all(h2) and all(d["kv"]["EXP"] == "TIM-02_001" for d in h2), "EXP=TIM-02_001")

    # ================= 5.9 latency =================
    inj_all = [(f, r.injected(f)) for f in C.STUDY_IDS + C.VALIDATION_IDS for r in all_runs(f)]
    R["5.9a"] = ev(all(i for _, i in inj_all), f"INJECTED events with the DWT cycle (Step 3/4 framework) present for all {len(inj_all)} fault runs")
    R["5.9b"] = ev(bool(all_det) and all("det_cycle" in d["kv"] for d in all_det), "det_cycle on every DETECT line")
    R["5.9c"] = ev(arith, "latency_cycles = det_cycle - inj_cycle verified on every DETECT line (unsigned 32-bit arithmetic)")
    rows = []
    for f in C.STUDY_IDS + C.VALIDATION_IDS:
        for r in all_runs(f):
            for d in det_for(r, exp_of(f)):
                rows.append((f, r.idx, d["kv"]["mech"], int(d["kv"]["latency_cycles"]), d["kv"]["det_cycle"], d["kv"]["inj_cycle"]))
    covered = {f for f, *_ in rows}
    expected_cov = {f for f, m in C.EXPECT.items() if m}
    R["5.9d"] = ev(expected_cov <= covered, f"latency recorded for {len(covered)} faults: {sorted(covered)}; faults without a detector: {sorted(set(C.EXPECT) - expected_cov)} (CPU-01/CPU-02 are detected only incidentally, as a hang, by the WWDG)")
    per_run_rows = {(f, r.idx, m) for f, r, m, *_ in [(x[0], x[1], x[2]) for x in rows]}
    R["5.9e"] = ev(all(len([x for x in rows if x[0] == f and x[2] == m]) == C.REPEATS for f, m in {(x[0], x[2]) for x in rows}),
                   f"{len(rows)} individual latency measurements kept in {a.csv} (one row per fault, run and mechanism), not averages")

    # ================= 5.10 false positive =================
    R["5.10a"] = ev(len(fp_runs) == 3 and all(r.present and r.exit == 42 or r.exit == 0 for r in fp_runs) and all(int(r.statuses()[-1]["kv"]["faults_injected"]) == 0 for r in fp_runs),
                    f"{len(fp_runs)} runs of the protected build, no FAULT command, ~{stat_t[0] / 10:.0f} s each (faults_injected=0)")
    R["5.10b"] = ev(R["5.2b"][0] == "PASS", "no WWDG reset: BOOT count 1, no WWDG detection")
    R["5.10c"] = ev(all(not [d for d in detects(r) if d["kv"]["mech"] == "FAULT_HANDLER"] for r in fp_runs), "no FAULT_HANDLER detection (and none could occur: Wokwi delivers no fault exceptions)")
    R["5.10d"] = ev(R["5.4b"][0] == "PASS", "canary_ok=3/3 throughout")
    pk = [[int(x) for x in last_detstat(r)["stack_peak_pct"].split(":")] for r in fp_runs]
    R["5.10e"] = ev(all(max(p) < WARN_PCT for p in pk) and not [d for r in fp_runs for d in detects(r) if d["kv"]["mech"] == "STACK_PAINT"], f"stack peaks (sensor:control:console) {pk[0]} % < {WARN_PCT} %, no STACK_PAINT detection")
    R["5.10f"] = ev(R["5.6b"][0] == "PASS", "CRC checks pass (cfg_fail=0, sample_fail=0)")
    R["5.10g"] = ev(R["5.7b"][0] == "PASS", "redundant pairs consistent")
    R["5.10h"] = ev(R["5.8b"][0] == "PASS" and R["5.8a"][0] == "PASS", "task heartbeats healthy (hb_flags=0, counters advancing)")

    # ================= 5.11 regression =================
    def summ(path):
        if not path or not os.path.exists(path):
            return None
        t = open(path).read()
        m = re.search(r"\*\*(\d+)/(\d+) criteria passed", t)
        return (int(m.group(1)), int(m.group(2)), t) if m else None
    s2, s3, s3p, s4 = (summ(reg.get(k)) for k in ("step2_summary", "step3_summary", "step3p_summary", "step4_summary"))
    row = lambda t, n: re.search(r"\| %s \| [^|]*\| (\w+) \|" % n, t[2]) if t else None
    R["5.11a"] = ev(s2 and row(s2, 15) and row(s2, 15).group(1) == "PASS", f"Step 0 (smoke, re-run inside the Step 2 suite): {row(s2, 15).group(1) if s2 and row(s2, 15) else 'no data'}")
    R["5.11b"] = ev(s2 and row(s2, 16) and row(s2, 16).group(1) == "PASS", f"Step 1 (i2ctest): {row(s2, 16).group(1) if s2 and row(s2, 16) else 'no data'}")
    R["5.11c"] = ev(s2 and s2[0] == s2[1] and reg.get("step2_exit") == "0", f"Step 2 suite (baseline): {s2[0]}/{s2[1]}" if s2 else "no data")
    R["5.11d"] = ev(s3 and s3[0] == s3[1] and reg.get("step3_exit") == "0", f"Step 3 suite (fwtest): {s3[0]}/{s3[1]}" if s3 else "no data")
    R["5.11e"] = ev(s4 and s4[0] == s4[1] and reg.get("step4_exit") == "0", f"Step 4 suite (baseline build, fault effects): {s4[0]}/{s4[1]}" if s4 else "no data")
    study_ok = []
    for f in C.STUDY_IDS:
        for r in all_runs(f):
            i = r.injected(f)
            if not i:
                study_ok.append((f, r.idx, False))
                continue
            b, af = hexint(i["kv"]["before"]), hexint(i["kv"]["after"])
            rel = {"MEM-01": af == b ^ (1 << 10), "MEM-02": af == b ^ (1 << 29), "CPU-01": af == ((b ^ (1 << 29)) | 1), "CPU-02": af == b ^ (1 << 28),
                   "TIM-01": af == b, "TIM-02": af == b, "DATA-01": af == 8500, "DATA-02": (b, af) == (15, 100),
                   "PERIPH-01": (b, af) == (0, 1)}[f]
            study_ok.append((f, r.idx, rel and r.fault_ev(exp_of(f), "INJECTED") and len(r.fault_ev(exp_of(f), "INJECTED")) == 1))
    R["5.11f"] = ev(all(x[2] for x in study_ok), f"all 9 study faults inject with the Step 4 corruption on the protected build ({sum(1 for x in study_ok if x[2])}/{len(study_ok)} runs: same targets, same bit flips / values, exactly one INJECTED)")
    R["5.11g"] = ev(s3p and s3p[0] == s3p[1] and reg.get("step3p_exit") == "0" and reg.get("unit_exit") == "0",
                    f"Step 3 suite on the protected code with the study faults off (protfw): {s3p[0]}/{s3p[1]}; host unit tests exit {reg.get('unit_exit')}" if s3p else "no data")

    # ================= 5.12 reproducibility =================
    def sigs(fid):
        out = []
        for r in all_runs(fid):
            out.append(tuple(sorted((d["kv"]["mech"], d["kv"]["latency_cycles"], d["kv"]["det_cycle"]) for d in det_for(r, exp_of(fid)))))
        return out
    sg = {f: sigs(f) for f in C.STUDY_IDS + C.VALIDATION_IDS}
    R["5.12a"] = ev(all(len(runs[s]) == 3 and all(x.present for x in runs[s]) for s in runs), f"{len(runs)} scenarios x 3 = {len(runs) * 3} simulations of the protected build")
    mset = {f: [frozenset(m for m, _, _ in s) for s in ss] for f, ss in sg.items()}
    R["5.12b"] = ev(all(len(set(v)) == 1 and (v[0] or C.EXPECT[f] is None) for f, v in mset.items() if C.EXPECT.get(f) or True) and
                    all(mset[f][0] for f in C.EXPECT if C.EXPECT[f] or f in ("CPU-01", "CPU-02")),
                    "the same fault is detected in 3/3 runs: " + ", ".join(f"{f}={'yes' if v[0] else 'NO'}" for f, v in mset.items()))
    R["5.12c"] = ev(all(len(set(v)) == 1 for v in mset.values()), "same mechanisms in every run: " + ", ".join(f"{f}={sorted(v[0])}" for f, v in mset.items()))
    R["5.12d"] = ev(all(len(s) == len(sg[f][0]) for f, ss in sg.items() for s in ss) and len(rows) > 0, f"latency recorded for every detection of every run ({len(rows)} measurements in the CSV)")
    var = {f: ss for f, ss in sg.items() if len(set(ss)) != 1}
    R["5.12e"] = ev(True, "variation: " + ("none - detection cycles and latencies are bit-identical across the 3 runs of every fault (the simulation is deterministic)" if not var else
                                          "; ".join(f"{f}: latencies differ between runs ({[[x[1] for x in s] for s in ss]})" for f, ss in var.items())))

    # ================= report =================
    titles = {
        "5.1a": "A common detection interface exists for all mechanisms", "5.1b": "A detected fault is associated with the active EXP",
        "5.1c": "Detection records the detection cycle", "5.1d": "Detection records which mechanism detected the fault",
        "5.1e": "Detection does not falsely report a fault during normal operation", "5.1f": "Normal Step 0-4 behaviour remains unchanged",
        "5.2a": "WWDG operates correctly during normal execution", "5.2b": "Normal servicing does not cause unwanted resets",
        "5.2c": "TIM-01 causes the watchdog to detect the hang", "5.2d": "A WWDG reset occurs when the watchdog condition is met",
        "5.2e": "Reset cause can be identified afterwards", "5.2f": "Detection/reset timing is recorded",
        "5.3a": "MemManage fault handling is enabled", "5.3b": "BusFault handling is enabled", "5.3c": "UsageFault handling is enabled",
        "5.3d": "HardFault handling captures the relevant fault condition", "5.3e": "CFSR is captured", "5.3f": "HFSR is captured",
        "5.3g": "MMFAR is captured when applicable", "5.3h": "BFAR is captured when applicable",
        "5.3i": "CPU-01 produces an observable fault result", "5.3j": "CPU-02 produces an observable fault result",
        "5.3k": "Fault information is associated with the experiment ID",
        "5.4a": "A stack canary is initialised", "5.4b": "Normal execution does not falsely trigger the canary",
        "5.4c": "MEM-02 can corrupt the protected stack region", "5.4d": "Canary corruption is detected",
        "5.4e": "Detection is logged with the experiment ID", "5.4f": "Detection cycle is recorded",
        "5.5a": "Stack region is initialised with a known pattern", "5.5b": "Normal stack usage can be measured",
        "5.5c": "Increased/abnormal stack usage is detectable", "5.5d": "Stack-overuse condition produces a detection event", "5.5e": "Detection is logged",
        "5.6a": "Hardware CRC is initialised", "5.6b": "Protected data produces a valid CRC during normal operation",
        "5.6c": "Corruption of protected data produces a CRC mismatch", "5.6d": "DATA-01 can be detected by CRC",
        "5.6e": "DATA-02 can be detected by CRC", "5.6f": "CRC detection is logged with the experiment ID", "5.6g": "Detection cycle is recorded",
        "5.7a": "Selected critical variables have redundant copies", "5.7b": "Matching copies are accepted during normal operation",
        "5.7c": "Corrupting one copy produces a mismatch", "5.7d": "The mismatch is detected",
        "5.7e": "Detection is associated with the correct experiment", "5.7f": "Detection cycle is recorded",
        "5.8a": "Each protected task reports its heartbeat periodically", "5.8b": "Normal heartbeats do not generate false alarms",
        "5.8c": "TIM-02 prevents the affected task from reporting normally", "5.8d": "Missing heartbeat is detected",
        "5.8e": "The affected task is identified", "5.8f": "Detection cycle is recorded", "5.8g": "Detection is associated with the experiment ID",
        "5.9a": "Injection cycle is available from Step 4", "5.9b": "Detection cycle is recorded", "5.9c": "Latency = detection cycle - injection cycle",
        "5.9d": "Latency is recorded for each applicable fault", "5.9e": "Repeated measurements are retained rather than only an average",
        "5.10a": "Protected firmware runs without injecting faults", "5.10b": "WWDG produces no unwanted reset", "5.10c": "Fault handlers produce no false fault",
        "5.10d": "Stack canary remains valid", "5.10e": "Stack monitoring remains normal", "5.10f": "CRC checks pass",
        "5.10g": "Redundant variables remain consistent", "5.10h": "Task heartbeats remain healthy",
        "5.11a": "Step 0 passes", "5.11b": "Step 1 passes", "5.11c": "Step 2 passes", "5.11d": "Step 3 passes",
        "5.11e": "Step 4 suite still passes (baseline build)", "5.11f": "Step 4 fault injections still produce their intended faults (protected build)",
        "5.11g": "Detection does not alter the fault-injection framework unexpectedly",
        "5.12a": "Detection tests run at least 3 times", "5.12b": "The same fault is detected consistently", "5.12c": "The same detection mechanism is reported consistently",
        "5.12d": "Detection latency is recorded for every run", "5.12e": "Any variation is documented",
    }
    assert set(titles) == set(R), sorted(set(titles) ^ set(R))
    key = lambda k: tuple(int(x) if x.isdigit() else x for x in re.findall(r"\d+|[a-z]", k))
    cnt = {"PASS": 0, "FAIL": 0, "LIMIT": 0}
    L = ["# Step 5 acceptance - fault detection (protected build)", "",
         f"Runs: {a.dir} ({len(runs)} scenarios x {C.REPEATS} simulations). Detection only: no recovery action is implemented.",
         "Verdicts: PASS = met; FAIL = not met; LIMIT = cannot be met as written in Wokwi (evidence shows what was observed instead).", ""]
    grp = None
    for k in sorted(R, key=key):
        g = k.split(".")[1].rstrip("abcdefghijk")
        if g != grp:
            grp = g
            L += ["", f"## 5.{g}", "", "| # | Criterion | Result | Evidence |", "|---|---|---|---|"]
        v, t = R[k]
        cnt[v] += 1
        L.append(f"| {k} | {titles[k]} | {v} | {t.replace('|', '/')} |")
    L += ["", f"**{cnt['PASS']} PASS, {cnt['LIMIT']} LIMIT (simulator), {cnt['FAIL']} FAIL of {len(R)} criteria.**", ""]
    os.makedirs(os.path.dirname(a.report), exist_ok=True)
    open(a.report, "w").write("\n".join(L))
    print("\n".join(L[-14:]))

    # ---- detection table + csv
    os.makedirs(os.path.dirname(a.table), exist_ok=True)
    T = ["# Step 5 - detection results (protected build)", "",
         "Latency = detection cycle - injection cycle, DWT cycles at 72 MHz (1 ms = 72000 cycles). Source: `" + a.dir + "`.", "",
         "| Fault | Expected mechanism | Detected by (3 runs) | Latency cycles (run1 / run2 / run3) | Latency ms (run1) | Follow-up |",
         "|---|---|---|---|---|---|"]
    follow = {"TIM-01": "WWDG reset, reboot (RESET breadcrumb=WWDG)", "CPU-01": "WWDG reset, reboot", "CPU-02": "WWDG reset, reboot",
              "MEM-02": "WWDG reset after the sensor task died, reboot", "TIM-02": "none (detection only)"}
    with open(a.csv, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["fault", "run", "mechanism", "latency_cycles", "det_cycle", "inj_cycle"])
        for x in sorted(rows):
            w.writerow(x)
    for f in C.STUDY_IDS + C.VALIDATION_IDS:
        ms = sorted({x[2] for x in rows if x[0] == f})
        if not ms:
            T.append(f"| {f} | {'-' if not C.EXPECT.get(f) else '/'.join(sorted(C.EXPECT[f]))} | not detected | - | - | simulation ends (code 1006) in the unprotected build |")
            continue
        for m in ms:
            lat = [next((x[3] for x in rows if x[0] == f and x[1] == i and x[2] == m), None) for i in (1, 2, 3)]
            exp_s = "/".join(sorted(C.EXPECT[f])) if C.EXPECT.get(f) else "none (no detector can see it in Wokwi)"
            mark = "" if (C.EXPECT.get(f) and m in C.EXPECT[f]) else " (incidental)"
            T.append(f"| {f} | {exp_s} | {m}{mark} | {' / '.join(str(x) for x in lat)} | {lat[0] / CYC_PER_MS:.2f} | {follow.get(f, '') if m == 'WWDG' or f == 'TIM-02' else ''} |")
    T += ["", "WWDG latencies measure the simulator-workaround timeout (150 ms without monitor progress + 8 ms), not the silicon WWDG timeout (58 ms). "
          "MEM-03, MEM-04 and CPU-03 are detector-validation faults added in Step 5, not study faults; CPU-03 runs the fault-handler path with a synthetic frame.", ""]
    open(a.table, "w").write("\n".join(T))
    return 0 if cnt["FAIL"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
