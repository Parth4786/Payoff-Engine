import type { Metadata } from 'next';
import { Inter, JetBrains_Mono } from 'next/font/google';
import { Providers } from './providers';
import './globals.css';

const inter = Inter({
  subsets: ['latin'],
  variable: '--font-inter',
  display: 'swap',
});

const jetbrainsMono = JetBrains_Mono({
  subsets: ['latin'],
  variable: '--font-mono',
  display: 'swap',
});

export const metadata: Metadata = {
  title: 'Payoff Engine | Options Analytics Console',
  description: 'Desk-grade options strategy and analytics console with curvature visualization, kill-zone warnings, and real-time replay.',
  keywords: ['options', 'trading', 'analytics', 'payoff', 'greeks', 'NIFTY', 'BANKNIFTY'],
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en" className="dark">
      <body
        className={`${inter.variable} ${jetbrainsMono.variable} antialiased min-h-screen bg-background font-sans`}
      >
        <Providers>{children}</Providers>
      </body>
    </html>
  );
}
