"""
CLI entry point: run cleaner -> param_extractor -> sku_mapper and print summary.
Usage: python -m python.ingestion.run_ingestion --input data/raw/your_pos_file.csv --output data/processed/
"""
import argparse
import os
import sys

# Ensure project root and python/ on path for analytics import
_script_dir = os.path.dirname(os.path.abspath(__file__))
_python_dir = os.path.dirname(_script_dir)
_root = os.path.dirname(_python_dir)
if _root not in sys.path:
    sys.path.insert(0, _root)
if _python_dir not in sys.path:
    sys.path.insert(0, _python_dir)

from ingestion.cleaner import load_and_clean
from ingestion.param_extractor import extract_params
from ingestion.sku_mapper import build_sku_mapping


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Run POS ingestion pipeline: clean CSV, extract sim params, build SKU mapping.",
    )
    parser.add_argument(
        "--input",
        required=True,
        help="Path to raw POS CSV (e.g. data/raw/your_pos_file.csv).",
    )
    parser.add_argument(
        "--output",
        default="data/processed",
        help="Output directory for pos_cleaned.csv, sim_params.json, sku_mapping.json (default: data/processed).",
    )
    args = parser.parse_args()

    output_dir = args.output
    if not os.path.isabs(output_dir):
        output_dir = os.path.normpath(os.path.join(_root, output_dir))
    os.makedirs(output_dir, exist_ok=True)

    # Step 1: Clean
    print("Step 1: Cleaning raw POS data...")
    cleaned = load_and_clean(args.input, output_dir=output_dir)
    cleaned_path = os.path.join(output_dir, "pos_cleaned.csv")
    print(f"  -> {len(cleaned)} line items -> {cleaned_path}")

    # Step 2: Extract params
    print("Step 2: Extracting simulation parameters...")
    params = extract_params(cleaned_path, output_dir=output_dir)
    print(f"  -> sim_params.json written")

    # Step 3: SKU mapping
    print("Step 3: Building SKU mapping...")
    top_skus = params.get("top_skus", [])
    mapping, unmapped = build_sku_mapping(top_skus, output_dir=output_dir)
    print(f"  -> {len(mapping)} mapped, {len(unmapped)} unmapped/flagged")

    # Summary
    print("\n--- Derived parameters summary ---")
    print(f"  spawn_interval_seconds: {params.get('spawn_interval_seconds')}")
    print(f"  mean_basket_size:        {params.get('mean_basket_size')}")
    print(f"  mission_probability:     {params.get('mission_probability')}")
    print(f"  price_sensitivity:       {params.get('price_sensitivity')}")
    print(f"  mean_service_time_s:     {params.get('mean_service_time_s')}")
    top5 = (params.get("top_skus") or [])[:5]
    print(f"  top_skus (first 5):      {top5}")
    print(f"  SKU mapping coverage:    {len(mapping)} / {len(top_skus) if top_skus else 0} products mapped")
    if unmapped:
        print(f"  Unmapped (review):        {unmapped[:5]}{'...' if len(unmapped) > 5 else ''}")


if __name__ == "__main__":
    main()
