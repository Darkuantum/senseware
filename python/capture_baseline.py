#!/usr/bin/env python3
"""
capture_baseline.py — Sensewear Phase 2: Baseline Data Capture

Connects to the ESP32 over serial, reads the CSV telemetry stream, and
records every row to a timestamped file under data/raw/.

Expected serial format (one line per second, 115200 baud):
    millis,heart_rate,spo2,emg_envelope,motion_magnitude,alert
    1234,72.0,98.0,0.45,1.02,0
    ...

Accepts 4–6 columns for forwards compatibility.  The first 4
(millis, heart_rate, emg_envelope, motion_magnitude) are always
captured; extra columns (spo2, alert) are logged when present.

Usage:
    python python/capture_baseline.py [--port PORT] [--duration MINUTES]
"""

from __future__ import annotations

import argparse
import glob
import logging
import os
import signal
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import serial
import serial.tools.list_ports

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

BAUD_RATE = 115200
HEADER_LINE_MIN = "millis,heart_rate,emg_envelope,motion_magnitude"
HEADER_LINE_FULL = "millis,heart_rate,spo2,emg_envelope,motion_magnitude,alert"
RECONNECT_BACKOFF_INIT = 1.0  # seconds – doubles each failed attempt
RECONNECT_BACKOFF_MAX = 30.0
RECONNECT_BACKOFF_RESET = 5.0  # seconds of successful read before resetting

DEFAULT_DURATION = 30  # minutes
LIVE_STATUS_INTERVAL = 1.0  # seconds between terminal updates

PROJECT_ROOT = Path(__file__).resolve().parent.parent
DATA_DIR = PROJECT_ROOT / "data" / "raw"

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger("capture_baseline")

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def find_serial_port() -> str | None:
    """Auto-detect a likely ESP32 serial port on Linux."""
    candidates = sorted(glob.glob("/dev/ttyUSB*")) + sorted(glob.glob("/dev/ttyACM*"))
    if candidates:
        return candidates[0]

    # Fall back to pyserial's port list (works on macOS / Windows too)
    ports = serial.tools.list_ports.comports()
    for p in sorted(ports, key=lambda p: p.device):
        if "cp210" in p.description.lower() or "ch34" in p.description.lower():
            return p.device
    if ports:
        return ports[0].device

    return None


def make_output_path() -> Path:
    """Return a path like data/raw/baseline_20240115_143000.csv."""
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    return DATA_DIR / f"baseline_{ts}.csv"


def format_elapsed(seconds: float) -> str:
    """Pretty-print elapsed time as e.g. '2m 04s'."""
    m, s = divmod(int(seconds), 60)
    return f"{m}m {s:02d}s"


# ---------------------------------------------------------------------------
# Core capture loop
# ---------------------------------------------------------------------------


