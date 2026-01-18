'use client';

import { useState } from 'react';
import { Navbar } from '@/components/shared';
import { GreekHeatmap } from '@/components/sensitivity/GreekHeatmap';
import { SurfaceControls } from '@/components/sensitivity/SurfaceControls';
import { KillZoneOverlay } from '@/components/sensitivity/KillZoneOverlay';
import { useStrategy, useSensitivitySurfaces } from '@/hooks';
import { Grid2X2, Layers } from 'lucide-react';
import Link from 'next/link';

type GreekType = 'delta' | 'gamma' | 'theta' | 'vega';

export default function SensitivityPage() {
  const { strategy } = useStrategy();
  const [selectedGreeks, setSelectedGreeks] = useState<GreekType[]>(['delta', 'gamma', 'theta', 'vega']);
  const [showKillZone, setShowKillZone] = useState(true);
  const [daysToExpiry, setDaysToExpiry] = useState(7);
  const [ivShift, setIvShift] = useState(0);
  
  const { data: surfacesData, isLoading } = useSensitivitySurfaces(daysToExpiry, ivShift);
  
  // Transform SensitivitySurface[] to lookup by type with HeatmapCell format
  const surfaces = surfacesData?.reduce((acc, surface) => {
    // Transform surface.surface (SensitivityRow[]) to HeatmapCell[]
    const heatmapCells = surface.surface.flatMap((row) =>
      row.values.map((val) => ({
        spot: row.spot,
        iv: val.days / 365, // Convert days to fraction for heatmap display
        value: val.value,
      }))
    );
    acc[surface.type] = heatmapCells;
    return acc;
  }, {} as Record<string, Array<{ spot: number; iv: number; value: number }>>);

  const toggleGreek = (greek: GreekType) => {
    setSelectedGreeks((prev) =>
      prev.includes(greek) ? prev.filter((g) => g !== greek) : [...prev, greek]
    );
  };

  return (
    <div className="min-h-screen">
      <Navbar
        title="Sensitivity Maps"
        subtitle={strategy.underlying}
        actions={
          <div className="flex items-center gap-2">
            <button
              onClick={() => setShowKillZone(!showKillZone)}
              className={`flex items-center gap-1.5 px-3 py-2 rounded-lg text-sm font-medium transition-colors ${
                showKillZone
                  ? 'bg-loss/20 text-loss'
                  : 'bg-background-tertiary text-foreground-muted hover:text-foreground'
              }`}
            >
              <Layers className="w-4 h-4" />
              Kill Zone
            </button>
            <Link
              href="/replay"
              className="flex items-center gap-1.5 px-4 py-2 bg-accent text-white rounded-lg text-sm font-medium hover:bg-accent/90 transition-colors"
            >
              Replay Mode
            </Link>
          </div>
        }
      />

      <div className="p-6 space-y-6">
        {/* Controls */}
        <div className="card">
          <div className="card-content">
            <SurfaceControls
              selectedGreeks={selectedGreeks}
              onToggleGreek={toggleGreek}
              daysToExpiry={daysToExpiry}
              onDaysChange={setDaysToExpiry}
              ivShift={ivShift}
              onIvShiftChange={setIvShift}
            />
          </div>
        </div>

        {/* Heatmap Grid */}
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
          {selectedGreeks.map((greek) => (
            <div key={greek} className="card">
              <div className="card-header flex items-center justify-between">
                <h2 className="card-title capitalize">{greek} Surface</h2>
                <span className="text-xs text-foreground-muted">Spot × IV</span>
              </div>
              <div className="card-content p-0 relative">
                <GreekHeatmap
                  greek={greek}
                  data={surfaces?.[greek]}
                  isLoading={isLoading}
                />
                {showKillZone && (
                  <KillZoneOverlay
                    greek={greek}
                    data={surfaces?.[greek]}
                  />
                )}
              </div>
            </div>
          ))}
        </div>

        {/* Legend */}
        <div className="card">
          <div className="card-header">
            <h2 className="card-title flex items-center gap-2">
              <Grid2X2 className="w-4 h-4" />
              Color Legend
            </h2>
          </div>
          <div className="card-content">
            <div className="flex flex-wrap gap-6">
              <div className="flex items-center gap-3">
                <div className="flex items-center gap-0.5">
                  <div className="w-6 h-4 bg-[hsl(0,70%,35%)]" />
                  <div className="w-6 h-4 bg-[hsl(0,60%,45%)]" />
                  <div className="w-6 h-4 bg-[hsl(0,50%,55%)]" />
                  <div className="w-6 h-4 bg-[hsl(0,40%,70%)]" />
                </div>
                <span className="text-xs text-foreground-muted">Negative</span>
              </div>
              <div className="flex items-center gap-3">
                <div className="w-6 h-4 bg-background-tertiary border border-border" />
                <span className="text-xs text-foreground-muted">Near Zero</span>
              </div>
              <div className="flex items-center gap-3">
                <div className="flex items-center gap-0.5">
                  <div className="w-6 h-4 bg-[hsl(120,40%,70%)]" />
                  <div className="w-6 h-4 bg-[hsl(120,50%,55%)]" />
                  <div className="w-6 h-4 bg-[hsl(120,60%,45%)]" />
                  <div className="w-6 h-4 bg-[hsl(120,70%,35%)]" />
                </div>
                <span className="text-xs text-foreground-muted">Positive</span>
              </div>
              {showKillZone && (
                <div className="flex items-center gap-3">
                  <div className="w-6 h-4 bg-loss/30 border-2 border-loss/60 border-dashed" />
                  <span className="text-xs text-foreground-muted">Kill Zone (High Risk)</span>
                </div>
              )}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
