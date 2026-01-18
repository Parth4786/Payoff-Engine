'use client';

import { useMemo } from 'react';

interface HeatmapCell {
  spot: number;
  iv: number;
  value: number;
}

interface Props {
  greek: string;
  data?: HeatmapCell[];
}

export function KillZoneOverlay({ greek, data }: Props) {
  // Calculate kill zones based on Greek type
  const killZones = useMemo(() => {
    if (!data) return [];

    const gridSize = Math.sqrt(data.length);
    const zones: { x: number; y: number; width: number; height: number }[] = [];

    // Define thresholds for kill zones
    const thresholds: Record<string, { min: number; max: number }> = {
      delta: { min: -0.8, max: 0.8 },
      gamma: { min: 0, max: 0.02 }, // High gamma is danger
      theta: { min: -Infinity, max: -30 }, // High theta decay
      vega: { min: 0, max: Infinity },
    };

    const threshold = thresholds[greek];
    if (!threshold) return [];

    // Find cells that exceed thresholds
    data.forEach((cell, index) => {
      const x = (index % gridSize) / gridSize * 100;
      const y = Math.floor(index / gridSize) / gridSize * 100;
      
      let isDanger = false;
      
      if (greek === 'gamma') {
        // High gamma is dangerous (near ATM, close to expiry)
        isDanger = cell.value > 0.005;
      } else if (greek === 'theta') {
        // Large negative theta is costly
        isDanger = cell.value < -30;
      } else if (greek === 'delta') {
        // Delta near 1 or -1 means ITM
        isDanger = Math.abs(cell.value) > 0.9;
      }

      if (isDanger) {
        zones.push({
          x,
          y,
          width: 100 / gridSize,
          height: 100 / gridSize,
        });
      }
    });

    return zones;
  }, [data, greek]);

  if (killZones.length === 0) return null;

  return (
    <div className="absolute inset-0 pointer-events-none">
      <svg className="w-full h-full" viewBox="0 0 100 100" preserveAspectRatio="none">
        <defs>
          <pattern
            id={`killzone-pattern-${greek}`}
            patternUnits="userSpaceOnUse"
            width="4"
            height="4"
            patternTransform="rotate(45)"
          >
            <line
              x1="0"
              y1="0"
              x2="0"
              y2="4"
              stroke="hsl(var(--loss))"
              strokeWidth="1"
              strokeOpacity="0.5"
            />
          </pattern>
        </defs>
        
        {killZones.map((zone, i) => (
          <rect
            key={i}
            x={zone.x}
            y={zone.y}
            width={zone.width}
            height={zone.height}
            fill={`url(#killzone-pattern-${greek})`}
            stroke="hsl(var(--loss))"
            strokeWidth="0.5"
            strokeOpacity="0.6"
            strokeDasharray="2 1"
          />
        ))}
      </svg>
    </div>
  );
}
