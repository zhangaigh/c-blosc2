/*
  Copyright (C) 2017  Francesc Alted
  http://blosc.org
  License: BSD (see LICENSE.txt)

  Benchmark showing Blosc TRUNC_PREC filter from C code.

  To compile this program:

  $ gcc -O3 trunc_prec_schunk.c -o trunc_prec_schunk -lblosc

*/

#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <math.h>
#include "blosc.h"


#define KB  1024
#define MB  (1024*KB)
#define GB  (1024*MB)

#define NCHUNKS 200
#define CHUNKSIZE (500 * 1000)
#define NTHREADS 4


void fill_buffer(double *buffer, size_t nchunk) {
  double incx = 10. / (NCHUNKS * CHUNKSIZE);

  for (int i = 0; i < CHUNKSIZE; i++) {
    double x = incx * (nchunk * CHUNKSIZE + i);
    buffer[i] = (x - .25) * (x - 4.45) * (x - 8.95);
    //buffer[i] = x;
  }
}


int main() {
  blosc2_cparams cparams = BLOSC_CPARAMS_DEFAULTS;
  blosc2_dparams dparams = BLOSC_DPARAMS_DEFAULTS;
  blosc2_schunk *schunk;
  size_t isize = CHUNKSIZE * sizeof(double);
  int dsize;
  int64_t nbytes, cbytes;
  size_t nchunk, nchunks = 0;
  blosc_timestamp_t last, current;
  double totaltime;
  float totalsize = isize * NCHUNKS;
  double *data_buffer = malloc(CHUNKSIZE * sizeof(double));
  double *rec_buffer = malloc(CHUNKSIZE * sizeof(double));

  printf("Blosc version info: %s (%s)\n",
         BLOSC_VERSION_STRING, BLOSC_VERSION_DATE);

  /* Initialize the Blosc compressor */
  blosc_init();

  /* Create a super-chunk container */
  cparams.filters[0] = BLOSC_TRUNC_PREC;
  cparams.filters_meta[0] = 23;  // treat doubles as floats
  cparams.typesize = sizeof(double);
  // DELTA makes compression ratio quite worse in this case
  //cparams.filters[1] = BLOSC_DELTA;
  // BLOSC_BITSHUFFLE is not compressing better and it quite slower here
  //cparams.filters[BLOSC_LAST_FILTER - 1] = BLOSC_BITSHUFFLE;
  // Good codec params for this dataset
  //cparams.compcode = BLOSC_LZ4;
  //cparams.clevel = 9;
  cparams.compcode = BLOSC_LIZARD;
  cparams.clevel = 9;
  //cparams.compcode = BLOSC_BLOSCLZ;
  //cparams.clevel = 9;
  //cparams.compcode = BLOSC_ZSTD;
  //cparams.clevel = 7;
  cparams.nthreads = NTHREADS;
  schunk = blosc2_new_schunk(cparams, dparams);

  /* Append the chunks */
  blosc_set_timestamp(&last);
  for (nchunk = 0; nchunk < NCHUNKS; nchunk++) {
    fill_buffer(data_buffer, nchunk);
    nchunks = blosc2_append_buffer(schunk, isize, data_buffer);
  }
  blosc_set_timestamp(&current);
  totaltime = blosc_elapsed_secs(last, current);
  printf("[Compr] Elapsed time:\t %6.3f s."
                 "  Processed data: %.3f GB (%.3f GB/s)\n",
         totaltime, totalsize / GB, totalsize / (GB * totaltime));

  /* Gather some info */
  nbytes = schunk->nbytes;
  cbytes = schunk->cbytes;
  printf("Compression super-chunk: %ld -> %ld (%.1fx)\n",
         (long)nbytes, (long)cbytes, (1. * nbytes) / cbytes);

  /* Retrieve and decompress the chunks */
  blosc_set_timestamp(&last);
  for (nchunk = 0; nchunk < NCHUNKS; nchunk++) {
    dsize = blosc2_decompress_chunk(schunk, nchunk, (void*)rec_buffer, isize);
    if (dsize < 0) {
      printf("Decompression error.  Error code: %d\n", dsize);
      return dsize;
    }
    assert (dsize == isize);
  }
  blosc_set_timestamp(&current);
  totaltime = blosc_elapsed_secs(last, current);
  totalsize = isize * nchunks;
  printf("[Decompr] Elapsed time:\t %6.3f s."
                 "  Processed data: %.3f GB (%.3f GB/s)\n",
         totaltime, totalsize / GB, totalsize / (GB * totaltime));

  /* Check that all the values are in the precision range */
  blosc_set_timestamp(&last);
  for (nchunk = 0; nchunk < NCHUNKS; nchunk++) {
    dsize = blosc2_decompress_chunk(schunk, nchunk, (void*)rec_buffer, isize);
    if (dsize < 0) {
      printf("Decompression error.  Error code: %d\n", dsize);
      return dsize;
    }
    assert (dsize == isize);
    fill_buffer(data_buffer, nchunk);
    for (int i = 0; i < CHUNKSIZE; i++) {
      if (fabs(data_buffer[i] - rec_buffer[i]) > 1e-5) {
        printf("Value not in tolerance margin: ");
        printf("%g - %g: %g, (nchunk: %d, nelem: %d)\n",
               data_buffer[i], rec_buffer[i],
               (data_buffer[i] - rec_buffer[i]), (int)nchunk, i);
        return -1;
      }
    }
  }
  printf("All data did a good roundtrip!\n");

  /* Free resources */
  free(data_buffer);
  free(rec_buffer);
  /* Destroy the super-chunk */
  blosc2_destroy_schunk(schunk);
  /* Destroy the Blosc environment */
  blosc_destroy();

  return 0;
}