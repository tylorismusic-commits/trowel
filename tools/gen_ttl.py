#!/usr/bin/env python3
"""Generate Trowel's LV2 bundle metadata (manifest, port tables, presets).

The C++ parameter table in src/trowel/param_info.hpp is the source of truth.
This script mirrors it for the RDF side; keep the two in sync when adding a
control.  Run from the repository root:  python3 tools/gen_ttl.py
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LV2 = ROOT / "lv2"
LV2.mkdir(exist_ok=True)

BASE = "https://github.com/tylorismusic-commits/trowel"
MASTER = BASE + "-master"

# (index, symbol, name, min, max, default, unit, group, kind, points)
# kind: "audio_in"/"audio_out" handled separately; control kinds are
# "float", "toggle", "enum", "log", "meter", "latency", "telemetry".
P = []


def p(idx, sym, name, lo, hi, default, unit, group, kind="float", points=None):
    P.append(dict(idx=idx, sym=sym, name=name, lo=lo, hi=hi, default=default,
                  unit=unit, group=group, kind=kind, points=points or []))


p(4, "pultec_on", "Colour", 0, 1, 1, None, "colour", "toggle")
p(5, "pul_low_freq", "Low Freq", 20, 100, 60, "hz", "colour", "enum",
  [("20", 20), ("30", 30), ("60", 60), ("100", 100)])
p(6, "pul_low_boost", "Low Boost", 0, 13.5, 0, "db", "colour")
p(7, "pul_low_cut", "Low Cut", -17.5, 0, 0, "db", "colour")
p(8, "pul_high_freq", "High Freq", 3000, 16000, 10000, "hz", "colour", "enum",
  [("3k", 3000), ("4k", 4000), ("5k", 5000), ("8k", 8000), ("10k", 10000),
   ("12k", 12000), ("16k", 16000)])
p(9, "pul_high_boost", "High Boost", 0, 18, 0, "db", "colour")
p(10, "pul_bandwidth", "Bandwidth", 0, 10, 5, None, "colour")
p(11, "pul_high_cut_freq", "High Cut Freq", 5000, 20000, 10000, "hz", "colour",
  "enum", [("5k", 5000), ("10k", 10000), ("20k", 20000)])
p(12, "pul_high_cut", "High Cut", -16, 0, 0, "db", "colour")
p(13, "pul_iron", "Iron", 0, 1, 0, None, "colour", "toggle")

p(14, "ssl_on", "Console", 0, 1, 1, None, "console", "toggle")
p(15, "ssl_lf_freq", "LF Freq", 30, 150, 60, "hz", "console", "enum",
  [("30", 30), ("40", 40), ("60", 60), ("75", 75), ("90", 90), ("110", 110),
   ("130", 130), ("150", 150)])
p(16, "ssl_lf_gain", "LF Gain", -15, 15, 0, "db", "console")
p(17, "ssl_lf_bell", "LF Shape", 0, 1, 0, None, "console", "toggle")
p(18, "ssl_lmf_freq", "LMF Freq", 100, 2000, 300, "hz", "console", "enum",
  [("100", 100), ("150", 150), ("200", 200), ("300", 300), ("400", 400),
   ("600", 600), ("800", 800), ("1k", 1000), ("1.5k", 1500), ("2k", 2000)])
p(19, "ssl_lmf_gain", "LMF Gain", -15, 15, 0, "db", "console")
p(20, "ssl_hmf_freq", "HMF Freq", 1500, 10000, 3000, "hz", "console", "enum",
  [("1.5k", 1500), ("2k", 2000), ("3k", 3000), ("4k", 4000), ("5k", 5000),
   ("6k", 6000), ("8k", 8000), ("10k", 10000)])
p(21, "ssl_hmf_gain", "HMF Gain", -15, 15, 0, "db", "console")
p(22, "ssl_hf_freq", "HF Freq", 3000, 16000, 10000, "hz", "console", "enum",
  [("3k", 3000), ("4k", 4000), ("5k", 5000), ("6k", 6000), ("8k", 8000),
   ("10k", 10000), ("12k", 12000), ("16k", 16000)])
p(23, "ssl_hf_gain", "HF Gain", -15, 15, 0, "db", "console")
p(24, "ssl_hf_bell", "HF Shape", 0, 1, 0, None, "console", "toggle")
p(25, "ssl_hpf", "HPF", 0, 160, 0, "hz", "console", "enum",
  [("OFF", 0), ("30", 30), ("50", 50), ("80", 80), ("120", 120), ("160", 160)])
p(26, "ssl_lpf", "LPF", 0, 20000, 0, "hz", "console", "enum",
  [("OFF", 0), ("8k", 8000), ("10k", 10000), ("12k", 12000),
   ("16k", 16000), ("20k", 20000)])

p(27, "tame_on", "Tame", 0, 1, 1, None, "tame", "toggle")
p(28, "tame_amount", "Amount", 0, 100, 0, "pc", "tame")
p(29, "tame_focus", "Focus", 0, 100, 50, "pc", "tame")
p(30, "tame_selectivity", "Selectivity", 0, 100, 50, "pc", "tame")
p(31, "tame_sensitivity", "Sensitivity", 0, 100, 50, "pc", "tame")
p(32, "tame_attack", "Attack", 1, 100, 10, "ms", "tame", "log")
p(33, "tame_release", "Release", 50, 2000, 250, "ms", "tame", "log")
p(34, "tame_mix", "Mix", 0, 100, 100, "pc", "tame")
p(35, "tame_listen", "Listen", 0, 1, 0, None, "tame", "toggle")

p(36, "input_gain", "Input", -24, 24, 0, "db", "global")
p(37, "output_gain", "Output", -24, 24, 0, "db", "global")
p(38, "routing", "Routing", 0, 5, 0, None, "global", "enum",
  [("COLOUR > CONSOLE > TAME", 0), ("COLOUR > TAME > CONSOLE", 1),
   ("CONSOLE > COLOUR > TAME", 2), ("CONSOLE > TAME > COLOUR", 3),
   ("TAME > COLOUR > CONSOLE", 4), ("TAME > CONSOLE > COLOUR", 5)])
p(39, "enable", "Enabled", 0, 1, 1, None, "global", "toggle")

p(40, "meter_gr", "Tame Reduction", -60, 0, 0, "db", "global", "meter")
p(41, "meter_out", "Output Level", -60, 6, -60, "db", "global", "meter")
p(42, "latency", "Latency", 0, 100000, 0, None, "global", "latency")

for i in range(32):
    p(43 + i, f"spectrum_{i + 1}", f"Spectrum {i + 1}", -100, 6, -100, "db",
      "telemetry", "telemetry")
for i in range(32):
    p(75 + i, f"gain_{i + 1}", f"Tame Gain {i + 1}", -24, 0, 0, "db",
      "telemetry", "telemetry")

p(107, "stereo_mode", "Stereo", 0, 1, 0, None, "tame", "enum",
  [("LINKED", 0), ("MID/SIDE", 1)])
p(108, "auto_gain", "Auto Gain", 0, 1, 0, None, "master", "toggle")
p(109, "delta", "Delta", 0, 1, 0, None, "master", "toggle")

UNIT_MAP = {"db": "units:db", "hz": "units:hz", "ms": "units:ms",
            "pc": "units:pc"}


def fmt(v):
    if isinstance(v, float) and v.is_integer():
        return str(int(v))
    return f"{v:g}"


def port_block(p, master, plugin_prefix):
    lines = ["\t\tlv2:port ["]
    lines.append("\t\t\ta lv2:ControlPort , lv2:InputPort ;" if p["kind"] not in
                 ("meter", "latency", "telemetry") else
                 "\t\t\ta lv2:ControlPort , lv2:OutputPort ;")
    lines.append(f"\t\t\tlv2:index {p['idx']} ;")
    lines.append(f"\t\t\tlv2:symbol \"{p['sym']}\" ;")
    lines.append(f"\t\t\tlv2:name \"{p['name']}\" ;")
    lines.append(f"\t\t\tlv2:group <{plugin_prefix}#{p['group']}> ;")
    lines.append(f"\t\t\tlv2:default {fmt(p['default'])} ;")
    lines.append(f"\t\t\tlv2:minimum {fmt(p['lo'])} ;")
    lines.append(f"\t\t\tlv2:maximum {fmt(p['hi'])} ;")
    if p["unit"]:
        lines.append(f"\t\t\tunits:unit {UNIT_MAP[p['unit']]} ;")
    flags = []
    if p["kind"] == "toggle":
        flags.append("lv2:toggled")
    if p["kind"] == "enum":
        flags.append("lv2:enumeration")
    if p["kind"] == "log":
        flags.append("lv2:logarithmic")
    if p["kind"] == "latency":
        flags.append("lv2:reportsLatency")
    if p["kind"] == "telemetry":
        flags.append("pprops:notOnGUI")
    for f in flags:
        lines.append(f"\t\t\tlv2:portProperty {f} ;")
    for label, value in p["points"]:
        lines.append(f"\t\t\tlv2:scalePoint [ rdfs:label \"{label}\" ; "
                     f"rdf:value {fmt(value)} ] ;")
    lines.append("\t\t\t] ;")
    return lines


def plugin_ttl(uri, binary, name, desc, master):
    prefix = "trowel" if not master else "trowel-master"
    ports = [q for q in P if master or q["idx"] < 107]
    lines = [
        "@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .",
        "@prefix doap: <http://usefulinc.com/ns/doap#> .",
        "@prefix foaf: <http://xmlns.com/foaf/0.1/> .",
        "@prefix pg:   <http://lv2plug.in/ns/ext/port-groups#> .",
        "@prefix pprops: <http://lv2plug.in/ns/ext/port-props#> .",
        "@prefix rdf:  <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .",
        "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .",
        "@prefix units: <http://lv2plug.in/ns/extensions/units#> .",
        "@prefix ui:   <http://lv2plug.in/ns/extensions/ui#> .",
        "",
        f"@prefix {prefix}: <{uri}> .",
        "",
        f"<{uri}>",
        "\ta lv2:Plugin , lv2:EQPlugin ;",
        f"\tlv2:binary <{binary}> ;",
        "\tlv2:majorVersion 0 ;",
        "\tlv2:minorVersion 1 ;",
        "\tlv2:microVersion 0 ;",
        f"\tlv2:project <{BASE}> ;",
        "\tlv2:optionalFeature lv2:hardRTCapable ;",
        "\tdoap:license <https://spdx.org/licenses/GPL-3.0-or-later.html> ;",
        f"\tdoap:name \"{name}\" ;",
        f"\tdoap:description \"{desc}\" ;",
        f"\tdoap:shortdesc \"{desc}\" ;",
        f"\tfoaf:homepage <{BASE}> ;",
        f"\tui:ui <{uri}#ui> ;",
    ]
    # audio ports
    for idx, sym, nm, desig in [(0, "in_l", "Input Left", "left"),
                                (1, "in_r", "Input Right", "right")]:
        lines += [
            "\tlv2:port [",
            "\t\ta lv2:AudioPort , lv2:InputPort ;",
            f"\t\tlv2:index {idx} ;",
            f"\t\tlv2:symbol \"{sym}\" ;",
            f"\t\tlv2:name \"{nm}\" ;",
            f"\t\tlv2:group <{prefix}#input> ;",
            f"\t\tlv2:designation pg:{desig} ;",
            "\t\t] ;",
        ]
    for idx, sym, nm, desig in [(2, "out_l", "Output Left", "left"),
                                (3, "out_r", "Output Right", "right")]:
        lines += [
            "\tlv2:port [",
            "\t\ta lv2:AudioPort , lv2:OutputPort ;",
            f"\t\tlv2:index {idx} ;",
            f"\t\tlv2:symbol \"{sym}\" ;",
            f"\t\tlv2:name \"{nm}\" ;",
            f"\t\tlv2:group <{prefix}#output> ;",
            f"\t\tlv2:designation pg:{desig} ;",
            "\t\t] ;",
        ]
    for q in ports:
        lines += port_block(q, master, prefix)
    # fix the trailing comma on the last port
    for i in range(len(lines) - 1, -1, -1):
        if lines[i].strip() == "] ;":
            lines[i] = lines[i].replace("] ;", "] .")
            break
    lines.append("")
    # groups
    for gid, gname in [("input", "Input"), ("output", "Output"),
                       ("colour", "Colour"), ("console", "Console"),
                       ("tame", "Tame"), ("global", "Global"),
                       ("master", "Master"), ("telemetry", "Telemetry")]:
        if gid == "master" and not master:
            continue
        lines += [
            f"<{prefix}#{gid}> a pg:Group ;",
            f"\tlv2:symbol \"{gid}\" ;",
            f"\tlv2:name \"{gname}\" .",
            "",
        ]
    return "\n".join(lines)


# Shared preset definitions.  The values mirror the UI preset bar.
PRESETS = {
    "vocal": ("Vocal - Air & Tame", {
        "pul_low_freq": 100, "pul_low_boost": 1.5, "pul_high_freq": 12000,
        "pul_high_boost": 2, "pul_bandwidth": 6, "ssl_lf_freq": 90,
        "ssl_lf_gain": -1, "ssl_lmf_freq": 300, "ssl_lmf_gain": -2,
        "ssl_hmf_freq": 3000, "ssl_hmf_gain": 1.5, "ssl_hf_freq": 10000,
        "ssl_hf_gain": 1, "ssl_hpf": 80, "tame_amount": 35,
        "tame_focus": 60, "tame_selectivity": 55, "tame_sensitivity": 55,
    }),
    "drum_bus": ("Drum Bus - Punch", {
        "pul_low_freq": 60, "pul_low_boost": 2.5, "pul_low_cut": -1.5,
        "pul_high_freq": 8000, "pul_high_boost": 1, "pul_bandwidth": 7,
        "ssl_lf_freq": 60, "ssl_lf_gain": 2, "ssl_lmf_freq": 400,
        "ssl_lmf_gain": -2, "ssl_hmf_freq": 3000, "ssl_hmf_gain": 1,
        "ssl_hf_freq": 12000, "ssl_hf_gain": 1, "tame_amount": 20,
        "tame_focus": 70, "tame_sensitivity": 50, "output_gain": -1,
    }),
    "mix_bus": ("Mix Bus - Polish", {
        "pul_low_freq": 30, "pul_low_boost": 0.5, "pul_high_freq": 12000,
        "pul_high_boost": 0.5, "pul_iron": 1, "ssl_lf_freq": 60,
        "ssl_lf_gain": 0.5, "ssl_lmf_freq": 300, "ssl_lmf_gain": -0.5,
        "ssl_hmf_freq": 3000, "ssl_hmf_gain": 0.5, "ssl_hf_freq": 12000,
        "ssl_hf_gain": 0.5, "ssl_hpf": 30, "tame_amount": 15,
        "tame_focus": 70, "tame_sensitivity": 45, "output_gain": -0.5,
    }),
    "broken": ("Broken Trowel", {
        "pul_low_freq": 60, "pul_low_boost": 10, "pul_low_cut": -10,
        "pul_high_freq": 5000, "pul_high_boost": 12, "pul_bandwidth": 1,
        "pul_high_cut_freq": 10000, "pul_high_cut": -10, "pul_iron": 1,
        "ssl_lf_freq": 100, "ssl_lf_gain": 12, "ssl_lmf_freq": 300,
        "ssl_lmf_gain": -12, "ssl_hmf_freq": 5000, "ssl_hmf_gain": 12,
        "ssl_hf_freq": 10000, "ssl_hf_gain": -12, "ssl_hpf": 160,
        "ssl_lpf": 8000, "tame_amount": 100, "tame_focus": 50,
        "tame_selectivity": 80, "tame_sensitivity": 80, "output_gain": -6,
    }),
}

controls = [q for q in P if q["kind"] not in ("meter", "latency", "telemetry")]
defaults = {q["sym"]: q["default"] for q in controls}

preset_lines = []
for key, (label, values) in PRESETS.items():
    for uri, master in [(BASE, False), (MASTER, True)]:
        pl = f"<{uri}#preset_{key}>"
        preset_lines += [
            f"{pl} a pset:Preset ;",
            f"\tlv2:appliesTo <{uri}> ;",
            f"\trdfs:label \"{label}\" ;",
        ]
        merged = dict(defaults)
        merged.update(values)
        for q in controls:
            if master or q["idx"] < 107:
                preset_lines.append(
                    f"\tlv2:port [ lv2:symbol \"{q['sym']}\" ; "
                    f"lv2:value {fmt(merged.get(q['sym'], q['default']))} ] ;")
        preset_lines[-1] = preset_lines[-1].replace("] ;", "] .")
        preset_lines.append("")

presets_ttl = """@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .
@prefix pset: <http://lv2plug.in/ns/ext/presets#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .

