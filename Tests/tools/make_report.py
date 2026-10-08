#!/usr/bin/env python3
"""Automatic report: figures + tables for everything that has been run.

Finds the newest complete raw directory of each campaign (Step 4 baseline, Step 5 detection, Step 6 recovery),
re-runs the evaluation scripts on them (tables, CSVs), and draws the figures from the raw logs and the CSVs.
Nothing is typed in: re-run it after any campaign and everything is regenerated.

  python3 Tests/tools/make_report.py [--out results/report] [--no-eval]

Output (results/report/):
  report.html / report.md   the whole report (figures + tables)
  figures/*.svg, *.png      light theme;  *_dark.svg  dark theme (the HTML picks by prefers-color-scheme)
  data/*.csv                the numbers behind every figure
"""
import argparse
import csv
import glob
import html
import os
import re
import subprocess
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

sys.path.insert(0, os.path.dirname(__file__))
import eval_step7 as E  # noqa: E402
import step4_cases as S4  # noqa: E402
from check_step4 import Run  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
CYC_MS = 72000

# validated reference palette (dataviz skill, references/palette.md); status colours for PASS/LIMIT/FAIL
THEMES = {
    "light": dict(surface="#fcfcfb", ink="#0b0b0b", ink2="#52514e", muted="#8a8984", grid="#e6e5e1", s1="#2a78d6", s2="#eb6834", s3="#1baf7a",
                  good="#0ca30c", warn="#fab219", crit="#d03b3b"),
    "dark": dict(surface="#1a1a19", ink="#ffffff", ink2="#c3c2b7", muted="#8f8e86", grid="#34332f", s1="#3987e5", s2="#d95926", s3="#199e70",
                 good="#0ca30c", warn="#fab219", crit="#d03b3b"),
}
STUDY = S4.STUDY_IDS


def theme_setup(t):
    c = THEMES[t]
    plt.rcParams.update({
        "svg.fonttype": "none", "font.family": "DejaVu Sans", "font.size": 9.5,
        "figure.facecolor": c["surface"], "axes.facecolor": c["surface"], "savefig.facecolor": c["surface"],
        "axes.edgecolor": c["grid"], "axes.labelcolor": c["ink2"], "xtick.color": c["ink2"], "ytick.color": c["ink2"],
        "text.color": c["ink"], "axes.titlecolor": c["ink"], "axes.titleweight": "semibold", "axes.titlesize": 11, "axes.titlelocation": "left",
        "axes.spines.top": False, "axes.spines.right": False, "axes.grid": True, "grid.color": c["grid"], "grid.linewidth": 0.8,
        "axes.axisbelow": True, "legend.frameon": False, "lines.linewidth": 2.0,
    })
    return c


def save(fig, name, outdir, theme):
    os.makedirs(f"{outdir}/figures", exist_ok=True)
    suffix = "" if theme == "light" else "_dark"
    fig.savefig(f"{outdir}/figures/{name}{suffix}.svg", format="svg")
    if theme == "light":
        fig.savefig(f"{outdir}/figures/{name}.png", dpi=160)
    plt.close(fig)


