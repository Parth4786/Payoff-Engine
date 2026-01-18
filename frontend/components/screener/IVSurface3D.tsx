'use client';

import { useEffect, useRef } from 'react';

interface Props {
  symbol: string;
  data?: { strike: number; expiry: number; iv: number }[];
}

export function IVSurface3D({ symbol, data }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    // Draw a stylized 3D surface visualization
    const width = canvas.width;
    const height = canvas.height;
    
    ctx.fillStyle = 'hsl(220, 20%, 10%)';
    ctx.fillRect(0, 0, width, height);

    // Draw grid
    ctx.strokeStyle = 'hsl(220, 20%, 20%)';
    ctx.lineWidth = 1;

    // Perspective grid
    const gridSize = 15;
    const centerX = width / 2;
    const centerY = height * 0.8;
    const scaleX = 15;
    const scaleY = 8;
    const scaleZ = 4;

    // Draw the surface
    for (let i = -gridSize; i < gridSize; i++) {
      for (let j = 0; j < gridSize; j++) {
        // Generate mock IV surface (smile shape)
        const x = i / gridSize;
        const y = j / gridSize;
        const iv = 0.15 + 0.1 * Math.pow(x, 2) + 0.05 * Math.exp(-y * 2);
        
        // 3D to 2D projection
        const projX = centerX + i * scaleX - j * scaleX * 0.5;
        const projY = centerY - j * scaleY - iv * 100 * scaleZ;
        
        // Color based on IV
        const hue = 240 - iv * 400; // Blue (low IV) to Red (high IV)
        ctx.fillStyle = `hsl(${Math.max(0, hue)}, 70%, 50%)`;
        
        ctx.beginPath();
        ctx.arc(projX, projY, 3, 0, Math.PI * 2);
        ctx.fill();
      }
    }

    // Draw axes
    ctx.strokeStyle = 'hsl(var(--foreground-muted))';
    ctx.lineWidth = 2;
    
    // X axis (Strike)
    ctx.beginPath();
    ctx.moveTo(centerX - gridSize * scaleX, centerY);
    ctx.lineTo(centerX + gridSize * scaleX, centerY);
    ctx.stroke();
    
    // Y axis (Expiry)  
    ctx.beginPath();
    ctx.moveTo(centerX - gridSize * scaleX, centerY);
    ctx.lineTo(centerX - gridSize * scaleX - gridSize * scaleX * 0.5, centerY - gridSize * scaleY);
    ctx.stroke();

    // Labels
    ctx.fillStyle = 'hsl(var(--foreground-muted))';
    ctx.font = '12px Inter, sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText('Strike →', centerX, centerY + 20);
    ctx.fillText('← Expiry', centerX - gridSize * scaleX - 30, centerY - gridSize * scaleY / 2);
    ctx.fillText('IV ↑', 20, height / 2);

  }, [symbol, data]);

  return (
    <div className="relative">
      <canvas
        ref={canvasRef}
        width={500}
        height={350}
        className="w-full h-[350px]"
      />
      <div className="absolute top-4 left-4 text-xs text-foreground-muted">
        {symbol} IV Surface
      </div>
    </div>
  );
}