def capture(port: str, duration_minutes: float) -> None:
    """Open the serial port and stream CSV rows to disk until interrupted or
    the duration limit is reached."""

    output_path = make_output_path()
    deadline = (
        None if duration_minutes <= 0 else time.monotonic() + duration_minutes * 60
    )

    # We accumulate rows in memory *and* flush periodically so that a crash
    # loses at most a few seconds of data.
    row_count = 0
    header_seen = False

    # -- Open the output file and write the CSV header -----------------------
    try:
        fh = open(output_path, "w", encoding="utf-8", newline="")
    except OSError as exc:
        log.error("Cannot create output file %s: %s", output_path, exc)
        sys.exit(1)

    # Write the minimal header; we may append extra columns later once we
    # see the firmware header row.
    fh.write(HEADER_LINE_MIN + "\n")
    fh.flush()
    header_columns_seen = 4  # default: minimal 4-col header

    def cleanup() -> None:
        fh.flush()
        fh.close()
        ts_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        if row_count > 0:
            log.info("Stopped. %d rows saved to %s", row_count, output_path)
        else:
            log.warning("No data rows captured.")

    # Register signal handler for clean Ctrl+C exit
    interrupted = False

    def _sigint_handler(signum, frame) -> None:  # type: ignore[misc]
        nonlocal interrupted
        interrupted = True

    signal.signal(signal.SIGINT, _sigint_handler)

    # -- Serial connection with auto-reconnect -------------------------------
    backoff = RECONNECT_BACKOFF_INIT
    ser: serial.Serial | None = None
    start_time = time.monotonic()
    last_status = 0.0
    consecutive_success = 0.0  # time since last failed read

    log.info("Connected to %s", port)
    log.info("Recording to %s", output_path)

    try:
        while not interrupted:
            # ---- Check duration deadline ----
            if deadline is not None and time.monotonic() >= deadline:
                log.info("Duration limit (%.1f min) reached.", duration_minutes)
                break

            # ---- Open / reopen serial port if needed ----
            if ser is None or not ser.is_open:
                try:
                    ser = serial.Serial(port, BAUD_RATE, timeout=1.0)
                    log.info("Serial port %s opened.", port)
                    backoff = RECONNECT_BACKOFF_INIT
                    # After a fresh open the ESP32 may print the header or
                    # some boot text — discard until we see our header.
                    header_seen = False
                except serial.SerialException as exc:
                    log.warning(
                        "Cannot open %s (%s). Retrying in %.1fs…",
                        port,
                        exc,
                        backoff,
                    )
                    time.sleep(backoff)
                    backoff = min(backoff * 2, RECONNECT_BACKOFF_MAX)
                    continue
                except PermissionError:
                    log.error(
                        "Permission denied for %s. "
                        "Add your user to the 'dialout' group: "
                        "sudo usermod -aG dialout $USER",
                        port,
                    )
                    cleanup()
                    sys.exit(1)
                except FileNotFoundError:
                    log.error("Port %s does not exist.", port)
                    cleanup()
                    sys.exit(1)

            # ---- Read a line ----
            try:
                line = ser.readline()
            except serial.SerialException as exc:
                log.warning("Serial read error: %s", exc)
                ser.close()
                ser = None
                continue

            if not line:
                # Timeout — no data this cycle, just loop.
                continue

            raw = line.decode("utf-8", errors="replace").strip()

            # Discard empty / boot-text lines
            if not raw:
                continue

            # ---- Handle the header row ----
            if raw.lower().startswith("millis"):
                header_seen = True
                consecutive_success = 0.0
                # Detect how many columns the firmware is sending.
                fw_cols = len(raw.split(","))
                if fw_cols != header_columns_seen:
                    # Firmware has more (or fewer) columns than what we wrote.
                    # Rewrite the CSV header to match.
                    if fw_cols >= 6:
                        new_header = HEADER_LINE_FULL
                    elif fw_cols >= 5:
                        # 5-column: millis,heart_rate,spo2,emg_envelope,motion_magnitude
                        new_header = ",".join(HEADER_LINE_FULL.split(",")[:5])
                    else:
                        new_header = HEADER_LINE_MIN
                    if new_header != HEADER_LINE_MIN:
                        # Rewrite the header in the output file
                        fh.seek(0)
                        fh.truncate()
                        fh.write(new_header + "\n")
                        fh.flush()
                        header_columns_seen = fw_cols
                        log.info(
                            "Firmware header has %d columns — updated output header.",
                            fw_cols,
                        )
                continue

            # If we haven't seen the header yet, skip (boot text).
            if not header_seen:
                continue

            # ---- Validate the data row ----
            parts = raw.split(",")
            if len(parts) < 4:
                # Too few columns — skip but don't treat as a connection error.
                log.debug("Skipping row with < 4 columns: %s", raw)
                continue

            # Accept 4–6 columns (forwards compatibility).
            if len(parts) > 6:
                log.debug("Skipping row with > 6 columns: %s", raw)
                continue

            # Quick sanity: each field should parse as a number.
            try:
                tuple(float(p) for p in parts)
            except ValueError:
                log.debug("Skipping non-numeric row: %s", raw)
                continue

            # ---- Write to file ----
            # Always write the full row as received so extra columns (spo2, alert)
            # are preserved for richer data capture.
            fh.write(raw + "\n")
            row_count += 1
            consecutive_success += 1.0

            # Reset backoff after sustained successful reads
            if consecutive_success > RECONNECT_BACKOFF_RESET:
                backoff = RECONNECT_BACKOFF_INIT

            # ---- Periodic status line (non-logging, overwrites) ----
            now = time.monotonic()
            if now - last_status >= LIVE_STATUS_INTERVAL:
                elapsed = now - start_time
                sys.stderr.write(
                    f"\rRows: {row_count} | Elapsed: {format_elapsed(elapsed)}    "
                )
                sys.stderr.flush()
                last_status = now

    finally:
        sys.stderr.write("\n")  # move past the \r status line
        cleanup()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Senseware baseline data capture — records ESP32 telemetry CSV "
            "to data/raw/."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--port",
        type=str,
        default=None,
        help=(
            "Serial port (e.g. /dev/ttyUSB0). If omitted, the script will auto-detect."
        ),
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=DEFAULT_DURATION,
        help=(
            "Recording duration in minutes. "
            "Use 0 for unlimited (stop with Ctrl+C). Default: %(default)s."
        ),
    )
    args = parser.parse_args()

    # ---- Resolve port ----
    port = args.port
    if port is None:
        port = find_serial_port()
        if port is None:
            log.error("No serial port found. Connect the ESP32 and/or specify --port.")
            sys.exit(1)
        log.info("Auto-detected serial port: %s", port)

    log.info("Recording duration: %.1f minutes", args.duration)
    capture(port, args.duration)


if __name__ == "__main__":
    main()
