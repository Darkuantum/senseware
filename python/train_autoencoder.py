#!/usr/bin/env python3
"""
train_autoencoder.py — Senseware Phase 3: Autoencoder Training & Export

Trains a lightweight autoencoder on baseline (calm-state) sensor data,
calibrates an anomaly detection threshold, and exports the model as
a TFLite C-byte array header for ESP32 deployment.

Input CSV columns (from capture_baseline.py):
    millis, heart_rate, emg_envelope, motion_magnitude

Usage:
    python python/train_autoencoder.py --data data/raw/ --epochs 50 --output models/
    python python/train_autoencoder.py --data data/raw/synthetic_baseline.csv
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
import pandas as pd

# TensorFlow may emit harmless CUDA warnings; suppress them for clean output.
import os

os.environ["TF_CPP_MIN_LOG_LEVEL"] = "2"

import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers

# ---------------------------------------------------------------------------
# Project paths
# ---------------------------------------------------------------------------

PROJECT_ROOT = Path(__file__).resolve().parent.parent
SENSEWARE_CODE_DIR = PROJECT_ROOT / "senseware_code"
FEATURE_COLS = ["heart_rate", "emg_envelope", "motion_magnitude"]


# ---------------------------------------------------------------------------
# Data loading
# ---------------------------------------------------------------------------


def load_data(data_path: Path) -> pd.DataFrame:
    """Load a single CSV or concatenate all CSVs in a directory."""
    if data_path.is_dir():
        csv_files = sorted(data_path.glob("*.csv"))
        if not csv_files:
            print(f"ERROR: No .csv files found in {data_path}", file=sys.stderr)
            sys.exit(1)
        print(f"Loading {len(csv_files)} CSV file(s) from {data_path}/")
        dfs = []
        for f in csv_files:
            df = pd.read_csv(f)
            dfs.append(df)
            print(f"  {f.name}: {len(df)} rows")
        data = pd.concat(dfs, ignore_index=True)
    elif data_path.is_file():
        print(f"Loading {data_path}")
        data = pd.read_csv(data_path)
    else:
        print(f"ERROR: {data_path} does not exist", file=sys.stderr)
        sys.exit(1)

    # Validate required columns
    missing = [c for c in FEATURE_COLS if c not in data.columns]
    if missing:
        print(f"ERROR: Missing columns: {missing}", file=sys.stderr)
        print(f"  Available columns: {list(data.columns)}", file=sys.stderr)
        sys.exit(1)

    # Check for NaN/Inf
    nan_count = data.isna().sum().sum()
    inf_count = np.isinf(data.select_dtypes(include=[np.number])).sum().sum()
    if nan_count > 0 or inf_count > 0:
        print(
            f"WARNING: Data contains {nan_count} NaN and {inf_count} Inf values. Dropping affected rows."
        )
        data = data.replace([np.inf, -np.inf], np.nan).dropna()
        print(f"  Rows after cleanup: {len(data)}")

    return data[FEATURE_COLS]


# ---------------------------------------------------------------------------
# Preprocessing
# ---------------------------------------------------------------------------


def normalize(
    data: pd.DataFrame, fit: bool, params: dict | None = None
) -> tuple[np.ndarray, dict]:
    """Z-normalize features. If fit=True, compute mean/std from data.

    Returns (normalized_array, params_dict).
    params_dict = {col: {"mean": float, "std": float}, ...}
    """
    arr = data.values.astype(np.float32)

    if fit:
        params = {}
        for i, col in enumerate(FEATURE_COLS):
            col_mean = float(np.mean(arr[:, i]))
            col_std = float(np.std(arr[:, i]))
            if col_std < 1e-8:
                col_std = 1.0  # Avoid division by zero for constant features
            params[col] = {"mean": col_mean, "std": col_std}

    assert params is not None
    normed = np.empty_like(arr)
    for i, col in enumerate(FEATURE_COLS):
        m = params[col]["mean"]
        s = params[col]["std"]
        normed[:, i] = (arr[:, i] - m) / s

    return normed, params


# ---------------------------------------------------------------------------
# Model
# ---------------------------------------------------------------------------


def build_autoencoder() -> keras.Model:
    """Build the lightweight autoencoder: 3 → 16 → 8 → 16 → 3."""
    model = keras.Sequential(
        [
            layers.Input(shape=(3,)),
            layers.Dense(16, activation="relu", name="encoder_1"),
            layers.Dense(8, activation="relu", name="latent"),
            layers.Dense(16, activation="relu", name="decoder_1"),
            layers.Dense(3, activation="linear", name="output"),
        ],
        name="SensewareAutoencoder",
    )
    return model


# ---------------------------------------------------------------------------
# Anomaly injection
# ---------------------------------------------------------------------------


def inject_anomalies(
    data: np.ndarray, fraction: float = 0.3, seed: int = 99
) -> np.ndarray:
    """Return a copy of *data* with *fraction* of rows corrupted to simulate
    stress / sensor spikes.

    *data* should be **raw** (un-normalized) sensor values.

    Corruption strategy per anomalous row:
      - Multiply each feature by a random factor in [2, 10].
      - Add Gaussian noise with std = 5.
    """
    rng = np.random.default_rng(seed)
    corrupted = data.copy()
    n = len(data)
    n_anomalous = int(n * fraction)
    indices = rng.choice(n, size=n_anomalous, replace=False)

    for idx in indices:
        # Random multiplier per feature
        multiplier = rng.uniform(2.0, 10.0, size=data.shape[1])
        corrupted[idx] = corrupted[idx] * multiplier

        # Add Gaussian noise
        corrupted[idx] += rng.normal(0, 5.0, size=data.shape[1])

    # Clip to reasonable sensor ranges (raw space)
    corrupted[:, 0] = np.clip(corrupted[:, 0], 40, 250)  # heart_rate
    corrupted[:, 1] = np.clip(corrupted[:, 1], 0.0, 10.0)  # emg_envelope
    corrupted[:, 2] = np.clip(corrupted[:, 2], 0.0, 20.0)  # motion_magnitude

    return corrupted


# ---------------------------------------------------------------------------
# Threshold calibration
# ---------------------------------------------------------------------------


def calibrate_threshold(model: keras.Model, train_norm: np.ndarray) -> float:
    """Compute per-sample MSE on training data; return mean + 3×std."""
    reconstructions = model.predict(train_norm, verbose=0)
    mse_per_sample = np.mean((train_norm - reconstructions) ** 2, axis=1)
    threshold = float(np.mean(mse_per_sample) + 3.0 * np.std(mse_per_sample))
    return threshold


def validate_threshold(
    model: keras.Model,
    threshold: float,
    norm_params: dict,
    test_norm: np.ndarray,
    test_raw: np.ndarray,
) -> None:
    """Inject anomalies into the *test* set (out-of-sample) and report
    detection rates.  This avoids the circular validation of using
    training data for both calibration and evaluation."""
    # Inject anomalies in raw space, then normalize
    anomalous_raw = inject_anomalies(test_raw, fraction=0.3, seed=99)
    anomalous_norm, _ = normalize(
        pd.DataFrame(anomalous_raw, columns=FEATURE_COLS),
        fit=False,
        params=norm_params,
    )

    recon_normal = model.predict(test_norm, verbose=0)
    mse_normal = np.mean((test_norm - recon_normal) ** 2, axis=1)

    recon_anomalous = model.predict(anomalous_norm, verbose=0)
    mse_anomalous = np.mean((anomalous_norm - recon_anomalous) ** 2, axis=1)

    false_positive_rate = np.mean(mse_normal > threshold) * 100
    detection_rate = np.mean(mse_anomalous > threshold) * 100

    print()
    print("=" * 60)
    print("THRESHOLD VALIDATION (out-of-sample, test set)")
    print("=" * 60)
    print(f"  Threshold (mean + 3σ):  {threshold:.6f}")
    print(
        f"  Normal MSE:             mean={np.mean(mse_normal):.6f}  std={np.std(mse_normal):.6f}"
    )
    print(
        f"  Anomalous MSE:          mean={np.mean(mse_anomalous):.6f}  std={np.std(mse_anomalous):.6f}"
    )
    print(
        f"  Normal samples above threshold (false positives):  {false_positive_rate:.1f}%"
    )
    print(f"  Anomalous samples detected (true positives):       {detection_rate:.1f}%")
    print("=" * 60)


# ---------------------------------------------------------------------------
# Export
# ---------------------------------------------------------------------------


def export_tflite(model: keras.Model, output_dir: Path) -> Path:
    """Convert Keras model to TFLite and save to output_dir."""
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    # Default optimization keeps the model small and fast on ESP32
    tflite_model = converter.convert()

    tflite_path = output_dir / "senseware_autoencoder.tflite"
    tflite_path.write_bytes(tflite_model)
    size_kb = len(tflite_model) / 1024
    print(f"\nTFLite model saved: {tflite_path}  ({size_kb:.1f} KB)")

    if size_kb > 10:
        print(
            f"  WARNING: Model is {size_kb:.1f} KB — may be tight on ESP32 flash!",
            file=sys.stderr,
        )
    else:
        print(f"  ✓ Model size OK for ESP32 (under 10 KB)")

    return tflite_path


def _xxd_fallback(data: bytes) -> str:
    """Pure-Python equivalent of `xxd -i` for generating C byte arrays."""
    lines = []
    var_name = "senseware_model_tflite"

    # Declare the array
    lines.append(f"unsigned char {var_name}[] = {{")

    # Format bytes: 12 per line, "0xHH" each
    for i in range(0, len(data), 12):
        chunk = data[i : i + 12]
        hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
        comma = "," if i + 12 < len(data) else ""
        lines.append(f"  {hex_vals}{comma}")

    lines.append("};")
    lines.append(f"unsigned int {var_name}_len = {len(data)};")
    return "\n".join(lines)


def export_c_header(tflite_path: Path, header_path: Path) -> None:
    """Generate a C header from the TFLite file (prefers xxd, falls back to
    pure-Python)."""
    model_bytes = tflite_path.read_bytes()

    # Try xxd first; fall back to pure Python if unavailable.
    try:
        result = subprocess.run(
            ["xxd", "-i", str(tflite_path)],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise FileNotFoundError
        raw_lines = result.stdout.strip().split("\n")
        c_body = "\n".join(
            line.replace(tflite_path.as_posix(), "senseware_model_tflite")
            for line in raw_lines
        )
    except FileNotFoundError:
        print("  xxd not found — using pure-Python fallback")
        c_body = _xxd_fallback(model_bytes)

    header_content = (
        "/* Auto-generated by train_autoencoder.py — do not edit manually. */\n"
        "#ifndef SENSEWARE_MODEL_DATA_H\n"
        "#define SENSEWARE_MODEL_DATA_H\n\n"
        + c_body
        + "\n\n#endif /* SENSEWARE_MODEL_DATA_H */\n"
    )

    header_path.parent.mkdir(parents=True, exist_ok=True)
    header_path.write_text(header_content)
    size_kb = len(model_bytes) / 1024
    print(f"C header saved: {header_path}  ({size_kb:.1f} KB of model data)")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Train the Senseware autoencoder and export for ESP32.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--data",
        type=Path,
        required=True,
        help="Path to a single baseline CSV or a directory of CSVs.",
    )
    parser.add_argument(
        "--epochs",
        type=int,
        default=50,
        help="Maximum training epochs. Default: 50",
    )
    parser.add_argument(
        "--batch-size",
        type=int,
        default=32,
        help="Training batch size. Default: 32",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=PROJECT_ROOT / "models",
        help="Directory to save model artifacts. Default: models/",
    )
    args = parser.parse_args()

    output_dir: Path = args.output
    output_dir.mkdir(parents=True, exist_ok=True)

    header_path = SENSEWARE_CODE_DIR / "model_data.h"

    # ------------------------------------------------------------------
    # 1. Load data
    # ------------------------------------------------------------------
    print("\n" + "=" * 60)
    print("STEP 1: Loading data")
    print("=" * 60)
    data = load_data(args.data)
    print(f"  Total samples: {len(data)}")
    print(f"  Columns: {list(data.columns)}")

    # ------------------------------------------------------------------
    # 2. Train/test split (80/20)
    # ------------------------------------------------------------------
    print("\n" + "=" * 60)
    print("STEP 2: Train/test split (80/20)")
    print("=" * 60)
    # Use a fixed seed for reproducibility
    train_df = data.sample(frac=0.8, random_state=42)
    test_df = data.drop(train_df.index)
    print(f"  Training samples: {len(train_df)}")
    print(f"  Test samples:     {len(test_df)}")

    # ------------------------------------------------------------------
    # 3. Normalize (fit on training set only!)
    # ------------------------------------------------------------------
    print("\n" + "=" * 60)
    print("STEP 3: Normalization (fit on training set)")
    print("=" * 60)
    train_norm, norm_params = normalize(train_df, fit=True)
    test_norm, _ = normalize(test_df, fit=False, params=norm_params)

    for col in FEATURE_COLS:
        p = norm_params[col]
        print(f"  {col:20s}  mean={p['mean']:10.4f}  std={p['std']:10.4f}")

    # Save normalization params — firmware needs these!
    norm_path = output_dir / "normalization.json"
    norm_path.write_text(json.dumps(norm_params, indent=2) + "\n")
    print(f"\n  Normalization params saved: {norm_path}")

    # ------------------------------------------------------------------
    # 4. Build model
    # ------------------------------------------------------------------
    print("\n" + "=" * 60)
    print("STEP 4: Building autoencoder")
    print("=" * 60)
    model = build_autoencoder()
    model.compile(optimizer=keras.optimizers.Adam(learning_rate=0.001), loss="mse")
    model.summary()

    # ------------------------------------------------------------------
    # 5. Train
    # ------------------------------------------------------------------
    print("\n" + "=" * 60)
    print("STEP 5: Training")
    print("=" * 60)

    early_stop = keras.callbacks.EarlyStopping(
        monitor="val_loss",
        patience=5,
        restore_best_weights=True,
        verbose=1,
    )

    history = model.fit(
        train_norm,
        train_norm,  # Autoencoder target = input
        validation_data=(test_norm, test_norm),
        epochs=args.epochs,
        batch_size=args.batch_size,
        callbacks=[early_stop],
        verbose=1,
    )

    final_val_loss = history.history["val_loss"][-1]
    print(f"\n  Final val_loss: {final_val_loss:.6f}")

    # ------------------------------------------------------------------
    # 6. Threshold calibration
    # ------------------------------------------------------------------
    print("\n" + "=" * 60)
    print("STEP 6: Threshold calibration")
    print("=" * 60)
    threshold = calibrate_threshold(model, train_norm)

    # Save threshold
    threshold_path = output_dir / "anomaly_threshold.json"
    threshold_path.write_text(
        json.dumps({"threshold": threshold, "method": "mean_plus_3std"}, indent=2)
        + "\n"
    )
    print(f"  Threshold saved: {threshold_path}")
    print(f"  Anomaly threshold (MSE): {threshold:.6f}")

    # Validate with injected anomalies (out-of-sample: test set)
    validate_threshold(
        model, threshold, norm_params, test_norm, test_df.values.astype(np.float32)
    )

    # ------------------------------------------------------------------
    # 7. Export TFLite
    # ------------------------------------------------------------------
    print("\n" + "=" * 60)
    print("STEP 7: Exporting model")
    print("=" * 60)
    tflite_path = export_tflite(model, output_dir)

    # ------------------------------------------------------------------
    # 8. Export C header
    # ------------------------------------------------------------------
    print()
    export_c_header(tflite_path, header_path)

    # ------------------------------------------------------------------
    # 8b. Verify TFLite accuracy
    # ------------------------------------------------------------------
    print()
    print("Verifying TFLite model accuracy against Keras model…")
    interpreter = tf.lite.Interpreter(model_path=str(tflite_path))
    interpreter.allocate_tensors()
    input_detail = interpreter.get_input_details()
    output_detail = interpreter.get_output_details()

    # Test with a few samples
    test_samples = test_norm[:10]
    for i, sample in enumerate(test_samples):
        interpreter.set_tensor(
            input_detail[0]["index"], sample.reshape(1, -1).astype(np.float32)
        )
        interpreter.invoke()
        tflite_output = interpreter.get_tensor(output_detail[0]["index"])[0]
        keras_output = model.predict(sample.reshape(1, -1), verbose=0)[0]
        if not np.allclose(tflite_output, keras_output, atol=1e-5):
            print(f"WARNING: TFLite output diverges from Keras for sample {i}")
            print(f"  Keras:  {keras_output}")
            print(f"  TFLite: {tflite_output}")
            break
    else:
        print(
            "  TFLite model accuracy verified: all outputs match Keras model (atol=1e-5)"
        )

    # ------------------------------------------------------------------
    # Summary
    # ------------------------------------------------------------------
    print("\n" + "=" * 60)
    print("PIPELINE COMPLETE — saved artifacts")
    print("=" * 60)
    print(f"  {norm_path}         — normalization params (mean, std per feature)")
    print(f"  {threshold_path}    — MSE anomaly threshold")
    print(f"  {tflite_path}       — TFLite model")
    print(f"  {header_path}       — C header for Arduino/ESP32")
    print()


if __name__ == "__main__":
    main()
