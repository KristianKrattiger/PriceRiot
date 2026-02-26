import argparse
import os
import sys

_script_dir = os.path.dirname(os.path.abspath(__file__))
if _script_dir not in sys.path:
    sys.path.insert(0, _script_dir)

from analytics import run_simulation_to_csv

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Run a PriceRiot simulation and export analytics-friendly CSVs."
    )
    parser.add_argument(
        "--store",
        dest="store_path",
        default="store.yaml",
        help="Path to store.yaml (relative to project root or absolute).",
    )
    parser.add_argument(
        "--duration",
        dest="duration_seconds",
        type=float,
        default=3600.0,
        help="Simulation time to run in seconds (default: 3600).",
    )
    parser.add_argument(
        "--spawn-interval",
        dest="spawn_interval",
        type=float,
        default=5.0,
        help="Seconds between customer spawns (default: 5.0).",
    )
    parser.add_argument(
        "--mission-probability",
        dest="mission_probability",
        type=float,
        default=0.5,
        help="Fraction of mission shoppers in [0,1] (default: 0.5).",
    )
    parser.add_argument(
        "--seed",
        dest="seed",
        type=int,
        default=0,
        help="Random seed (0 = non-deterministic).",
    )
    parser.add_argument(
        "--output-dir",
        dest="output_dir",
        default=None,
        help="Directory to write CSVs into (default: <project-root>/data/processed).",
    )

    args = parser.parse_args()

    result = run_simulation_to_csv(
        store_path=args.store_path,
        duration_seconds=args.duration_seconds,
        spawn_interval=args.spawn_interval,
        mission_probability=args.mission_probability,
        seed=args.seed,
        output_dir=args.output_dir,
    )

    output_dir = args.output_dir
    if output_dir is None:
        # Mirror default in analytics.core
        project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        output_dir = os.path.join(project_root, "data", "processed")

    print(f"Simulation finished. Duration: {args.duration_seconds:.1f}s")
    print(f"Transactions: {len(result.transactions)} rows")
    print(f"Customers:    {len(result.customers)} rows")
    print(f"CSV outputs written to: {os.path.abspath(output_dir)}")


if __name__ == "__main__":
    main()

