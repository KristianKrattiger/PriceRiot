from .temporal import (
    SimDay,
    SimulationRangeConfig,
    SpawnPeriod,
    TemporalScheduler,
    TemporalSimResult,
    run_temporal_simulation,
)
from .orchestrator import (
    AggregateSummary,
    RunOrchestrator,
    SimRun,
    SimRunOutput,
)

__all__ = [
    # core (lazy — see __getattr__)
    "SimulationResult",
    "run_simulation",
    "run_simulation_to_csv",
    "transactions_to_frame",
    "customers_to_frame",
    "cell_heatmap_to_frame",
    "queue_metrics_to_frame",
    "get_project_root",
    # temporal
    "SimulationRangeConfig",
    "SimDay",
    "SpawnPeriod",
    "TemporalScheduler",
    "TemporalSimResult",
    "run_temporal_simulation",
    # orchestrator
    "RunOrchestrator",
    "SimRun",
    "SimRunOutput",
    "AggregateSummary",
]

_CORE_NAMES = frozenset(
    {
        "SimulationResult",
        "run_simulation",
        "run_simulation_to_csv",
        "transactions_to_frame",
        "customers_to_frame",
        "cell_heatmap_to_frame",
        "queue_metrics_to_frame",
        "get_project_root",
    }
)


def __getattr__(name: str):
    if name in _CORE_NAMES:
        from . import core

        return getattr(core, name)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
