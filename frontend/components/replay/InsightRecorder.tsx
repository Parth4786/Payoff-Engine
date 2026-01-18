'use client';

import { useState } from 'react';
import { Plus, Save, Trash2, Lightbulb, Tag } from 'lucide-react';
import { cn, formatNumber } from '@/lib/utils';
import { useReplayStore } from '@/lib/store';
import type { ReplaySnapshot, RecordedInsight } from '@/lib/types';

interface Props {
  currentSnapshot: ReplaySnapshot | null;
}

const PRESET_TAGS = ['entry', 'exit', 'mistake', 'success', 'observation', 'question'];

export function InsightRecorder({ currentSnapshot }: Props) {
  const { insights, addInsight, removeInsight } = useReplayStore();
  const [isAdding, setIsAdding] = useState(false);
  const [newNote, setNewNote] = useState('');
  const [selectedTags, setSelectedTags] = useState<string[]>(['observation']);

  const handleAddInsight = () => {
    if (!currentSnapshot || !newNote.trim()) return;

    addInsight(newNote.trim(), selectedTags);
    setNewNote('');
    setSelectedTags(['observation']);
    setIsAdding(false);
  };

  const toggleTag = (tag: string) => {
    setSelectedTags((prev) =>
      prev.includes(tag) ? prev.filter((t) => t !== tag) : [...prev, tag]
    );
  };

  const getTagColor = (tag: string) => {
    const colors: Record<string, string> = {
      entry: 'bg-blue-500/20 text-blue-400',
      exit: 'bg-purple-500/20 text-purple-400',
      mistake: 'bg-loss/20 text-loss',
      success: 'bg-profit/20 text-profit',
      observation: 'bg-info/20 text-info',
      question: 'bg-warning/20 text-warning',
    };
    return colors[tag] || 'bg-foreground-muted/20 text-foreground-muted';
  };

  return (
    <div className="space-y-4">
      {/* Add New Insight */}
      {!isAdding ? (
        <button
          onClick={() => setIsAdding(true)}
          disabled={!currentSnapshot}
          className="w-full flex items-center justify-center gap-2 px-4 py-3 border-2 border-dashed border-border rounded-lg text-foreground-muted hover:border-accent hover:text-accent disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
        >
          <Plus className="w-4 h-4" />
          Add Insight at Current Point
        </button>
      ) : (
        <div className="p-4 border border-border rounded-lg space-y-3">
          <div className="flex items-center gap-2 flex-wrap">
            {PRESET_TAGS.map((tag) => (
              <button
                key={tag}
                onClick={() => toggleTag(tag)}
                className={cn(
                  'px-3 py-1 rounded-full text-xs font-medium transition-colors capitalize',
                  selectedTags.includes(tag)
                    ? getTagColor(tag)
                    : 'bg-background-tertiary text-foreground-muted hover:text-foreground'
                )}
              >
                {tag}
              </button>
            ))}
          </div>

          <textarea
            value={newNote}
            onChange={(e) => setNewNote(e.target.value)}
            placeholder="What did you observe? What would you do differently?"
            className="w-full px-3 py-2 text-sm bg-background-tertiary border border-border rounded-lg focus:outline-none focus:border-accent resize-none"
            rows={3}
            autoFocus
          />

          <div className="flex items-center justify-between">
            <span className="text-xs text-foreground-muted">
              At spot: {formatNumber(currentSnapshot?.underlying_price || 0, 0)}
            </span>
            <div className="flex items-center gap-2">
              <button
                onClick={() => setIsAdding(false)}
                className="px-3 py-1.5 text-sm text-foreground-muted hover:text-foreground transition-colors"
              >
                Cancel
              </button>
              <button
                onClick={handleAddInsight}
                disabled={!newNote.trim()}
                className="flex items-center gap-1.5 px-3 py-1.5 bg-accent text-white rounded-lg text-sm font-medium hover:bg-accent/90 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
              >
                <Save className="w-4 h-4" />
                Save
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Insights List */}
      {insights.length > 0 && (
        <div className="space-y-2">
          <h3 className="text-sm font-medium text-foreground-muted">
            Recorded Insights ({insights.length})
          </h3>
          <div className="space-y-2">
            {insights.map((insight) => {
              const time = new Date(insight.timestamp).toLocaleTimeString('en-IN', {
                hour: '2-digit',
                minute: '2-digit',
              });

              return (
                <div
                  key={insight.id}
                  className="group p-3 rounded-lg bg-background-tertiary/50 hover:bg-background-tertiary transition-colors"
                >
                  <div className="flex items-start justify-between gap-2">
                    <div className="flex items-start gap-2">
                      <Lightbulb className="w-4 h-4 text-warning mt-0.5 shrink-0" />
                      <div>
                        <div className="flex items-center gap-2 mb-1 flex-wrap">
                          {insight.tags?.map((tag) => (
                            <span
                              key={tag}
                              className={cn(
                                'px-2 py-0.5 rounded-full text-xs capitalize',
                                getTagColor(tag)
                              )}
                            >
                              {tag}
                            </span>
                          ))}
                          <span className="text-xs text-foreground-muted">{time}</span>
                          {insight.market_state?.spot && (
                            <span className="text-xs text-foreground-muted">
                              @ {formatNumber(insight.market_state.spot, 0)}
                            </span>
                          )}
                        </div>
                        <p className="text-sm text-foreground-secondary">{insight.text}</p>
                      </div>
                    </div>
                    <button
                      onClick={() => removeInsight(insight.id)}
                      className="p-1 rounded opacity-0 group-hover:opacity-100 hover:bg-loss/10 text-foreground-muted hover:text-loss transition-all"
                    >
                      <Trash2 className="w-4 h-4" />
                    </button>
                  </div>
                </div>
              );
            })}
          </div>
        </div>
      )}
    </div>
  );
}
