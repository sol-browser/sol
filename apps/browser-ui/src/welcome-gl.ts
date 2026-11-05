// The welcome's sky (WebGL2, one full-screen pass): Sol as a black hole. A
// starfield we fly through bends around a dark disc; smoky golden light swirls
// round it, brighter on one side; a hairline photon ring traces its edge, where
// Sol's logo appears. Film grain over everything. It does not follow the pointer. Written for Sol (MPL-2.0).
export type Sky = {
  /** Draws one frame; times and levels come from the welcome's timeline. */
  draw(frame: SkyFrame): void;
  resize(width: number, height: number, ratio: number): void;
  dispose(): void;
};
export type SkyFrame = {
  time: number;          // seconds since the welcome began
  stars: number;         // 0..1 starfield fade-in
  hole: number;          // 0..1 the dark disc and its lensing form
  smoke: number;         // 0..1 the light swirling round it
  bass: number;          // 0..1 the music's low end
  exit: number;          // 0..1 leaving: a last flare
  ring: { x: number; y: number; radius: number };  // the logo's circle, CSS px, top-left origin
};

const vertex = `#version 300 es
in vec2 position;
void main() { gl_Position = vec4(position, 0.0, 1.0); }`;

const fragment = `#version 300 es
precision highp float;
uniform vec2 uRes;
uniform float uTime, uStars, uHole, uSmoke, uBass, uExit;
uniform vec3 uRing;
out vec4 outColor;
const float TAU = 6.2831853;

float h1(float n) { return fract(sin(n * 127.1) * 43758.5453); }
float hash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p), u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) { float v = 0.0, a = 0.5; for (int i = 0; i < 5; i++) { v += a * noise(p); p = p * 2.03 + 17.1; a *= 0.5; } return v; }

// Flying through the stars: layers at different depths come towards the
// viewer, grow and pass by; each fades in far away and out as it passes.
vec3 starfield(vec2 uv, float t) {
  vec3 col = vec3(0.0);
  const float LAYERS = 5.0;
  for (float i = 0.0; i < LAYERS; i++) {
    float z = fract(i / LAYERS + t * 0.035);
    float scale = mix(26.0, 1.6, z);
    float fade = smoothstep(0.0, 0.35, z) * (1.0 - smoothstep(0.82, 1.0, z));
    float a = t * 0.012 + i * 1.7;
    vec2 p = mat2(cos(a), -sin(a), sin(a), cos(a)) * uv * scale + i * 37.1;
    vec2 id = floor(p);
    for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {
      vec2 cell = id + vec2(x, y);
      float h = hash(cell + i * 11.0);
      if (h < 0.55) continue;
      vec2 d = p - (cell + 0.5 + (vec2(hash(cell + 1.3), hash(cell + 7.1)) - 0.5) * 0.8);
      float mag = pow(hash(cell + 3.7 + i), 7.0);
      float twinkle = 0.65 + 0.35 * sin(t * (0.7 + h * 2.4) + h * 61.0);
      float r = length(d) * (1.0 + 2.0 * (1.0 - z));
      float light = exp(-r * r / (0.0005 + 0.0015 * mag)) + exp(-r / 0.06) * 0.12 * mag;
      light += (exp(-abs(d.x) / 0.004) * exp(-abs(d.y) / 0.22) + exp(-abs(d.y) / 0.004) * exp(-abs(d.x) / 0.22)) * mag * 0.6;
      vec3 tint = mix(vec3(1.0, 0.86, 0.68), vec3(0.78, 0.86, 1.0), hash(cell + 9.2));
      col += tint * light * twinkle * fade * (0.35 + 0.9 * mag);
    }
  }
  return col;
}

void main() {
  vec2 frag = gl_FragCoord.xy;
  vec2 center = vec2(uRing.x, uRes.y - uRing.y);
  float shortSide = min(uRes.x, uRes.y);
  float t = uTime;
  float R = uRing.z;
  vec2 rel = (frag - center) / R;            // in hole radii
  float r = length(rel);

  // Stars, bent by the hole: light from behind is pushed outwards near the edge.
  float bend = uHole * 0.85 / max(r * r, 0.15);
  vec2 lensed = (frag - center) * (1.0 + bend) / shortSide;
  vec3 col = mix(vec3(0.010, 0.009, 0.014), vec3(0.024, 0.017, 0.015), exp(-r * 0.5));
  col += starfield(lensed, t) * uStars;

  // Smoky light streaming past behind the hole, seen through it as through a
  // lens: each point of the sky shows the light from where the hole's gravity
  // bends it from (the point-lens equation). Light passing behind it is
  // stretched into arcs round its edge and brightened near the Einstein ring.
  vec3 hot = vec3(1.0, 0.95, 0.86), gold = vec3(1.0, 0.76, 0.32), ember = vec3(0.85, 0.36, 0.08);
  float einstein = 1.32;
  float r2 = max(dot(rel, rel), 0.05);
  vec2 source = rel * (1.0 - einstein * einstein / r2);
  float magnify = min(1.0 / max(abs(1.0 - pow(einstein * einstein / r2, 2.0)), 1e-3), 3.2);
  // The stream: flat sheets of light, drawn out along their way and barely
  // folded, coming from one side that turns slowly; now and then a dense one.
  float heading = 0.35 + 0.5 * sin(t * 0.07);
  vec2 from = vec2(cos(heading), sin(heading));
  vec2 sp = mat2(from.x, -from.y, from.y, from.x) * source * 0.55;
  sp.x -= t * 0.75;
  float fold = fbm(sp * vec2(0.18, 1.1) + t * 0.1) - 0.5;
  float density = fbm(vec2(sp.x * 0.22, sp.y * 2.6 + fold * 0.5));
  float clouds = smoothstep(0.55, 0.8, density) * smoothstep(0.48, 0.7, fbm(sp * vec2(0.12, 0.35) + vec2(3.0, t * 0.05)));
  float light = clouds * magnify * 0.7;
  light *= smoothstep(1.0, 1.05, r) * uSmoke * (1.0 + 0.4 * uBass);
  col += mix(mix(ember, gold, smoothstep(0.05, 0.4, light)), hot, smoothstep(0.45, 1.3, light)) * light * 1.7;

  // The shadow: nothing comes from inside; a hairline photon ring traces its edge.
  col *= mix(1.0, smoothstep(0.93, 1.0, r), uHole);
  col += vec3(1.0, 0.9, 0.72) * exp(-abs(r - 1.0) / 0.006) * 0.45 * uHole;
  col += vec3(1.0, 0.75, 0.35) * exp(-abs(r - 1.0) / 0.05) * 0.12 * uHole * uSmoke;
  col += vec3(1.0, 0.85, 0.6) * uExit * uExit * 0.6 * exp(-r / 2.0);

  // Tone curve, vignette, and a visible film grain over everything.
  col = 1.0 - exp(-col * 1.15);
  col *= 1.0 - 0.35 * smoothstep(0.55, 1.35, length((frag - uRes * 0.5) / uRes.y));
  col += (hash(frag + fract(t * 7.0) * vec2(113.0, 271.0)) - 0.5) * 0.085;
  outColor = vec4(col, 1.0);
}`;

