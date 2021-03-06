/*
    Copyright 2018,2020 Joel Svensson	svenssonjoel@yahoo.se

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#include "heap.h"
#include "symrepr.h"
#include "eval_cps.h"
#include "print.h"
#include "tokpar.h"
#include "prelude.h"
#include "compression.h"
#include "lispbm_memory.h"
#include "env.h"
#include "extensions.h"

#define EVAL_CPS_STACK_SIZE 256

void *eval_thd_wrapper(void *v) {
  eval_cps_run_eval();
  return NULL;
}

uint32_t timestamp_callback() {
  struct timeval tv;
  gettimeofday(&tv,NULL);
  return (uint32_t)(tv.tv_sec * 1000000 + tv.tv_usec);
}

void sleep_callback(uint32_t us) {
  struct timespec s;
  struct timespec r;
  s.tv_sec = 0;
  s.tv_nsec = (long)us * 1000;
  nanosleep(&s, &r);
}

VALUE ext_even(VALUE *args, UINT argn) {

  if (argn < 1) return enc_sym(SYM_NIL);

  VALUE v = args[0];

  if (val_type(v) == VAL_TYPE_I ||
      val_type(v) == VAL_TYPE_U) {
    if (dec_i(v) % 2 == 0)
      return enc_sym(SYM_TRUE);
  }

  return enc_sym(SYM_NIL);
}

VALUE ext_odd(VALUE *args, UINT argn) {

  if (argn < 1) return enc_sym(SYM_NIL);

  VALUE v = args[0];

  if (val_type(v) == VAL_TYPE_I ||
      val_type(v) == VAL_TYPE_U) {
    if (dec_i(v) % 2 == 1)
      return enc_sym(SYM_TRUE);
  }

  return enc_sym(SYM_NIL);
}



int main(int argc, char **argv) {

  int res = 0;

  unsigned int heap_size = 8 * 1024 * 1024;  // 8 Megabytes is standard
  bool growing_continuation_stack = false;
  bool compress_decompress = false;

  pthread_t lispbm_thd;
  cons_t *heap_storage = NULL;

  int c;
  opterr = 1;

  while (( c = getopt(argc, argv, "gch:")) != -1) {
    switch (c) {
    case 'h':
      heap_size = (unsigned int)atoi((char *)optarg);
      break;
    case 'g':
      growing_continuation_stack = true;
      break;
    case 'c':
      compress_decompress = true;
      break;
    case '?':
      break;
    default:
      break;
    }
  }
  printf("------------------------------------------------------------\n");
  printf("Heap size: %u\n", heap_size);
  printf("Growing stack: %s\n", growing_continuation_stack ? "yes" : "no");
  printf("Compression: %s\n", compress_decompress ? "yes" : "no");
  printf("------------------------------------------------------------\n");

  if (argc - optind < 1) {
    printf("Incorrect arguments\n");
    return 0;
  }

  printf("Opening file: %s\n", argv[optind]);

  FILE* fp = fopen(argv[optind], "r");

  if (fp == NULL) {
    printf("Error opening file\n");
    return 0;
  }

  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  if (size <= 0) {
    printf("Error file empty %s\n", argv[1]);
    return 0;
  }
  fseek(fp, 0, SEEK_SET);
  char *code_buffer = malloc((unsigned long)size * sizeof(char) + 1);
  size_t r = fread (code_buffer, 1, (unsigned int)size, fp);

  if (r == 0) {
    printf("Error empty file?\n");
    return 0;
  }

  uint32_t *memory = malloc(4 * MEMORY_SIZE_16K);
  if (memory == NULL) return 0;
  uint32_t *bitmap = malloc(4 * MEMORY_BITMAP_SIZE_16K);
  if (bitmap == NULL) return 0;


  res = memory_init(memory, MEMORY_SIZE_16K,
                    bitmap, MEMORY_BITMAP_SIZE_16K);
  if (res)
    printf("Memory initialized.\n");
  else {
    printf("Error initializing memory!\n");
    return 0;
  }

  res = symrepr_init();
  if (res)
    printf("Symrepr initialized.\n");
  else {
    printf("Error initializing symrepr!\n");
    return 0;
  }

  heap_storage = (cons_t*)malloc(sizeof(cons_t) * heap_size);
  if (heap_storage == NULL) {
    return 0;
  }

  res = heap_init(heap_storage, heap_size);
  if (res)
    printf("Heap initialized. Heap size: %f MiB. Free cons cells: %d\n", heap_size_bytes() / 1024.0 / 1024.0, heap_num_free());
  else {
    printf("Error initializing heap!\n");
    return 0;
  }

  res = eval_cps_init();
  if (res)
    printf("Evaluator initialized.\n");
  else {
    printf("Error initializing evaluator.\n");
    return 0;
  }

  res = env_init();
  if (res)
    printf("Environment initialized.\n");
  else {
    printf("Error initializing environment.\n");
    return 0;
  }

  res = extensions_add("ext-even", ext_even);
  if (res)
    printf("Extension added.\n");
  else {
    printf("Error adding extension.\n");
    return 0;
  }

   res = extensions_add("ext-odd", ext_odd);
  if (res)
    printf("Extension added.\n");
  else {
    printf("Error adding extension.\n");
    return 0;
  }

  eval_cps_set_timestamp_us_callback(timestamp_callback);
  eval_cps_set_usleep_callback(sleep_callback);

  if (pthread_create(&lispbm_thd, NULL, eval_thd_wrapper, NULL)) {
    printf("Error creating evaluation thread\n");
    return 1;
  }

  VALUE prelude = prelude_load();
  CID cid = eval_cps_program(prelude);

  eval_cps_wait_ctx(cid);

  VALUE t;

  if (compress_decompress) {
    uint32_t compressed_size = 0;
    char *compressed_code = compression_compress(code_buffer, &compressed_size);
    if (!compressed_code) {
      printf("Error compressing code\n");
      return 0;
    }
    char decompress_code[8192];
    compression_decompress(decompress_code, 8192, compressed_code);
    printf("\n\nDECOMPRESS TEST: %s\n\n", decompress_code);

    t = compression_parse(compressed_code);
    free(compressed_code);
  } else {
    t = tokpar_parse(code_buffer);
  }

  char output[128];

  res = print_value(output, 128, t);

  if ( res >= 0) {
    printf("I: %s\n", output);
  } else {
    printf("%s\n", output);
    return 0;
  }
  cid = eval_cps_program_ext(t,256,growing_continuation_stack);

  t = eval_cps_wait_ctx(cid);

  res = print_value(output, 128, t);

  if ( res >= 0) {
    printf("O: %s\n", output);
  } else {
    printf("%s\n", output);
    return 0;
  }

  if ( dec_sym(t) == SYM_EERROR) {
    res = 0;
  }


  if (res && type_of(t) == VAL_TYPE_SYMBOL && dec_sym(t) == SYM_TRUE){ // structural_equality(car(rest),car(cdr(rest)))) {
    printf("Test: OK!\n");
    res = 1;
  } else {
    printf("Test: Failed!\n");
    res = 0;
  }

  free(heap_storage);

  return res;
}
