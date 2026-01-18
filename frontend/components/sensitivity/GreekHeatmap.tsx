'use client';

import { useEffect, useRef, useMemo } from 'react';
import { cn, formatNumber } from '@/lib/utils';

interface HeatmapCell {
  spot: number;
  iv: number;
  value: number;
}

interface Props {
  greek: string;
  data?: HeatmapCell[];
  isLoading: boolean;
}

export function GreekHeatmap({ greek, data, isLoading }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const tooltipRef = useRef<HTMLDivElement>(null);

  // Generate mock data if none provided
  const gridData = useMemo(() => {
    if (data) return data;

    // Generate a 20x20 grid of mock data
    const cells: HeatmapCell[] = [];
    const baseSpot = 26300;
    const spotRange = 1000;
    const ivRange = 0.2;

    for (let i = 0; i < 20; i++) {
      for (let j = 0; j < 20; j++) {
        const spot = baseSpot - spotRange + (i / 19) * spotRange * 2;
        const iv = 0.1 + (j / 19) * ivRange;
        
        // Generate realistic Greek values
        let value = 0;
        const moneyness = (spot - baseSpot) / baseSpot;
        
        switch (greek) {
          case 'delta':
            value = 0.5 + moneyness * 2;
            value = Math.max(-1, Math.min(1, value));
            break;
          case 'gamma':
            value = Math.exp(-Math.pow(moneyness * 10, 2)) * (1 + iv);
            break;
          case 'theta':
            value = -20 * Math.exp(-Math.pow(moneyness * 10, 2)) * iv;
            break;
          case 'vega':
            value = 100 * Math.exp(-Math.pow(moneyness * 10, 2));
            break;
        }
        
        cells.push({ spot, iv, value });
      }
    }
    return cells;
  }, [data, greek]);

  // Get min/max for color scaling
  const { minVal, maxVal } = useMemo(() => {
    const values = gridData.map((c) => c.value);
    return {
      minVal: Math.min(...values),
      maxVal: Math.max(...values),
    };
  }, [gridData]);

  // Value to color mapping
  const valueToColor = (value: number): string => {
    const range = maxVal - minVal || 1;
    const normalized = (value - minVal) / range;
    
    if (value > 0) {
      // Green for positive
      const intensity = Math.min(normalized * 2, 1);
      const lightness = 70 - intensity * 35;
      const saturation = 40 + intensity * 30;
      return `hsl(120, ${saturation}%, ${lightness}%)`;
    } else if (value < 0) {
      // Red for negative
      const intensity = Math.min((1 - normalized) * 2, 1);
      const lightness = 70 - intensity * 35;
      const saturation = 40 + intensity * 30;
      return `hsl(0, ${saturation}%, ${lightness}%)`;
    }
    return 'hsl(0, 0%, 25%)';
  };

  // Draw the heatmap
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || !gridData.length) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const width = canvas.width;
    const height = canvas.height;
    const gridSize = Math.sqrt(gridData.length);
    const cellWidth = width / gridSize;
    const cellHeight = height / gridSize;

    // Clear
    ctx.fillStyle = 'hsl(220, 20%, 10%)';
    ctx.fillRect(0, 0, width, height);

    // Draw cells
    gridData.forEach((cell, index) => {
      const x = (index % gridSize) * cellWidth;
      const y = Math.floor(index / gridSize) * cellHeight;
      
      ctx.fillStyle = valueToColor(cell.value);
      ctx.fillRect(x, y, cellWidth - 1, cellHeight - 1);
    });
  }, [gridData, maxVal, minVal]);

  // Handle mouse move for tooltip
  const handleMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const canvas = canvasRef.current;
    const tooltip = tooltipRef.current;
    if (!canvas || !tooltip || !gridData.length) return;

    const rect = canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    
    const gridSize = Math.sqrt(gridData.length);
    const cellWidth = canvas.width / gridSize;
    const cellHeight = canvas.height / gridSize;
    
    const col = Math.floor(x / cellWidth);
    const row = Math.floor(y / cellHeight);
    const index = row * gridSize + col;
    
    if (index >= 0 && index < gridData.length) {
      const cell = gridData[index];
      tooltip.style.display = 'block';
      tooltip.style.left = `${x + 10}px`;
      tooltip.style.top = `${y - 40}px`;
      tooltip.innerHTML = `
        <div class="text-xs">
          <div>Spot: ${formatNumber(cell.spot, 0)}</div>
          <div>IV: ${(cell.iv * 100).toFixed(1)}%</div>
          <div class="font-semibold">${greek}: ${formatNumber(cell.value, 4)}</div>
        </div>
      `;
    }
  };

  const handleMouseLeave = () => {
    if (tooltipRef.current) {
      tooltipRef.current.style.display = 'none';
    }
  };

  if (isLoading) {
    return (
      <div className="h-[300px] flex items-center justify-center bg-background-tertiary/50">
        <div className="animate-pulse text-foreground-muted">Loading surface...</div>
      </div>
    );
  }

  return (
    <div className="relative">
      <canvas
        ref={canvasRef}
        width={400}
        height={300}
        className="w-full h-[300px] cursor-crosshair"
        onMouseMove={handleMouseMove}
        onMouseLeave={handleMouseLeave}
      />
      
      {/* Axis Labels */}
      <div className="absolute bottom-2 left-1/2 -translate-x-1/2 text-xs text-foreground-muted">
        Spot Price →
      </div>
      <div className="absolute top-1/2 left-2 -translate-y-1/2 -rotate-90 text-xs text-foreground-muted">
        IV →
      </div>
      
      {/* Tooltip */}
      <div
        ref={tooltipRef}
        className="absolute hidden bg-background-secondary border border-border rounded-lg p-2 shadow-lg pointer-events-none z-20"
      />
    </div>
  );
}
