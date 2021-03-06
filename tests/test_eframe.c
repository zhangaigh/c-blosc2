/*
  Copyright (C) 2020 The Blosc Developers
  http://blosc.org
  License: BSD (see LICENSE.txt)

  Creation date: 2020-12-02

  See LICENSE.txt for details about copyright and rights to use.
*/

#include <stdio.h>
#include "test_common.h"

#define CHUNKSIZE (200 * 1000)
#define NTHREADS (2)

/* Global vars */
int nchunks_[] = {0, 1, 2, 10};
int tests_run = 0;
int nchunks;
bool multithread;
bool splits;
bool free_new;
bool filter_pipeline;
bool metalayers;
bool usermeta;
char* directory;
char buf[256];


static char* test_eframe(void) {
  size_t isize = CHUNKSIZE * sizeof(int32_t);
  int32_t* data = malloc(isize);
  int32_t* data_dest = malloc(isize);
  int dsize;
  int64_t nbytes, cbytes;
  blosc2_cparams cparams = BLOSC2_CPARAMS_DEFAULTS;
  blosc2_dparams dparams = BLOSC2_DPARAMS_DEFAULTS;
  if (filter_pipeline) {
    cparams.filters[BLOSC2_MAX_FILTERS - 2] = BLOSC_DELTA;
    cparams.filters_meta[BLOSC2_MAX_FILTERS - 2] = 0;
  }
  blosc2_schunk* schunk;

  /* Initialize the Blosc compressor */
  blosc_init();

  /* Create a frame container */
  cparams.typesize = sizeof(int32_t);
  if (splits) {
    // Use a codec that splits blocks (important for lazy chunks).
    // Only BLOSCLZ is doing that.
    cparams.compcode = BLOSC_BLOSCLZ;
  }
#if defined(HAVE_LZ4)
  else {
    cparams.compcode = BLOSC_LZ4;
  }
#endif
  if (multithread) {
    cparams.nthreads = NTHREADS;
    dparams.nthreads = NTHREADS;
  }
  else {
    cparams.nthreads = 1;
    dparams.nthreads = 1;
  }
  blosc2_storage storage = {.sequential=false, .urlpath=directory, .cparams=&cparams, .dparams=&dparams};
  schunk = blosc2_schunk_new(storage);
  mu_assert("blosc2_schunk_new() failed", schunk != NULL);
  char* content = "This is a pretty long string with a good number of chars";
  char* content2 = "This is a pretty long string with a good number of chars; longer than content";
  char* content3 = "This is a short string, and shorter than content";
  uint8_t* content_;
  size_t content_len = strlen(content);
  size_t content_len2 = strlen(content2);
  size_t content_len3 = strlen(content3);

  if (metalayers) {
    blosc2_add_metalayer(schunk, "metalayer1", (uint8_t*)"my metalayer1", sizeof("my metalayer1"));
    blosc2_add_metalayer(schunk, "metalayer2", (uint8_t*)"my metalayer1", sizeof("my metalayer1"));
  }

  if (usermeta) {
    blosc2_update_usermeta(schunk, (uint8_t *) content, (int32_t) content_len, BLOSC2_CPARAMS_DEFAULTS);
  }

  if (free_new) {
    blosc2_schunk_free(schunk);
  }
  blosc2_storage storage2 = {.sequential=false, .urlpath=directory};
  schunk = blosc2_schunk_open(storage2);
  mu_assert("blosc2_schunk_open() failed", schunk != NULL);

  if (metalayers) {
    uint8_t* _content;
    uint32_t _content_len;
    blosc2_get_metalayer(schunk, "metalayer1", &_content, &_content_len);
    mu_assert("ERROR: bad metalayer content", strncmp((char*)_content, "my metalayer1", _content_len) == 0);
    if (_content != NULL) {
      free(_content);
    }
    blosc2_get_metalayer(schunk, "metalayer2", &_content, &_content_len);
    mu_assert("ERROR: bad metalayer content", strncmp((char*)_content, "my metalayer1", _content_len) == 0);
    if (_content != NULL) {
      free(_content);
    }
  }

  if (usermeta) {
    int content_len_ = blosc2_get_usermeta(schunk, &content_);
    mu_assert("ERROR: bad usermeta length in frame", (size_t) content_len_ == content_len);
    mu_assert("ERROR: bad usermeta data in frame", strncmp((char*)content_, content, content_len) == 0);
    free(content_);
    blosc2_update_usermeta(schunk, (uint8_t *) content2, (int32_t) content_len2, BLOSC2_CPARAMS_DEFAULTS);
  }

  // Feed it with data
  int _nchunks = 0;
  for (int nchunk = 0; nchunk < nchunks; nchunk++) {
    for (int i = 0; i < CHUNKSIZE; i++) {
      data[i] = i + nchunk * CHUNKSIZE;
    }
    _nchunks = blosc2_schunk_append_buffer(schunk, data, isize);
    mu_assert("ERROR: bad append in frame", nchunk >= 0);
  }
  mu_assert("ERROR: wrong number of append chunks", _nchunks == nchunks);

  if (metalayers) {
    uint8_t* _content;
    uint32_t _content_len;
    blosc2_get_metalayer(schunk, "metalayer1", &_content, &_content_len);
    mu_assert("ERROR: bad metalayer content", strncmp((char*)_content, "my metalayer1", _content_len) == 0);
    if (_content != NULL) {
      free(_content);
    }
    blosc2_get_metalayer(schunk, "metalayer2", &_content, &_content_len);
    mu_assert("ERROR: bad metalayer content", strncmp((char*)_content, "my metalayer1", _content_len) == 0);
    if (_content != NULL) {
      free(_content);
    }
    blosc2_update_metalayer(schunk, "metalayer2", (uint8_t*)"my metalayer2", sizeof("my metalayer2"));
  }

  if (usermeta) {
    int content_len_ = blosc2_get_usermeta(schunk, &content_);
    mu_assert("ERROR: bad usermeta length in frame", (size_t) content_len_ == content_len2);
    mu_assert("ERROR: bad usermeta data in frame", strncmp((char*)content_, content2, content_len2) == 0);
    free(content_);
    blosc2_update_usermeta(schunk, (uint8_t *) content3, (int32_t) content_len3, BLOSC2_CPARAMS_DEFAULTS);
  }

  if (free_new) {
    blosc2_schunk_free(schunk);
  }
  schunk = blosc2_schunk_open(storage2);

  /* Gather some info */
  nbytes = schunk->nbytes;
  cbytes = schunk->cbytes;
  if (nchunks > 0) {
    mu_assert("ERROR: bad compression ratio in frame", nbytes > 10 * cbytes);
  }

  // Check that the chunks have been decompressed correctly
  for (int nchunk = 0; nchunk < nchunks; nchunk++) {
    dsize = blosc2_schunk_decompress_chunk(schunk, nchunk, (void *) data_dest, isize);
    mu_assert("ERROR: chunk cannot be decompressed correctly.", dsize >= 0);
    for (int i = 0; i < CHUNKSIZE; i++) {
      mu_assert("ERROR: bad roundtrip", data_dest[i] == i + nchunk * CHUNKSIZE);
    }
  }

  if (metalayers) {
    uint8_t* _content;
    uint32_t _content_len;
    blosc2_get_metalayer(schunk, "metalayer1", &_content, &_content_len);
    mu_assert("ERROR: bad metalayer content", strncmp((char*)_content, "my metalayer1", _content_len) == 0);
    if (_content != NULL) {
      free(_content);
    }
    blosc2_get_metalayer(schunk, "metalayer2", &_content, &_content_len);
    mu_assert("ERROR: bad metalayer content", strncmp((char*)_content, "my metalayer2", _content_len) == 0);
    if (_content != NULL) {
      free(_content);
    }
  }

  if (usermeta) {
    int content_len_ = blosc2_get_usermeta(schunk, &content_);
    mu_assert("ERROR: bad usermeta length in frame", (size_t) content_len_ == content_len3);
    mu_assert("ERROR: bad usermeta data in frame", strncmp((char*)content_, content3, content_len3) == 0);
    free(content_);
  }

  /* Remove directory */
  blosc2_remove_dir(storage.urlpath);
  /* Free resources */
  free(data_dest);
  free(data);
  blosc2_schunk_free(schunk);

  /* Destroy the Blosc environment */
  blosc_destroy();

  return EXIT_SUCCESS;
}


