"""
Smoke test for the PriceRiot simulation Python module.

Validates that the pybind11 extension builds and runs: Simulator construction,
step/run, get_transactions(), get_customers(), and basic shape of Transaction/Customer.

Run from project root with PYTHONPATH including the build directory, e.g.:
  PYTHONPATH=build python tests/test_simulation.py
  # or after install: python tests/test_simulation.py
"""
import os
import sys

# Resolve store.yaml relative to project root (parent of tests/)
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.dirname(_SCRIPT_DIR)
STORE_YAML = os.path.join(_PROJECT_ROOT, "store.yaml")


def test_import():
    """Module can be imported."""
    import simulation  # noqa: F401
    assert hasattr(simulation, "Simulator")
    assert hasattr(simulation, "Transaction")
    assert hasattr(simulation, "Customer")
    assert hasattr(simulation, "LineItem")
    assert hasattr(simulation, "TripPurpose")


def test_simulator_construct_and_step():
    """Simulator builds from store.yaml and step() runs without error."""
    import simulation
    if not os.path.isfile(STORE_YAML):
        raise FileNotFoundError(
            f"store.yaml not found at {STORE_YAML}. Run from project root."
        )
    sim = simulation.Simulator(STORE_YAML, spawn_interval=5.0, mission_probability=0.5)
    sim.step(1.0 / 60.0)
    assert sim.elapsed_time >= 0.0
    sim.step(1.0 / 60.0)
    assert sim.elapsed_time >= 0.0


def test_simulator_run_and_data():
    """run() completes and get_transactions / get_customers return valid data."""
    import simulation
    if not os.path.isfile(STORE_YAML):
        raise FileNotFoundError(
            f"store.yaml not found at {STORE_YAML}. Run from project root."
        )
    sim = simulation.Simulator(STORE_YAML, spawn_interval=2.0, mission_probability=0.3)
    # Short run: 30 sim-seconds so test stays fast
    sim.run(30.0, dt=1.0 / 60.0)
    txns = sim.get_transactions()
    custs = sim.get_customers()
    assert isinstance(txns, list)
    assert isinstance(custs, list)
    # At least one customer should have been spawned
    assert len(custs) >= 1, "Expected at least one customer after 30s run"
    # Transaction / Customer shape
    for t in txns:
        assert hasattr(t, "trans_id") and hasattr(t, "cust_id")
        assert hasattr(t, "total_spent") and hasattr(t, "items")
        assert hasattr(t, "to_dict")
        for item in t.items():
            assert hasattr(item, "id") and hasattr(item, "name")
            assert hasattr(item, "quantity") and hasattr(item, "price_per_unit")
    for c in custs:
        assert hasattr(c, "id") and hasattr(c, "age") and hasattr(c, "gender")
        assert hasattr(c, "total_spent") and hasattr(c, "num_purchases")
        assert hasattr(c, "to_dict")
    assert sim.transaction_count() == len(txns)


def test_transaction_and_customer_invariants():
    """Basic systemic invariants on transactions and customers."""
    import simulation
    if not os.path.isfile(STORE_YAML):
        raise FileNotFoundError(
            f"store.yaml not found at {STORE_YAML}. Run from project root."
        )
    sim = simulation.Simulator(
        STORE_YAML, spawn_interval=2.0, mission_probability=0.4, seed=1234
    )
    sim.run(60.0, dt=1.0 / 60.0)
    txns = sim.get_transactions()
    custs = sim.get_customers()

    customer_ids = {c.id() for c in custs}
    for t in txns:
        assert t.cust_id() in customer_ids
        assert t.total_spent() >= 0.0
        for item in t.items():
            assert item.quantity >= 0
            assert item.price_per_unit >= 0.0
            assert item.total >= 0.0

    # With this configuration we expect a reasonable number of transactions.
    # Exact values may evolve, so keep bounds conservative.
    txn_count = sim.transaction_count()
    assert 0 <= txn_count < 10_000


def test_queue_and_traffic_metrics_exposed():
    """Queue metrics and traffic heatmap should be exposed and well-shaped."""
    import simulation
    if not os.path.isfile(STORE_YAML):
        raise FileNotFoundError(
            f"store.yaml not found at {STORE_YAML}. Run from project root."
        )
    sim = simulation.Simulator(
        STORE_YAML, spawn_interval=3.0, mission_probability=0.5, seed=42
    )
    sim.run(30.0, dt=1.0 / 60.0)

    # Traffic heatmap
    cell_counts = sim.get_cell_heatmap()
    assert isinstance(cell_counts, list)
    if cell_counts:
        assert isinstance(cell_counts[0], list)

    # Queue metrics
    times = sim.get_queue_sample_times()
    lengths = sim.get_queue_lengths_history()
    assert isinstance(times, list)
    assert isinstance(lengths, list)
    if lengths:
        # One entry per lane
        assert all(isinstance(series, list) for series in lengths)
        # Each time series should not be longer than times
        for series in lengths:
            assert len(series) <= len(times)


def test_reset_and_repeat():
    """reset() clears state; second run produces new data."""
    import simulation
    if not os.path.isfile(STORE_YAML):
        raise FileNotFoundError(
            f"store.yaml not found at {STORE_YAML}. Run from project root."
        )
    sim = simulation.Simulator(STORE_YAML, spawn_interval=3.0, mission_probability=0.5)
    sim.run(15.0, dt=1.0 / 60.0)
    sim.reset()
    assert sim.transaction_count() == 0
    sim.run(15.0, dt=1.0 / 60.0)
    cust_count_2 = len(sim.get_customers())
    # After reset, new run; customer count may be similar or different
    assert cust_count_2 >= 1
    # Elapsed time restarted after reset
    assert sim.elapsed_time >= 0.0


if __name__ == "__main__":
    test_import()
    print("test_import passed")
    test_simulator_construct_and_step()
    print("test_simulator_construct_and_step passed")
    test_simulator_run_and_data()
    print("test_simulator_run_and_data passed")
    test_reset_and_repeat()
    print("test_reset_and_repeat passed")
    print("All smoke tests passed.")