def write_csv(outdir, name, header, rows):
    os.makedirs(f"{outdir}/data", exist_ok=True)
    with open(f"{outdir}/data/{name}.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(header)
        w.writerows(rows)


def read_csv(path):
    return list(csv.DictReader(open(path))) if os.path.exists(path) else []


def note(ax, text, c, y=-0.22):
    ax.text(0, y, text, transform=ax.transAxes, fontsize=8, color=c["muted"], va="top")


# ---------------------------------------------------------------------------------------------- data
def acceptance_counts():
    out = []
    for step, f in (("Step 2 application", "step2_acceptance.md"), ("Step 3 framework", "step3_acceptance.md"), ("Step 4 fault effects", "step4_acceptance.md"),
                    ("Step 5 detection", "step5_acceptance.md"), ("Step 6 recovery", "step6_acceptance.md"), ("Step 7 evaluation", "step7_acceptance.md")):
        p = f"results/summaries/{f}"
        if not os.path.exists(p):
            continue
        t = open(p).read()
        m = re.search(r"\*\*(\d+) PASS, (\d+) LIMIT \(simulator\), (\d+) FAIL of (\d+)", t)
        if m:
            out.append((step, int(m.group(1)), int(m.group(2)), int(m.group(3))))
            continue
        m = re.search(r"\*\*(\d+)/(\d+) criteria passed", t)
        if m:
            p_, n = int(m.group(1)), int(m.group(2))
            out.append((step, p_, 0, n - p_))
    return out


def trace(run, fault):
    """CONTROL records of a run as (ms since injection, value, input); time from the DWT stamp of the SENSOR record before each."""
    inj = run.injected(fault)
    if inj is None:
        return None, []
    c0 = int(inj["kv"]["cycle"])
    cyc = None
    pts = []
    for r in run.recs:
        if r["tag"] == "SENSOR" and "cyc" in r["kv"]:
            cyc = int(r["kv"]["cyc"])
        elif r["tag"] == "CONTROL" and "value" in r["kv"] and cyc is not None:
            pts.append(((cyc - c0) / CYC_MS, int(r["kv"]["value"]), int(r["kv"]["input"])))
    return c0, pts


# ---------------------------------------------------------------------------------------------- figures
def fig_acceptance(rows, c, outdir):
    fig, ax = plt.subplots(figsize=(7.6, 0.55 * len(rows) + 1.3))
    ys = range(len(rows))[::-1]
    for y, (name, p, l, f) in zip(ys, rows):
        left = 0
        for v, col in ((p, c["good"]), (l, c["warn"]), (f, c["crit"])):
            if v:
                ax.barh(y, v, left=left, color=col, height=0.55, edgecolor=c["surface"], linewidth=2)
                left += v
        ax.text(left + 1.5, y, f"{p} pass" + (f", {l} limited by simulator" if l else "") + (f", {f} fail" if f else ""), va="center", fontsize=9, color=c["ink2"])
    ax.set_yticks(list(ys))
    ax.set_yticklabels([r[0] for r in rows])
    ax.set_xlabel("acceptance criteria")
    ax.set_xlim(0, max(r[1] + r[2] + r[3] for r in rows) * 1.45)
    ax.grid(axis="y", visible=False)
    ax.set_title("Acceptance criteria per step (green pass, yellow limited by the simulator, red fail)")
    fig.tight_layout()
    return fig


def fig_outcomes(mat, c, outdir):
    import textwrap
    fig, ax = plt.subplots(figsize=(13.0, 0.78 * len(mat) + 1.7))
    ax.axis("off")
    ax.set_xlim(-0.56, 2.0)
    ax.set_ylim(0, len(mat) + 0.6)
    ax.set_title("What happens after each fault: baseline vs protected firmware", pad=10)
    ax.text(0.02, len(mat) + 0.12, "Baseline (no protection)", fontsize=10, fontweight="bold", color=c["ink"], va="bottom")
    ax.text(1.02, len(mat) + 0.12, "Protected (detection + recovery)", fontsize=10, fontweight="bold", color=c["ink"], va="bottom")
    for i, (fault, b_text, b_sev, p_text, p_sev) in enumerate(mat):
        y = len(mat) - 1 - i
        ax.text(-0.01, y + 0.5, fault, ha="right", va="center", fontsize=9.5, color=c["ink"])
        for x0, text, sev in ((0.0, b_text, b_sev), (1.0, p_text, p_sev)):
            col = {"bad": c["crit"], "warn": c["warn"], "good": c["good"]}[sev]
            ax.add_patch(plt.Rectangle((x0 + 0.01, y + 0.06), 0.97, 0.88, color=c["grid"], zorder=1))
            ax.add_patch(plt.Rectangle((x0 + 0.01, y + 0.06), 0.012, 0.88, color=col, zorder=2))
            ax.text(x0 + 0.04, y + 0.5, "\n".join(textwrap.wrap(text, 78)), va="center", fontsize=8.4, color=c["ink"], zorder=3, linespacing=1.25)
    fig.subplots_adjust(left=0.01, right=0.99, top=0.93, bottom=0.01)
    return fig


def fig_coverage(rows, c, outdir):
    fig, ax = plt.subplots(figsize=(7.2, 3.6))
    labels = [r[0] for r in rows]
    ys = list(range(len(rows)))[::-1]
    for y, (name, n, d) in zip(ys, rows):
        ax.barh(y, 100.0, color=c["grid"], height=0.55)
        ax.barh(y, 100.0 * d / n, color=c["s1"], height=0.55, edgecolor=c["surface"], linewidth=2)
        ax.text(101.5, y, f"{d}/{n} detected  ({100.0 * d / n:.0f} %)", va="center", fontsize=9, color=c["ink2"])
    ax.set_yticks(ys)
    ax.set_yticklabels(labels)
    ax.set_xlim(0, 140)
    ax.set_xticks([0, 25, 50, 75, 100])
    ax.set_xlabel("detection coverage, % of injections (3 runs per fault)")
    ax.grid(axis="y", visible=False)
    ax.set_title("Detection coverage per fault class (protected build)")
    fig.tight_layout()
    return fig


def fig_latency(rows, c, outdir):
    """rows: (label, ms). One bar per fault+mechanism, log scale because the values span four decades."""
    rows = sorted(rows, key=lambda r: r[1])
    fig, ax = plt.subplots(figsize=(8.4, 0.34 * len(rows) + 1.6))
    ys = list(range(len(rows)))
    ax.barh(ys, [r[1] for r in rows], color=c["s1"], height=0.62)
    for y, (lab, ms) in zip(ys, rows):
        ax.text(ms * 1.12, y, f"{ms:.2f} ms" if ms < 10 else f"{ms:.0f} ms", va="center", fontsize=8.5, color=c["ink2"])
    ax.set_yticks(ys)
    ax.set_yticklabels([r[0] for r in rows], fontsize=8.5)
    ax.set_xscale("log")
    ax.set_xlim(0.1, max(r[1] for r in rows) * 6)
    ax.set_xlabel("detection latency, ms at 72 MHz (log scale; DWT cycles / 72 000)")
    ax.grid(axis="y", visible=False)
    ax.set_title("Detection latency per fault and mechanism")
    note(ax, "Three runs per fault give identical values (deterministic simulation).\nWWDG rows use the Wokwi workaround timeout, not the silicon one.", c, y=-0.10 - 0.9 / max(len(rows), 1))
    fig.tight_layout()
    return fig


def fig_rectime(rows, c, outdir):
    """rows: (fault, action, level, ms)."""
    fig, ax = plt.subplots(figsize=(8.2, 0.42 * len(rows) + 1.5))
    ys = list(range(len(rows)))[::-1]
    colmap = {1: c["s1"], 2: c["s3"], 3: c["s2"], 4: c["muted"]}
    for y, (f, act, lvl, ms) in zip(ys, rows):
        ax.barh(y, ms, color=colmap.get(lvl, c["s1"]), height=0.6)
        ax.text(ms + 1, y, f"{ms:.0f} ms   L{lvl} {act}", va="center", fontsize=8.5, color=c["ink2"])
    ax.set_yticks(ys)
    ax.set_yticklabels([r[0] for r in rows])
    ax.set_xlim(0, max(r[3] for r in rows) * 1.55)
    ax.set_xlabel("recovery time, ms (start of the action to verified normal operation)")
    ax.grid(axis="y", visible=False)
    ax.set_title("Recovery time of the successful recovery (colour = level: 1 blue, 2 green, 3 orange)")
    fig.tight_layout()
    return fig


def fig_recovery_rate(levels, c, outdir):
    """levels: list of (label, all_ok, all_n, study_ok, study_n)."""
    fig, ax = plt.subplots(figsize=(8.0, 3.9))
    xs = range(len(levels))
    w = 0.36
    for k, (col, lab, io, inn) in enumerate(((c["s1"], "all attempts (incl. escalation scenarios)", 1, 2), (c["s2"], "attempts on the nine study faults", 3, 4))):
        vals = [100.0 * L[io] / L[inn] if L[inn] else 0 for L in levels]
        bars = ax.bar([x + (k - 0.5) * w for x in xs], vals, w * 0.92, color=col, label=lab)
        for x, L, v in zip(xs, levels, vals):
            if L[inn]:
                ax.text(x + (k - 0.5) * w, v + 2, f"{L[io]}/{L[inn]}", ha="center", fontsize=8.5, color=c["ink2"])
    ax.set_xticks(list(xs))
    ax.set_xticklabels([L[0] for L in levels], fontsize=8.8)
    ax.set_ylim(0, 118)
    ax.set_ylabel("recovery success rate, %")
    ax.grid(axis="x", visible=False)
    ax.legend(loc="upper left", bbox_to_anchor=(0, 1.0), ncol=2, fontsize=8.5)
    ax.set_title("Recovery success rate per level (successful / attempts)", pad=26)
    fig.tight_layout()
    return fig


def fig_overhead(ov, c, outdir):
    fig, axes = plt.subplots(1, 3, figsize=(10.2, 3.6))
    spec = [("Flash (text+data)", "flash_bytes", "bytes", 64 * 1024), ("RAM (data+bss)", "ram_bytes", "bytes", 20 * 1024), ("CPU busy, fault-free", "cpu_busy_percent", "% of CPU time", None)]
    for ax, (title, key, unit, cap) in zip(axes, spec):
        row = ov.get(key)
        if not row:
            ax.axis("off")
            continue
        vals = [("baseline", row["baseline"], c["s1"]), ("detection only", row["detection_only"], c["s3"]), ("protected", row["protected"], c["s2"])]
        vals = [(n, float(v), col) for n, v, col in vals if v not in ("", None)]
        for x, (n, v, col) in enumerate(vals):
            ax.bar(x, v, 0.62, color=col)
            ax.text(x, v * 1.02, f"{v:,.0f}" if cap else f"{v:.1f} %", ha="center", fontsize=8.8, color=c["ink2"])
            if len(vals) > 1 and n == "protected":
                pct = 100.0 * (v - vals[0][1]) / vals[0][1]
                ax.text(x, v * 0.5, f"+{pct:.0f} %", ha="center", fontsize=10, color=c["surface"], fontweight="bold")
        ax.set_xticks(range(len(vals)))
        ax.set_xticklabels([v[0] for v in vals], fontsize=8.5)
        ax.set_title(title, fontsize=10)
        ax.set_ylabel(unit)
        ax.set_ylim(0, max(v[1] for v in vals) * 1.18)
        ax.grid(axis="x", visible=False)
        if cap:
            ax.axhline(cap, color=c["crit"], linewidth=1.2, linestyle=(0, (4, 3)))
            ax.text(len(vals) - 0.5, cap * 0.985, f"device limit {cap // 1024} KB", ha="right", va="top", fontsize=8, color=c["crit"])
            ax.set_ylim(0, cap * 1.12)
    fig.suptitle("Resource overhead of the protected firmware", x=0.01, ha="left", fontsize=11, fontweight="semibold")
    fig.tight_layout()
    return fig


def fig_outputs(panels, c, outdir):
    """Control output vs time since injection, baseline vs protected, one panel per fault."""
    fig, axes = plt.subplots(1, len(panels), figsize=(4.0 * len(panels), 3.4), sharey=False)
    if len(panels) == 1:
        axes = [axes]
    for ax, (fault, base, prot) in zip(axes, panels):
        lo, hi = -600, 2500
        for pts, col, lab in ((base, c["s1"], "baseline"), (prot, c["s2"], "protected")):
            xs = [p[0] for p in pts if lo <= p[0] <= hi]
            ys = [p[1] for p in pts if lo <= p[0] <= hi]
            ax.plot(xs, ys, color=col, label=lab, marker="o", markersize=3, linewidth=1.6)
        ax.axvline(0, color=c["muted"], linewidth=1, linestyle=(0, (3, 3)))
        ax.text(10, 1.0, "injection", transform=ax.get_xaxis_transform(), fontsize=8, color=c["muted"], va="bottom")
        ax.set_title(f"{fault}", fontsize=10)
        ax.set_xlabel("ms since injection")
        ax.set_xlim(lo, hi)
    axes[0].set_ylabel("control output, % (fan duty)")
    axes[0].legend(loc="best", fontsize=8.5)
    fig.suptitle("Control output around the injection: the protected build restores the nominal output", x=0.01, ha="left", fontsize=11, fontweight="semibold")
    fig.tight_layout()
    return fig


def fig_progress(panels, c, outdir):
    """Cumulative control cycles vs time since injection: flat = the application is not making progress."""
    cols = 3
    rowsn = (len(panels) + cols - 1) // cols
    fig, axes = plt.subplots(rowsn, cols, figsize=(4.0 * cols, 2.9 * rowsn), squeeze=False)
    for ax in axes.flat[len(panels):]:
        ax.axis("off")
    for ax, (fault, base, prot, base_end) in zip(axes.flat, panels):
        lo, hi = -500, 3000
        for pts, col, lab, lw in ((base, c["s1"], "baseline", 4.2), (prot, c["s2"], "protected", 1.8)):
            ts = sorted(p[0] for p in pts if lo <= p[0] <= hi)
            ys = [i + 1 for i in range(len(ts))]
            if ts and lab == "baseline" and ts[-1] < hi - 400:   # the baseline stopped: draw the flat line to the end of the window
                ts.append(hi)
                ys.append(ys[-1])
            ax.step(ts, ys, where="post", color=col, label=lab, linewidth=lw, solid_capstyle="butt")
        ax.axvline(0, color=c["muted"], linewidth=1, linestyle=(0, (3, 3)))
        ax.set_title(fault, fontsize=10)
        ax.set_xlim(lo, hi)
        ax.set_xlabel("ms since injection")
        ax.set_ylabel("control cycles")
        if base_end:
            ax.text(0.98, 0.05, base_end, transform=ax.transAxes, ha="right", fontsize=8, color=c["crit"])
    axes.flat[0].legend(loc="upper left", fontsize=8.5)
    fig.suptitle("Application progress after the injection (control cycles completed; a flat line = no progress)", x=0.01, ha="left", fontsize=11, fontweight="semibold")
    fig.tight_layout()
    return fig


def fig_escalation(rows, c, outdir):
    """rows: scenario -> list of (label, start_ms, end_ms, ok) relative to the first injection."""
    names = list(rows)
    fig, axes = plt.subplots(len(names), 1, figsize=(9.0, 1.15 * sum(max(2, len(rows[n])) for n in names) * 0.55 + 1.2), squeeze=False)
    for ax, n in zip(axes.flat, names):
        att = rows[n]
        for k, (lab, s, e, ok) in enumerate(att):
            col = c["good"] if ok else c["crit"]
            ax.barh(k, max(e - s, 8), left=s, color=col, height=0.55)
            ax.text(e + 15, k, f"{lab}  {'ok' if ok else 'failed'}", va="center", fontsize=8.3, color=c["ink2"])
        ax.set_yticks(range(len(att)))
        ax.set_yticklabels([f"attempt {i + 1}" for i in range(len(att))], fontsize=8)
        ax.invert_yaxis()
        ax.set_title(n, fontsize=9.5)
        ax.set_xlim(0, max(e for _, _, e, _ in att) * 1.45)
        ax.grid(axis="y", visible=False)
    axes.flat[-1].set_xlabel("ms since the first injection of the run (DWT cycles / 72 000; a level-3 bar includes the reboot)")
    fig.suptitle("Recovery attempts and escalation (green = verified success, red = failed)", x=0.01, ha="left", fontsize=11, fontweight="semibold")
    fig.tight_layout()
    return fig


# ---------------------------------------------------------------------------------------------- report
def md_table_block(path):
    return open(path).read().strip() if os.path.exists(path) else f"_({path} not found)_"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="results/report")
    ap.add_argument("--no-eval", action="store_true")
    ap.add_argument("--recovery")
    a = ap.parse_args()
    out = a.out
    os.makedirs(out, exist_ok=True)

    rdir = a.recovery or E.newest_complete("results/raw/step6/2*", set(E.C6.SCENARIOS))
    bdir = E.newest_complete("results/raw/step4/2*", set(E.BASE_SCEN.values()))
    pdir = E.newest_complete("results/raw/step5/2*", {"group", "tim01"})
    sources = {"baseline campaign (Step 4)": bdir, "detection campaign (Step 5)": pdir, "recovery campaign (Step 6)": rdir}
    print("sources:", sources)
    if rdir and bdir and not a.no_eval:
        subprocess.run([sys.executable, f"{HERE}/eval_step7.py", "--baseline", bdir, "--recovery", rdir], check=False, capture_output=True)

    runs = read_csv("results/summaries/step7_runs.csv")
    rec = read_csv("results/summaries/step7_recoveries.csv")
    lat = read_csv("results/summaries/step7_latencies.csv")
    ovr = {r["resource"]: r for r in read_csv("results/summaries/step7_overhead.csv")}
    figs = []

    def emit(name, title, builder, *args):
        for th in ("light", "dark"):
            c = theme_setup(th)
            fig = builder(*args, c, out)
            save(fig, name, out, th)
        figs.append((name, title))

    # 1 acceptance
    acc = acceptance_counts()
    if acc:
        write_csv(out, "acceptance", ["step", "pass", "limit", "fail"], acc)
        emit("acceptance", "Acceptance criteria per step", fig_acceptance, acc)

    # 2 outcome matrix
    if runs and rec:
        mat = []
        for f in STUDY:
            b = next((r for r in runs if r["build"] == "baseline" and r["fault"] == f and r["run"] == "1"), None)
            p = next((r for r in runs if r["build"] == "recovery" and r["fault"] == f and r["run"] == "1"), None)
            if not b or not p:
                continue
            btxt = b["final_state_or_behaviour"]
            bsev = "bad" if btxt.startswith(("crash", "hang", "task")) else "warn"
            btxt = {"crash": "crash: simulation ends (code 1006)", "hang": "hang: no output, no recovery"}.get(btxt.split(" (")[0], btxt)
            ats = [r for r in rec if r["fault"] == f and r["scenario"] == E.REC_SCEN[f] and r["run"] == "1"]
            chain = " > ".join(f"L{r['level']} {r['action'].replace('_', ' ')}{'' if r['success'] == 'True' else ' (failed)'}" for r in sorted(ats, key=lambda r: int(r["attempt"])))
            ok = bool(ats) and ats[-1]["success"] == "True"
            ptxt = f"detected by {p['mechanisms'].replace('+', ' + ')}; {chain}; " + ("normal operation restored" if ok else "NOT recovered")
            mat.append((f"{f} {E.NAMES[f]}", btxt, bsev, ptxt, "good" if ok else "bad"))
        write_csv(out, "outcomes", ["fault", "baseline", "baseline_severity", "protected", "protected_severity"], mat)
        emit("outcomes", "Baseline vs protected outcome per fault", fig_outcomes, mat)

    # 3 coverage
    if runs:
        rows = []
        for cl in E.CLASSES:
            ps = [r for r in runs if r["build"] == "recovery" and r["fault"] in STUDY and E.CLASS[r["fault"]] == cl]
            if ps:
                rows.append((cl.title(), len(ps), sum(1 for r in ps if r["detected"] == "True")))
        allr = [r for r in runs if r["build"] == "recovery" and r["fault"] in STUDY]
        rows.append(("Overall", len(allr), sum(1 for r in allr if r["detected"] == "True")))
        write_csv(out, "coverage", ["class", "injected", "detected"], rows)
        emit("coverage", "Detection coverage", fig_coverage, rows)

    # 4 latency (first detection per fault and mechanism, run 1; the 3 runs are identical, checked below)
    if lat:
        agg = {}
        for r in lat:
            agg.setdefault((r["fault"], r["mechanism"]), []).append(int(r["latency_cycles"]))
        rows = [(f"{f}  {m}", sum(v) / len(v) / CYC_MS) for (f, m), v in agg.items() if f in STUDY]
        write_csv(out, "latency", ["fault_mechanism", "avg_ms"], rows)
        emit("latency", "Detection latency", fig_latency, rows)

    # 5 recovery time of the successful recovery (run 1)
    if rec:
        rows = []
        for f in STUDY:
            ats = [r for r in rec if r["fault"] == f and r["scenario"] == E.REC_SCEN[f] and r["run"] == "1" and r["success"] == "True" and r["time_cycles"] not in ("", "None")]
            if ats:
                r = sorted(ats, key=lambda r: int(r["attempt"]))[-1]
                rows.append((f, r["action"].replace("_", " "), int(r["level"]), int(r["time_cycles"]) / CYC_MS))
        write_csv(out, "recovery_time", ["fault", "action", "level", "ms"], rows)
        emit("recovery_time", "Recovery time", fig_rectime, rows)

    # 6 recovery rate per level
    if rec:
        lv = {1: "Level 1\ntask restart /\nbus recovery", 2: "Level 2\ncheckpoint\nrestore", 3: "Level 3\nsystem reset", 4: "Level 4\nsafe state"}
        levels = []
        for L in (1, 2, 3, 4):
            allx = [r for r in rec if int(r["level"]) == L]
            stx = [r for r in allx if r["fault"] in STUDY]
            levels.append((lv[L], sum(1 for r in allx if r["success"] == "True"), len(allx), sum(1 for r in stx if r["success"] == "True"), len(stx)))
        write_csv(out, "recovery_rate", ["level", "ok_all", "n_all", "ok_study", "n_study"], [(l[0].replace("\n", " "),) + l[1:] for l in levels])
        emit("recovery_rate", "Recovery success rate", fig_recovery_rate, levels)

    # 7 overhead
    if ovr:
        write_csv(out, "overhead", ["resource", "baseline", "detection_only", "protected"], [(k, v["baseline"], v["detection_only"], v["protected"]) for k, v in ovr.items()])
        emit("overhead", "Resource overhead", fig_overhead, ovr)

    # 8/9 behaviour traces from the raw logs
    if bdir and rdir:
        bruns = {s: [Run(s, i, bdir) for i in (1,)] for s in set(E.BASE_SCEN.values())}
        rruns = {s: [Run(s, i, rdir) for i in (1,)] for s in E.C6.SCENARIOS}
        panels = []
        for f in ("MEM-01", "DATA-01", "DATA-02"):
            _, bp = trace(bruns[E.BASE_SCEN[f]][0], f)
            _, pp = trace(rruns[E.REC_SCEN[f]][0], f)
            panels.append((f"{f} {E.NAMES[f]}", bp, pp))
        write_csv(out, "trace_output", ["fault", "build", "ms_since_injection", "output", "input"],
                  [(p[0], bn, round(t, 1), v, i) for p in panels for bn, pts in (("baseline", p[1]), ("recovery", p[2])) for t, v, i in pts])
        emit("control_output", "Control output around the injection", fig_outputs, panels)

        panels = []
        for f in ("TIM-01", "TIM-02", "MEM-02", "CPU-01", "CPU-02", "PERIPH-01"):
            br, rr_ = bruns[E.BASE_SCEN[f]][0], rruns[E.REC_SCEN[f]][0]
            _, bp = trace(br, f)
            _, pp = trace(rr_, f)
            end = "simulation ends (1006)" if br.crashed else ""
            panels.append((f"{f} {E.NAMES[f]}", bp, pp, end))
        write_csv(out, "trace_progress", ["fault", "build", "ms_since_injection"], [(p[0], bn, round(t[0], 1)) for p in panels for bn, pts in (("baseline", p[1]), ("recovery", p[2])) for t in pts])
        emit("progress", "Application progress after the injection", fig_progress, panels)

        esc = {}
        for scen, label in (("periph", "PERIPH-01: bus recovery fails while the fault is held, reset releases it, bus recovery succeeds"),
                            ("safe_fail", "TIM-03: a fault that survives restart and reset ends in the safe state"),
                            ("safe_rep", "TIM-01 injected four times in a row: three resets, then the safe state")):
            run = rruns[scen][0]
            first = next((x for x in run.by_tag("FAULT") if x["kv"].get("state") == "INJECTED"), None)
            if first is None:
                continue
            c0 = int(first["kv"]["cycle"])
            ats = [r for r in rec if r["scenario"] == scen and r["run"] == "1" and r["end_cycle"] not in ("", "None")]
            esc[label] = [(f"L{r['level']} {r['action'].replace('_', ' ')}", (int(r["start_cycle"]) - c0) / CYC_MS, (int(r["end_cycle"]) - c0) / CYC_MS, r["success"] == "True")
                          for r in sorted(ats, key=lambda r: int(r["attempt"]))]
        esc = {k: v for k, v in esc.items() if v}
        if esc:
            write_csv(out, "escalation", ["scenario", "action", "start_ms", "end_ms", "success"], [(k,) + (a_[0], round(a_[1], 1), round(a_[2], 1), a_[3]) for k, v in esc.items() for a_ in v])
            emit("escalation", "Recovery attempts and escalation", fig_escalation, esc)

    # ---- markdown + html
    tables = [("Final comparison", "results/tables/final_comparison.md"), ("Detection coverage", "results/tables/step7_coverage.md"), ("Detection latency", "results/tables/step7_latency.md"),
              ("Recovery success and time", "results/tables/step7_recovery.md"), ("Resource overhead", "results/tables/step7_overhead.md"),
              ("Baseline fault effects (Step 4)", "results/tables/step4_fault_effects.md"), ("Detection per fault (Step 5)", "results/tables/step5_detection.md")]
    md = ["# Results report", "", "Generated by `Tests/tools/make_report.py` from the raw logs; re-run it after any campaign. Sources:", ""]
    md += [f"* {k}: `{v}`" for k, v in sources.items()]
    md += ["", "Read `docs/SIMULATOR_LIMITATIONS.md` before quoting numbers: the WWDG results are a simulator workaround, Wokwi delivers no fault exceptions, and the simulation is deterministic (repeated runs are bit-identical).", ""]
    for name, title in figs:
        md += [f"## {title}", "", f"![{title}](figures/{name}.png)", ""]
    for title, p in tables:
        md += [f"## Table: {title}", "", md_table_block(p), ""]
    open(f"{out}/report.md", "w").write("\n".join(md))

    css = """body{margin:0;padding:24px 16px;background:#fcfcfb;color:#0b0b0b;font:15px/1.5 system-ui,sans-serif}
main{max-width:1100px;margin:0 auto}h1{font-size:24px}h2{font-size:18px;margin-top:32px}table{border-collapse:collapse;font-size:13px;margin:8px 0;display:block;overflow-x:auto}
td,th{border-bottom:1px solid #e6e5e1;padding:4px 10px;text-align:left;vertical-align:top}img{max-width:100%;height:auto}code{font-size:12.5px}.src li{color:#52514e}
@media (prefers-color-scheme:dark){body{background:#1a1a19;color:#fff}td,th{border-color:#34332f}.src li{color:#c3c2b7}}"""
    def md_to_html(t):
        lines, outl, i = t.split("\n"), [], 0
        while i < len(lines):
            ln = lines[i]
            if ln.startswith("|") and i + 1 < len(lines) and set(lines[i + 1].replace("|", "").strip()) <= set("-: "):
                hdr = [x.strip() for x in ln.strip("|").split("|")]
                outl.append("<table><tr>" + "".join(f"<th>{html.escape(h)}</th>" for h in hdr) + "</tr>")
                i += 2
                while i < len(lines) and lines[i].startswith("|"):
                    cells = [x.strip() for x in lines[i].strip("|").split("|")]
                    outl.append("<tr>" + "".join(f"<td>{html.escape(x)}</td>" for x in cells) + "</tr>")
                    i += 1
                outl.append("</table>")
                continue
            if ln.startswith("# "):
                outl.append(f"<h3>{html.escape(ln[2:])}</h3>")
            elif ln.strip():
                outl.append(f"<p>{html.escape(ln)}</p>")
            i += 1
        return "\n".join(outl)
    h = [f"<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Fault study results</title><style>{css}</style></head><body><main>",
         "<h1>Fault detection and self-recovery on an STM32F103C8 (Wokwi): results</h1>",
         "<p>Generated by <code>Tests/tools/make_report.py</code> from the raw logs. Read <code>docs/SIMULATOR_LIMITATIONS.md</code> before quoting numbers.</p><ul class='src'>"]
    h += [f"<li>{html.escape(k)}: <code>{html.escape(str(v))}</code></li>" for k, v in sources.items()]
    h.append("</ul>")
    for name, title in figs:
        h.append(f"<h2>{html.escape(title)}</h2><picture><source media='(prefers-color-scheme: dark)' srcset='figures/{name}_dark.svg'><img src='figures/{name}.svg' alt='{html.escape(title)}'></picture>")
    for title, p in tables:
        h.append(f"<h2>Table: {html.escape(title)}</h2>{md_to_html(md_table_block(p))}")
    h.append("</main></body></html>")
    open(f"{out}/report.html", "w").write("\n".join(h))
    print(f"report: {out}/report.html  ({len(figs)} figures)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
