#!/usr/bin/env python3
"""
generate_synthetic_data.py — Senseware Phase 3 helper

Generates synthetic "calm state" baseline data for testing the training
pipeline before real hardware data is available.

Usage:
    python python/generate_synthetic_data.py [--samples 2000] [--output data/raw/synthetic_baseline.csv]
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import pandas as pd

PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT = PROJECT_ROOT / "data" / "raw" / "synthetic_baseline.csv"
DEFAULT_SAMPLES = 2000


def generate_baseline(n: int, seed: int = 42) -> pd.DataFrame:
    """Return a DataFrame of n synthetic calm-state sensor readings.

    Columns match the serial output format from capture_baseline.py:
        millis, heart_rate, emg_envelope, motion_magnitude
    """
    rng = np.random.default_rng(seed)

    # Simulated monotonic timestamps (1 reading per second → ms steps of 1000)
    millis = np.arange(n, dtype=np.float64) * 1000.0

    heart_rate = rng.normal(loc=72.0, scale=5.0, size=n)
    emg_envelope = rng.normal(loc=0.3, scale=0.1, size=n)
    motion_magnitude = rng.normal(loc=1.0, scale=0.15, size=n)

    # Add temporal autocorrelation using an AR(1) process
    for col_name, col_arr, col_mean, col_std in [
        ("heart_rate", heart_rate, 72.0, 5.0),
        ("emg_envelope", emg_envelope, 0.3, 0.1),
        ("motion_magnitude", motion_magnitude, 1.0, 0.15),
    ]:
        alpha = 0.95  # autocorrelation coefficient
        noise_std = col_std * np.sqrt(1 - alpha**2)
        for i in range(1, n):
            col_arr[i] = (
                alpha * col_arr[i - 1]
                + (1 - alpha) * col_mean
                + rng.normal(0, noise_std)
            )

    # Clip to physically plausible ranges
    heart_rate = np.clip(heart_rate, 40, 120)
    emg_envelope = np.clip(emg_envelope, 0.0, 3.0)
    motion_magnitude = np.clip(motion_magnitude, 0.0, 5.0)

    return pd.DataFrame(
        {
            "millis": millis,
            "heart_rate": heart_rate,
            "emg_envelope": emg_envelope,
            "motion_magnitude": motion_magnitude,
        }
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate synthetic calm-state baseline CSV for pipeline testing."
    )
    parser.add_argument(
        "--samples",
        type=int,
        default=DEFAULT_SAMPLES,
        help=f"Number of samples to generate. Default: {DEFAULT_SAMPLES}",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"Output CSV path. Default: {DEFAULT_OUTPUT}",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=42,
        help="RNG seed for reproducibility. Default: 42",
    )
    args = parser.parse_args()

    # Ensure output directory exists
    args.output.parent.mkdir(parents=True, exist_ok=True)

    df = generate_baseline(n=args.samples, seed=args.seed)
    df.to_csv(args.output, index=False)

    print(f"Generated {len(df)} synthetic baseline samples → {args.output}")
    print(
        f"  heart_rate:       mean={df['heart_rate'].mean():.1f}  std={df['heart_rate'].std():.1f}"
    )
    print(
        f"  emg_envelope:     mean={df['emg_envelope'].mean():.3f}  std={df['emg_envelope'].std():.3f}"
    )
    print(
        f"  motion_magnitude: mean={df['motion_magnitude'].mean():.3f}  std={df['motion_magnitude'].std():.3f}"
    )


if __name__ == "__main__":
    main()
