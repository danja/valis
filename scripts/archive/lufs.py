# scripts/shot/lufs.py
#
# Aim a mix at a loudness in LUFS, using valis-shot for the measurement (Python has no BS.1770 meter here).
#
#   import lufs
#   gain_db, limited = lufs.gain_to_target("mix.wav", -14.0)    # dB to apply; limited if the peak ceiling stopped it
#
#   python3 lufs.py mix.wav -14        print the gain that would reach -14 LUFS
import json, os, subprocess, sys

VALIS = os.environ.get("VALIS", os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")))
SHOT = next((p for p in (os.path.join(VALIS, d, "valis_shot_artefacts", c, "valis_shot")
                         for d, c in (("build-release", "Release"), ("build", "Debug"))) if os.path.exists(p)), None)


def measure(path):
    """The loudness block of `valis-shot measure`: integratedLufs, rangeLu, truePeakDbtp and the rest."""
    if SHOT is None:
        raise RuntimeError("valis_shot is not built: cmake --build build-release --target valis_shot")
    out = subprocess.run([SHOT, "measure", path], capture_output=True, text=True)
    start = out.stdout.find("{")    # JUCE prints a banner line first
    if out.returncode != 0 or start < 0:
        raise RuntimeError("valis-shot measure failed on %s: %s" % (path, (out.stderr or out.stdout).strip()))
    return json.loads(out.stdout[start:])["loudness"]


def gain_to_target(path, target_lufs, ceiling_dbtp=-1.0):
    """Gain in dB that brings the file to `target_lufs`, held back so its true peak stays at or under `ceiling_dbtp`.
    Returns (gain_db, limited): limited is True when the ceiling, not the target, set the gain."""
    loud = measure(path)
    wanted = target_lufs - loud["integratedLufs"]
    allowed = ceiling_dbtp - loud["truePeakDbtp"]
    return (allowed, True) if wanted > allowed else (wanted, False)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    g, limited = gain_to_target(sys.argv[1], float(sys.argv[2]))
    print("gain %+.2f dB%s" % (g, " (held back by the -1 dBTP ceiling)" if limited else ""))
