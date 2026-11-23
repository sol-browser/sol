import { useId } from 'react';
import type { TreeNode } from './bridge';

// Each curve repeats after 240 units. The SVG clips to one period as the
// paths translate, so the last frame meets the first without a jump.
const farWave = 'M0 20 C20 8 40 5 60 5 S100 8 120 20 S160 35 180 35 S220 32 240 20 C260 8 280 5 300 5 S340 8 360 20 S400 35 420 35 S460 32 480 20 V48 H0Z';
const nearWave = 'M0 28 C20 20 40 16 60 16 S100 20 120 28 S160 40 180 40 S220 36 240 28 C260 20 280 16 300 16 S340 20 360 28 S400 40 420 40 S460 36 480 28 V48 H0Z';

function SpotifyWaves() {
  const id = useId();
  return <svg className="spotify-waves" viewBox="0 0 240 48" preserveAspectRatio="none" focusable="false">
    <defs>
      <linearGradient id={`${id}-far`} x2="0" y2="1">
        <stop stopColor="#51e6b1" stopOpacity=".58" />
        <stop offset="1" stopColor="#0fb876" stopOpacity=".06" />
      </linearGradient>
      <linearGradient id={`${id}-near`} x2="0" y2="1">
        <stop stopColor="#b1ee72" stopOpacity=".15" />
        <stop offset=".6" stopColor="#35c46f" stopOpacity=".39" />
        <stop offset="1" stopColor="#087c57" stopOpacity=".11" />
      </linearGradient>
    </defs>
    <path className="spotify-wave-far" d={farWave} fill={`url(#${id}-far)`} />
    <path className="spotify-wave-near" d={nearWave} fill={`url(#${id}-near)`} />
  </svg>;
}

export function ProtectedMediaTint({ service }: { service: TreeNode['mediaService'] }) {
  if (!service) return null;
  return <span className="protected-media-tint" aria-hidden="true">
    {service === 'spotify' && <SpotifyWaves />}
  </span>;
}
