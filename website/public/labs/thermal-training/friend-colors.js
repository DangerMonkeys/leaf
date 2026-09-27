// Shared palette swap for the authored canopy skin, its shading, and fallback.
(function (root) {
  'use strict';
  function orangeRedPixels(data) {
    for (let i = 0; i < data.length; i += 4) {
      const r = data[i], g = data[i + 1], b = data[i + 2];
      // Only yellow-green fabric: neutral white/black, skin and blue stay intact.
      if (g > b + 8 && r > b + 8 && g > r * .9) {
        const fabric = (g - b) / 239;
        data[i] = Math.min(255, b + fabric * 255);
        data[i + 1] = Math.min(255, b + fabric * 79);
        data[i + 2] = Math.min(255, b + fabric * 42);
      }
    }
    return data;
  }
  function orangeRedCanvas(source) {
    const canvas = document.createElement('canvas');
    canvas.width = source.naturalWidth || source.width;
    canvas.height = source.naturalHeight || source.height;
    const ctx = canvas.getContext('2d');
    ctx.drawImage(source, 0, 0);
    const pixels = ctx.getImageData(0, 0, canvas.width, canvas.height);
    orangeRedPixels(pixels.data);
    ctx.putImageData(pixels, 0, 0);
    return canvas;
  }
  root.FriendColors = {orangeRedPixels, orangeRedCanvas};
})(globalThis);
