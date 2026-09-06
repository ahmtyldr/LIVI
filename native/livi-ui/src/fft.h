// Radix-2 iterative FFT and the log-scaled spectrum bins the Media
// visualiser draws — a C port of src/renderer/src/components/worker/fft.ts
// and fft.worker.ts (FFT_SIZE 4096, 24 bars, Hamming window, dB floor -80).
#pragma once
#include <stddef.h>

#define FFT_SIZE 4096
#define FFT_POINTS 24

typedef struct fft fft_t;

fft_t *fft_create(void);
/** Windowed real transform of `input` (FFT_SIZE samples) → power per bin. */
void fft_forward(fft_t *f, const float *input);
/** Fills `bins` (FFT_POINTS, 0..1) from the last transform, log-binned by
 *  frequency for `sample_rate`. */
void fft_bins(fft_t *f, float *bins, int sample_rate);
