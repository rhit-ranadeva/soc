#!/usr/bin/env python3
"""Load the GEMV bitstream and apply the existing device-tree overlay."""
from pathlib import Path
import os

ROOT = Path(__file__).resolve().parent
bit = ROOT / "Ultra96_Design_wrapper.bit"
hwh = bit.with_suffix(".hwh")
dtbo = bit.with_suffix(".dtbo")  # Reuse the existing DTBO unchanged.

if os.geteuid() != 0:
    raise SystemExit("Run as root in the PYNQ Python environment.")

for path in (bit, hwh, dtbo):
    if not path.is_file():
        raise SystemExit(f"Missing required file: {path}")

if Path("/sys/module/gemv_drv").exists():
    raise SystemExit(
        "Stop GEMV applications and run 'rmmod gemv_drv' first."
    )

live = Path("/sys/firmware/devicetree/base")
if not (live / "fabric@A0000000").is_dir():
    raise SystemExit("The existing DTBO requires /fabric@A0000000.")
if not (live / "axi").is_dir():
    raise SystemExit("The existing DTBO requires /axi.")

# The existing DTBO still references this symbol, even though the
# polling driver ignores the interrupt resource.
gic_symbol = live / "__symbols__" / "gic"
if (
    not gic_symbol.is_file()
    or gic_symbol.read_bytes().rstrip(b"\0")
    != b"/axi/interrupt-controller@f9010000"
):
    raise SystemExit("The GIC symbol does not match the existing DTBO.")

from pynq import Overlay
from pynq.devicetree import DeviceTreeSegment

segment = DeviceTreeSegment(str(dtbo))
if Path(segment.sysfs_dir).is_dir():
    segment.remove()

overlay = Overlay(str(bit), dtbo=str(dtbo), download=False)
overlay.download()

print("Overlay loaded:", overlay.is_loaded())
print("Existing DTBO applied:", segment.is_dtbo_applied())
print("Discovered IP blocks:")
for name in overlay.ip_dict:
    print("  ", name)

print(f"Next: insmod {ROOT / 'driver_build/gemv_drv.ko'}")
