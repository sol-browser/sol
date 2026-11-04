// Captures the visible tab's audio (as volume extensions do) and reports its
// RMS level four times a second. Nothing is played back.
chrome.tabCapture.capture({ audio: true, video: false }, stream => {
  if (!stream) { console.info('RED_CAPTURE_LEVEL:error:' + (chrome.runtime.lastError && chrome.runtime.lastError.message)); return; }
  const context = new AudioContext();
  const analyser = context.createAnalyser();
  analyser.fftSize = 2048;
  context.createMediaStreamSource(stream).connect(analyser);
  const samples = new Float32Array(analyser.fftSize);
  setInterval(() => {
    analyser.getFloatTimeDomainData(samples);
    let sum = 0; for (const value of samples) sum += value * value;
    console.info('RED_CAPTURE_LEVEL:' + Math.sqrt(sum / samples.length).toExponential(3));
  }, 250);
});
