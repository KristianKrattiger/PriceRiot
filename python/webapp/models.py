from __future__ import annotations

from enum import Enum
from typing import Any, Dict, List, Optional

from pydantic import BaseModel, Field


class RunStatus(str, Enum):
    QUEUED = "queued"
    RUNNING = "running"
    COMPLETED = "completed"
    FAILED = "failed"


class RunConfig(BaseModel):
    """Configuration for a single simulation run."""

    store_yaml: str = Field(..., description="Path to store.yaml on disk")
    duration_seconds: float = Field(..., gt=0, description="Sim-time to run")
    spawn_interval: float = Field(
        5.0, gt=0, description="Seconds between customer spawns"
    )
    mission_probability: float = Field(
        0.5, ge=0, le=1, description="Fraction of mission-behavior customers"
    )
    random_seed: int = Field(
        0, ge=0, description="RNG seed; 0 = non-deterministic"
    )
    product_csv: Optional[str] = Field(
        default=None, description="Optional path to uploaded product CSV"
    )
    pos_data: Optional[str] = Field(
        default=None, description="Optional path to uploaded POS CSV"
    )


class RunResult(BaseModel):
    """Metadata and analytics outputs for a run."""

    run_id: str
    config: RunConfig
    status: RunStatus = RunStatus.QUEUED

    # Simple timestamps as ISO strings for now; can be refined later
    created_at: Optional[str] = None
    started_at: Optional[str] = None
    completed_at: Optional[str] = None
    failed_at: Optional[str] = None
    error_message: Optional[str] = None

    # CSV contents as text (materialized to temp files when downloaded)
    transactions_csv: Optional[str] = None
    customers_csv: Optional[str] = None

    # Analytics data (JSON-ready structures)
    heatmap_data: Optional[List[Dict[str, Any]]] = None
    queue_data: Optional[List[Dict[str, Any]]] = None

    # Placeholder for future graph / traffic data
    traffic_edges: Optional[List[Dict[str, Any]]] = None

    # Aggregated KPIs for dashboard and comparison
    kpis: Dict[str, Any] = Field(default_factory=dict)


class ComparisonRequest(BaseModel):
    run_ids: List[str]

