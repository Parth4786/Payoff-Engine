from __future__ import annotations

from dataclasses import replace

from core.models import DepthSnapshot


def assemble_snapshot(*, prev: DepthSnapshot | None, update: DepthSnapshot) -> DepthSnapshot:
    """Assemble a snapshot update into a continuous book state.

    Current internal model requires both bids and asks to be present (>=1 level) for validity.
    Therefore, true one-sided partial updates cannot be represented as `DepthSnapshot`.

    We still implement a conservative assembler hook so downstream logic stays stable as
    data sources evolve:
    - Mark `is_partial=True` when depth has fewer than 5 levels on either side.
    - Otherwise, pass through.

    Future extension:
    - If we introduce a PartialDepthUpdate type, this function becomes the merge point.
    """

    # Treat "less than top-5" as partial.
    is_partial = update.is_partial or (len(update.bids) < 5) or (len(update.asks) < 5)

    if update.is_partial == is_partial:
        return update

    return replace(update, is_partial=is_partial)
