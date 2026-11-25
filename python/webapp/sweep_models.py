"""
sweep_models.py
===============
Pydantic schemas for the POST /api/sweeps parameter-sweep endpoint.
"""
from __future__ import annotations

from enum import Enum
from typing import List, Optional

from pydantic import BaseModel, Field


class SweepStatus(str, Enum):
    QUEUED = "queued"
    RUNNING = "running"
    COMPLETE = "complete"
    FAILED = "failed"


class SweepConfig(BaseModel):
    store_yaml: str = Field(..., description="Store layout YAML filename or path")
    duration_seconds: float = Field(300.0, gt=0, description="Sim-seconds per grid point")
    spawn_intervals: List[float] = Field(default_factory=lambda: [5.0], min_length=1)
    mission_probabilities: List[float] = Field(default_factory=lambda: [0.5], min_length=1)
    num_stockers: List[int] = Field(default_factory=lambda: [2], min_length=1)
    num_cashiers: List[int] = Field(default_factory=lambda: [1], min_length=1)
    price_multipliers: List[float] = Field(default_factory=lambda: [1.0], min_length=1)
    seed: int = Field(42, ge=0)
    dt: float = Field(1.0 / 60.0, gt=0)
    max_threads: int = Field(4, ge=1)
    auto_stock_tasks: bool = True
    auto_register_tasks: bool = True
    label: Optional[str] = None

    def grid_size(self) -> int:
        return (
            len(self.spawn_intervals)
            * len(self.mission_probabilities)
            * len(self.num_stockers)
            * len(self.num_cashiers)
            * len(self.price_multipliers)
        )


class SweepPointResult(BaseModel):
    point_index: int
    spawn_interval: float
    mission_probability: float
    num_stockers: int
    num_cashiers: int
    price_multiplier: float
    seed: int
    total_customers: int
    total_transactions: int
    total_units: int
    total_revenue: float
    avg_basket_value: float
    avg_items_per_basket: float
    mean_queue_length: float
    max_queue_length: int
    p95_queue_length: float
    elapsed_wall_seconds: float


class SweepResult(BaseModel):
    sweep_id: str
    config: SweepConfig
    status: SweepStatus = SweepStatus.QUEUED
    created_at: str
    completed_at: Optional[str] = None
    elapsed_seconds: Optional[float] = None
    total_points: int = 0
    results: List[SweepPointResult] = Field(default_factory=list)
    results_csv: Optional[str] = None
    error_message: Optional[str] = None
