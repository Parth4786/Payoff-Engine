from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class ExecutionHint(str, Enum):
    PASSIVE = "PASSIVE"
    AGGRESSIVE = "AGGRESSIVE"
    WAIT = "WAIT"


@dataclass(frozen=True, slots=True)
class ExecutionDecision:
    hint: ExecutionHint
    reasons: tuple[str, ...]
