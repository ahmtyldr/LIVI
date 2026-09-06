#include "fft.h"
#include <math.h>
#include <stdlib.h>

#define FLOOR_DB (-80.0)
#define MIN_FREQ 20.0

struct fft {
  float cos_t[FFT_SIZE / 2];
  float sin_t[FFT_SIZE / 2];
  unsigned rev[FFT_SIZE];
  float window[FFT_SIZE];
  float re[FFT_SIZE];
  float im[FFT_SIZE];
};

fft_t *fft_create(void) {
  fft_t *f = calloc(1, sizeof *f);
  if (!f) return NULL;
  int bits = 0;
  while ((1 << bits) < FFT_SIZE) bits++;
  for (int i = 0; i < FFT_SIZE; i++) {
    unsigned r = 0;
    for (int b = 0; b < bits; b++)
      if (i & (1 << b)) r |= 1u << (bits - 1 - b);
    f->rev[i] = r;
    f->window[i] = 0.54f - 0.46f * cosf((2.0f * (float)M_PI * i) / (FFT_SIZE - 1));
  }
  for (int i = 0; i < FFT_SIZE / 2; i++) {
    f->cos_t[i] = cosf((-2.0f * (float)M_PI * i) / FFT_SIZE);
    f->sin_t[i] = sinf((-2.0f * (float)M_PI * i) / FFT_SIZE);
  }
  return f;
}

void fft_forward(fft_t *f, const float *input) {
  for (int i = 0; i < FFT_SIZE; i++) {
    unsigned r = f->rev[i];
    f->re[r] = input[i] * f->window[i];
    f->im[r] = 0.0f;
  }
  for (int len = 2; len <= FFT_SIZE; len <<= 1) {
    int half = len >> 1;
    int step = FFT_SIZE / len;
    for (int base = 0; base < FFT_SIZE; base += len) {
      for (int j = 0, k = 0; j < half; j++, k += step) {
        float wr = f->cos_t[k], wi = f->sin_t[k];
        int a = base + j, b = a + half;
        float xr = f->re[b] * wr - f->im[b] * wi;
        float xi = f->re[b] * wi + f->im[b] * wr;
        f->re[b] = f->re[a] - xr;
        f->im[b] = f->im[a] - xi;
        f->re[a] += xr;
        f->im[a] += xi;
      }
    }
  }
}

void fft_bins(fft_t *f, float *bins, int sample_rate) {
  double sums[FFT_POINTS] = {0};
  int half = FFT_SIZE / 2;
  double scale = (double)half * half;
  double log_min = log10(MIN_FREQ);
  double log_max = log10(sample_rate / 2.0);
  double log_den = log_max - log_min;
  for (int i = 1; i <= half; i++) {
    double re = f->re[i], im = f->im[i];
    double freq = (double)i * sample_rate / FFT_SIZE;
    if (freq < MIN_FREQ || freq > sample_rate / 2.0) continue;
    double pos = (log10(freq) - log_min) / log_den;
    int idx = (int)(pos * FFT_POINTS);
    if (idx >= 0 && idx < FFT_POINTS) sums[idx] += (re * re + im * im) / scale;
  }
  for (int i = 0; i < FFT_POINTS; i++) {
    double amp = sqrt(sums[i]);
    double db = 20.0 * log10(amp + 1e-12);
    if (db < FLOOR_DB) db = FLOOR_DB;
    if (db > 0) db = 0;
    bins[i] = (float)((db - FLOOR_DB) / -FLOOR_DB);
  }
}
