'use client';

import { useState } from 'react';
import { Navbar } from '@/components/shared';
import { ScreenerFilters } from '@/components/screener/ScreenerFilters';
import { ScreenerTable } from '@/components/screener/ScreenerTable';
import { IVSurface3D } from '@/components/screener/IVSurface3D';
import { OptionChainModal } from '@/components/screener/OptionChainModal';
import { useScreener } from '@/hooks';
import { Filter, Grid3X3, Table2 } from 'lucide-react';
import type { InstrumentSnapshot } from '@/lib/types';

export default function ScreenerPage() {
  const {
    instruments,
    filters,
    isLoading,
    underlyings,
    totalCount,
    currentPage,
    totalPages,
    limit,
    offset,
    updateFilters,
    goToPage,
    nextPage,
    prevPage,
    hasNextPage,
    hasPrevPage,
  } = useScreener();
  
  const [viewMode, setViewMode] = useState<'table' | 'grid'>('table');
  const [showFilters, setShowFilters] = useState(true);
  const [selectedInstrument, setSelectedInstrument] = useState<InstrumentSnapshot | null>(null);

  return (
    <div className="min-h-screen">
      <Navbar
        title="Option Screener"
        subtitle={`${totalCount.toLocaleString()} instruments`}
        actions={
          <div className="flex items-center gap-2">
            <button
              onClick={() => setShowFilters(!showFilters)}
              className={`flex items-center gap-1.5 px-3 py-2 rounded-lg text-sm font-medium transition-colors ${
                showFilters
                  ? 'bg-accent/20 text-accent'
                  : 'bg-background-tertiary text-foreground-muted hover:text-foreground'
              }`}
            >
              <Filter className="w-4 h-4" />
              Filters
            </button>
            <div className="flex items-center bg-background-tertiary rounded-lg p-1">
              <button
                onClick={() => setViewMode('table')}
                className={`p-1.5 rounded ${viewMode === 'table' ? 'bg-background text-foreground shadow-sm' : 'text-foreground-muted'}`}
              >
                <Table2 className="w-4 h-4" />
              </button>
              <button
                onClick={() => setViewMode('grid')}
                className={`p-1.5 rounded ${viewMode === 'grid' ? 'bg-background text-foreground shadow-sm' : 'text-foreground-muted'}`}
              >
                <Grid3X3 className="w-4 h-4" />
              </button>
            </div>
          </div>
        }
      />

      <div className="flex h-[calc(100vh-4rem)]">
        {/* Filters Panel */}
        {showFilters && (
          <div className="w-72 border-r border-border p-4 overflow-auto">
            <ScreenerFilters
              filters={filters}
              onFiltersChange={updateFilters}
              underlyings={underlyings.map((u) => u.symbol)}
            />
          </div>
        )}

        {/* Main Content */}
        <div className="flex-1 flex flex-col overflow-hidden">
          {/* Table View */}
          <div className="flex-1 overflow-auto">
            <ScreenerTable
              instruments={instruments}
              isLoading={isLoading}
              onSelectInstrument={setSelectedInstrument}
            />
          </div>

          {/* Pagination */}
          <div className="flex items-center justify-between px-4 py-3 border-t border-border bg-background-secondary">
            <span className="text-sm text-foreground-muted">
              Showing {offset + 1} - {Math.min(offset + limit, totalCount)} of {totalCount}
            </span>
            <div className="flex items-center gap-2">
              <button
                onClick={prevPage}
                disabled={!hasPrevPage}
                className="px-3 py-1.5 text-sm bg-background-tertiary rounded disabled:opacity-50 hover:bg-background transition-colors"
              >
                Previous
              </button>
              <span className="text-sm">
                Page {currentPage} of {totalPages}
              </span>
              <button
                onClick={nextPage}
                disabled={!hasNextPage}
                className="px-3 py-1.5 text-sm bg-background-tertiary rounded disabled:opacity-50 hover:bg-background transition-colors"
              >
                Next
              </button>
            </div>
          </div>
        </div>
      </div>

      {/* Option Chain Modal */}
      {selectedInstrument && (
        <OptionChainModal
          instrument={selectedInstrument}
          onClose={() => setSelectedInstrument(null)}
        />
      )}
    </div>
  );
}
