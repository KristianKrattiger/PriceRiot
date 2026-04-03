"""
sim_models.py
=============
Pydantic models for the simulation control-plane (temporal / multi-run jobs).
These are distinct from RunConfig / RunResult in models.py, which cover
single-session headless runs.
"""
from __future__ import annotations

from enum import Enum
from typing import Any, Dict, List, Optional

from pydantic import BaseModel, Field


class SimJobStatus(str, Enum):
    QUEUED  = "queued"
    RUNNING = "running"
    COMPLETE = "complete"
    FAILED  = "failed"


class DateConfig(BaseModel):
    """Preset-specific date parameters.  Only the fields relevant to the
    active preset need to be populated."""
    # contiguous_range
    start: Optional[str] = None   # ISO date string
    end:   Optional[str] = None
    # single_day_n_runs
    date: Optional[str] = None
    # weekday_repeat
    days_of_week: Optional[List[int]] = None  # 0=Mon … 6=Sun
    weeks:        Optional[int]       = None
    anchor:       Optional[str]       = None  # ISO date string
    # custom_days
    dates: Optional[List[str]] = None  # list of ISO date strings


class TimeWindow(BaseModel):
    open:  str = "09:00"   # "HH:MM" 24-hour
    close: str = "21:00"


class SimJobConfig(BaseModel):
    pos_dataset: str           # POS CSV filename (relative to uploads/raw dir)
    store_yaml:  str           # YAML filename resolved from pos_dataset
    preset: str                # contiguous_range | single_day_n_runs | weekday_repeat | custom_days
    date_config: DateConfig
    time_window: Optional[TimeWindow] = None   # None → use store hours from YAML
    runs:        int = Field(4, ge=1, description="Number of independent statistical runs")
    max_threads: int = Field(4, ge=1, description="Thread concurrency cap")
    label:       Optional[str] = None
    # Simulation parameters
    seed:                int   = 42
    mission_probability: float = 0.5
    spawn_interval:      float = 5.0
    num_stockers:        int   = 2
    num_cashiers:        int   = 1
    auto_stock_tasks:    bool  = True
    auto_register_tasks: bool  = True
    product_csv:         Optional[str] = None


class PerRunSummary(BaseModel):
    run_index:         int
    transaction_count: int
    customer_count:    int
    total_revenue:     float
    peak_period:       str
    output_path:       str
    duration_seconds:  float
    status:            str = "complete"
    error:             Optional[str] = None
    day_summaries:     List[Dict[str, Any]] = Field(default_factory=list)
    # Rich KPIs (computed during simulation)
    kpis:             Optional[Dict[str, Any]] = None
    sku_breakdown:    List[Dict[str, Any]]     = Field(default_factory=list)
    traffic_edges:    List[Dict[str, Any]]     = Field(default_factory=list)
    queue_data:       List[Dict[str, Any]]     = Field(default_factory=list)
    worker_timeseries: List[Dict[str, Any]]    = Field(default_factory=list)


class SimJobResult(BaseModel):
    sim_id:     str
    config:     SimJobConfig
    status:     SimJobStatus = SimJobStatus.QUEUED
    created_at: str
    started_at:   Optional[str]   = None
    completed_at: Optional[str]   = None
    elapsed_seconds: Optional[float] = None
    progress: Dict[str, int] = Field(
        default_factory=lambda: {"completed_runs": 0, "total_runs": 0}
    )
    aggregate:     Optional[Dict[str, Any]] = None
    per_runs:      List[PerRunSummary]      = Field(default_factory=list)
    error_message: Optional[str]            = None