static char* test_eframe_simple(void) {
  static int32_t data[CHUNKSIZE];
  static int32_t data_dest[CHUNKSIZE];
  size_t isize = CHUNKSIZE * sizeof(int32_t);
  int dsize;
  blosc2_cparams cparams = BLOSC2_CPARAMS_DEFAULTS;
  blosc2_dparams dparams = BLOSC2_DPARAMS_DEFAULTS;
  blosc2_schunk* schunk;

  /* Initialize the Blosc compressor */
  blosc_init();

  /* Create a super-chunk container */
  cparams.typesize = sizeof(int32_t);
  cparams.clevel = 9;
  cparams.nthreads = NTHREADS;
  dparams.nthreads = NTHREADS;
  blosc2_storage storage = {.sequential=false, .urlpath=directory, .cparams=&cparams, .dparams=&dparams};
  blosc2_remove_dir(storage.urlpath);
  schunk = blosc2_schunk_new(storage);
  mu_assert("Error in creating schunk", schunk != NULL);

  // Feed it with data
  for (int nchunk = 0; nchunk < nchunks; nchunk++) {
    for (int i = 0; i < CHUNKSIZE; i++) {
      data[i] = i + nchunk;
    }
    int _nchunks = blosc2_schunk_append_buffer(schunk, data, isize);
    mu_assert("ERROR: bad append in eframe", _nchunks > 0);
  }

  /* Retrieve and decompress the chunks (0-based count) */
  for (int nchunk = nchunks-1; nchunk >= 0; nchunk--) {
    dsize = blosc2_schunk_decompress_chunk(schunk, nchunk, data_dest, isize);
    mu_assert("Decompression error", dsize>=0);
  }

  if (nchunks >= 2) {
    /* Check integrity of the second chunk (made of non-zeros) */
    blosc2_schunk_decompress_chunk(schunk, 1, data_dest, isize);
    for (int i = 0; i < CHUNKSIZE; i++) {
      mu_assert("Decompressed data differs from original", data_dest[i]==(i+1));
    }
  }

  /* Remove directory */
  blosc2_remove_dir(storage.urlpath);
  /* Free resources */
  blosc2_schunk_free(schunk);
  /* Destroy the Blosc environment */
  blosc_destroy();

  return EXIT_SUCCESS;
}