export function createSky(canvas: HTMLCanvasElement): Sky | null {
  const gl = canvas.getContext('webgl2', { antialias: false, alpha: false, premultipliedAlpha: false, powerPreference: 'high-performance' });
  if (!gl) return null;
  const compile = (type: number, source: string) => {
    const shader = gl.createShader(type)!;
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) { console.warn(gl.getShaderInfoLog(shader)); return null; }
    return shader;
  };
  const vs = compile(gl.VERTEX_SHADER, vertex), fs = compile(gl.FRAGMENT_SHADER, fragment);
  if (!vs || !fs) return null;
  const program = gl.createProgram()!;
  gl.attachShader(program, vs);
  gl.attachShader(program, fs);
  gl.linkProgram(program);
  gl.deleteShader(vs);
  gl.deleteShader(fs);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) { console.warn(gl.getProgramInfoLog(program)); return null; }
  gl.useProgram(program);
  const array = gl.createVertexArray();
  gl.bindVertexArray(array);
  const buffer = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
  const position = gl.getAttribLocation(program, 'position');
  gl.enableVertexAttribArray(position);
  gl.vertexAttribPointer(position, 2, gl.FLOAT, false, 0, 0);
  const at = (name: string) => gl.getUniformLocation(program, name);
  const u = { res: at('uRes'), time: at('uTime'), stars: at('uStars'), hole: at('uHole'), smoke: at('uSmoke'),
    bass: at('uBass'), exit: at('uExit'), ring: at('uRing') };
  let ratio = 1;
  return {
    resize(width, height, scale) {
      ratio = scale;
      canvas.width = Math.max(1, Math.round(width * scale));
      canvas.height = Math.max(1, Math.round(height * scale));
      gl.viewport(0, 0, canvas.width, canvas.height);
    },
    draw(frame) {
      gl.uniform2f(u.res, canvas.width, canvas.height);
      gl.uniform1f(u.time, frame.time);
      gl.uniform1f(u.stars, frame.stars);
      gl.uniform1f(u.hole, frame.hole);
      gl.uniform1f(u.smoke, frame.smoke);
      gl.uniform1f(u.bass, frame.bass);
      gl.uniform1f(u.exit, frame.exit);
      gl.uniform3f(u.ring, frame.ring.x * ratio, frame.ring.y * ratio, frame.ring.radius * ratio);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    },
    dispose() {
      gl.deleteBuffer(buffer);
      gl.deleteVertexArray(array);
      gl.deleteProgram(program);
      // The context goes with its canvas; losing it here would break a canvas that is drawn again.
    },
  };
}
