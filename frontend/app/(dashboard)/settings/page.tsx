'use client';

import { useEffect, useState } from 'react';

import { Navbar } from '@/components/shared';

type HealthResponse = {
  status?: string;
  timestamp?: string;
  version?: string;
};

export default function SettingsPage() {
  const [health, setHealth] = useState<HealthResponse | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;

    (async () => {
      try {
        const res = await fetch('/api/health');
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const json = (await res.json()) as HealthResponse;
        if (!cancelled) setHealth(json);
      } catch (e) {
        if (!cancelled) setError(e instanceof Error ? e.message : 'Failed to fetch');
      }
    })();

    return () => {
      cancelled = true;
    };
  }, []);

  return (
    <div className="min-h-screen">
      <Navbar title="Settings" subtitle="Diagnostics and environment info" />

      <div className="p-4 md:p-6">
        <div className="grid gap-4 md:grid-cols-2">
          <div className="rounded-xl border border-border bg-background-secondary p-4">
            <div className="mb-3">
              <div className="text-sm font-semibold">Backend</div>
              <div className="text-xs text-foreground-muted">/api/health</div>
            </div>

            {error ? (
              <div className="text-sm text-red-400">{error}</div>
            ) : !health ? (
              <div className="text-sm text-foreground-muted">Loading…</div>
            ) : (
              <div className="grid gap-2 text-sm">
                <div className="flex items-center justify-between">
                  <span className="text-foreground-muted">Status</span>
                  <span className="font-mono">{health.status ?? 'unknown'}</span>
                </div>
                <div className="flex items-center justify-between">
                  <span className="text-foreground-muted">Timestamp</span>
                  <span className="font-mono">{health.timestamp ?? '—'}</span>
                </div>
                <div className="flex items-center justify-between">
                  <span className="text-foreground-muted">Version</span>
                  <span className="font-mono">{health.version ?? '—'}</span>
                </div>
              </div>
            )}
          </div>

          <div className="rounded-xl border border-border bg-background-secondary p-4">
            <div className="mb-3">
              <div className="text-sm font-semibold">Frontend</div>
              <div className="text-xs text-foreground-muted">Runtime</div>
            </div>

            <div className="grid gap-2 text-sm">
              <div className="flex items-center justify-between">
                <span className="text-foreground-muted">Node Env</span>
                <span className="font-mono">{process.env.NODE_ENV}</span>
              </div>
              <div className="flex items-center justify-between">
                <span className="text-foreground-muted">API</span>
                <span className="font-mono">/api (Next rewrite)</span>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