static char *all_tests(void) {
  directory = "dir1.b2eframe";

  nchunks = 0;
  mu_run_test(test_eframe_simple);

  nchunks = 1;
  mu_run_test(test_eframe_simple);

  nchunks = 2;
  mu_run_test(test_eframe_simple);

  nchunks = 10;
  mu_run_test(test_eframe_simple);

  // Check directory with a trailing slash
  directory = "dir1.b2eframe/";
  nchunks = 0;
  mu_run_test(test_eframe_simple);

  nchunks = 1;
  mu_run_test(test_eframe_simple);

  // Iterate over all different parameters
  for (int i = 0; i < (int)sizeof(nchunks_) / (int)sizeof(int); i++) {
    nchunks = nchunks_[i];
    for (int isplits = 0; isplits < 2; isplits++) {
      for (int imultithread = 0; imultithread < 2; imultithread++) {
        for (int ifree_new = 0; ifree_new < 2; ifree_new++) {
          for (int ifilter_pipeline = 0; ifilter_pipeline < 2; ifilter_pipeline++) {
            for (int imetalayers = 0; imetalayers < 2; imetalayers++) {
              for (int iusermeta = 0; iusermeta < 2; iusermeta++) {
                splits = (bool) isplits;
                multithread = (bool) imultithread;
                free_new = (bool) ifree_new;
                filter_pipeline = (bool) ifilter_pipeline;
                metalayers = (bool) imetalayers;
                usermeta = (bool) iusermeta;
                snprintf(buf, sizeof(buf), "test_eframe_nc%d.b2eframe", nchunks);
                directory = buf;
                mu_run_test(test_eframe);
                snprintf(buf, sizeof(buf), "test_eframe_nc%d.b2eframe/", nchunks);
                directory = buf;
                mu_run_test(test_eframe);
              }
            }
          }
        }
      }
    }
  }

  return EXIT_SUCCESS;
}


int main(void) {
  char *result;

  install_blosc_callback_test(); /* optionally install callback test */
  blosc_init();

  /* Run all the suite */
  result = all_tests();
  if (result != EXIT_SUCCESS) {
    printf(" (%s)\n", result);
  }
  else {
    printf(" ALL TESTS PASSED");
  }
  printf("\tTests run: %d\n", tests_run);

  blosc_destroy();

  return result != EXIT_SUCCESS;
}