""" + "\n".join(preset_lines)

def manifest_for(uri, binary, ttl, ui_binary):
    out = f"""@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix ui:   <http://lv2plug.in/ns/extensions/ui#> .
@prefix opts: <http://lv2plug.in/ns/ext/options#> .
@prefix pset: <http://lv2plug.in/ns/ext/presets#> .

<{uri}>
\ta lv2:Plugin ;
\tlv2:binary <{binary}> ;
\trdfs:seeAlso <{ttl}> ;
\tui:ui <{uri}#ui> .

<{uri}#ui>
\ta ui:X11UI ;
\tui:binary <{ui_binary}> ;
\tlv2:requiredFeature ui:idleInterface , ui:parent ;
\tlv2:optionalFeature ui:resize , opts:options ;
\tlv2:extensionData ui:idleInterface , ui:resize ;
\topts:supportedOption ui:scaleFactor ;
\trdfs:seeAlso <{ttl}> .

"""
    for key in PRESETS:
        out += (f"<{uri}#preset_{key}> a pset:Preset ;\n"
                f"\tlv2:appliesTo <{uri}> ;\n"
                "\trdfs:seeAlso <presets.ttl> .\n\n")
    return out


(LV2 / "manifest-trowel.ttl").write_text(
    manifest_for(BASE, "trowel.so", "trowel.ttl", "trowelUI.so"))
(LV2 / "manifest-trowel-master.ttl").write_text(
    manifest_for(MASTER, "trowel-master.so", "trowel-master.ttl",
                 "trowelMasterUI.so"))
(LV2 / "trowel.ttl").write_text(plugin_ttl(
    BASE, "trowel.so", "Trowel",
    "Zero-latency EQ suite: Pultec-style colour, 4000E console EQ and an "
    "adaptive resonance suppressor.", False))
(LV2 / "trowel-master.ttl").write_text(plugin_ttl(
    MASTER, "trowel-master.so", "Trowel Master",
    "Mastering EQ suite: Pultec-style colour, 4000E console EQ and a "
    "4096-point spectral resonance suppressor.", True))
(LV2 / "presets.ttl").write_text(presets_ttl)
print("wrote lv2/manifest-trowel.ttl, manifest-trowel-master.ttl, "
      "trowel.ttl, trowel-master.ttl, presets.ttl")
