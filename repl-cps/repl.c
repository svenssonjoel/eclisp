/*
    Copyright 2018, 2021 Joel Svensson  svenssonjoel@yahoo.se

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
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>

#include "lispbm.h"

#define EVAL_CPS_STACK_SIZE 256

static volatile bool allow_print = true;

struct termios old_termios;
struct termios new_termios;

void setup_terminal(void) {

  tcgetattr(0,&old_termios);
  new_termios = old_termios;
  //new_termios.c_iflag;                     // INPUT MODES
  //new_termios.c_oflag;                     // OUTPUT MODES
  //new_termios.c_cflag;                     // CONTROL MODES
  // LOCAL MODES
  new_termios.c_lflag &= (tcflag_t) ~(ICANON  | ISIG | ECHO);
  new_termios.c_cc[VMIN] = 0;
  new_termios.c_cc[VTIME] = 0;
  //new_termios.c_cc;                       // SPECIAL CHARACTERS

  // LOCAL MODES
  // Turn off:
  //  - canonical mode
  //  - Signal generation for certain characters (INTR, QUIT, SUSP, DSUSP)
  //  VMIN:  Minimal number of characters for noncanonical read.
  //  VTIME: Timeout in deciseconds for noncanonical read.

  tcsetattr(0, TCSANOW, &new_termios);

}

void restore_terminal(void) {
  tcsetattr(0, TCSANOW, &old_termios);
}


int inputline(char *buffer, unsigned int size) {
  int n = 0;
  int c;
  for (n = 0; n < size - 1; n++) {

    c = getchar(); // busy waiting.

    if (c < 0) {
      n--;
      struct timespec s;
      struct timespec r;
      s.tv_sec = 0;
      s.tv_nsec = (long)1000 * 1000;
      nanosleep(&s, &r);
      continue;
    }
    switch (c) {
    case 127: /* fall through to below */
    case '\b': /* backspace character received */
      if (n > 0)
        n--;
      buffer[n] = 0;
      //putchar(0x8); /* output backspace character */
      //putchar(' ');
      //putchar(0x8);
      n--; /* set up next iteration to deal with preceding char location */
      break;
    case '\n': /* fall through to \r */
    case '\r':
      buffer[n] = 0;
      return n;
    default:
      if (isprint(c)) { /* ignore non-printable characters */
        //putchar(c);
        buffer[n] = (char)c;
      } else {
        n -= 1;
      }
      break;
    }
  }
  buffer[size - 1] = 0;
  return 0; // Filled up buffer without reading a linebreak
}

void *eval_thd_wrapper(void *v) {

  eval_cps_run_eval();

  return NULL;
}

void done_callback(eval_context_t *ctx) {

  char output[1024];

  CID cid = ctx->id;
  VALUE t = ctx->r;

  int print_ret = print_value(output, 1024, t);

  if (print_ret >= 0) {
    printf("<< Context %d finished with value %s >>\n", cid, output);
  } else {
    printf("<< Context %d finished with value %s >>\n", cid, output);
  }

  //  if (!eval_cps_remove_done_ctx(cid, &t)) {
  //   printf("Error: done context (%d)  not in list\n", cid);
  //}
  fflush(stdout);

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


VALUE ext_print(VALUE *args, UINT argn) {
  if (argn < 1) return enc_sym(SYM_NIL);

  if (!allow_print) return enc_sym(SYM_TRUE);

  char output[1024];

  for (int i = 0; i < argn; i ++) {
    VALUE t = args[i];

    if (is_ptr(t) && ptr_type(t) == PTR_TYPE_ARRAY) {
      array_header_t *array = (array_header_t *)car(t);
      switch (array->elt_type){
      case VAL_TYPE_CHAR: {
        char *data = (char*)array + 8;
        printf("%s", data);
        break;
      }
      default:
        return enc_sym(SYM_NIL);
        break;
      }
    } else if (val_type(t) == VAL_TYPE_CHAR) {
      printf("%c", dec_char(t));
    } else {
      print_value(output, 1024, t);
      printf("%s", output);
    }
  }
  return enc_sym(SYM_TRUE);
}

char output[128];

static VALUE ext_range(VALUE *args, UINT argn) {
        if (argn != 2 || type_of(args[0]) != VAL_TYPE_I || type_of(args[1]) != VAL_TYPE_I) {
                return enc_sym(SYM_EERROR);
        }

        INT start = dec_i(args[0]);
        INT end = dec_i(args[1]);

        if (start > end || (end - start) > 100) {
                return enc_sym(SYM_EERROR);
        }

        VALUE res = enc_sym(SYM_NIL);

        for (INT i = end;i >= start;i--) {
                res = cons(enc_i(i), res);
        }

        return res;
}


static VALUE ext_get_bms_val(VALUE *args, UINT argn) {
        VALUE res = enc_sym(SYM_EERROR);

        if (argn != 1 && argn != 2) {
                return enc_sym(SYM_EERROR);
        }

        char *name = dec_str(args[0]);

        if (!name) {
                return enc_sym(SYM_EERROR);
        }

        res = enc_i(20);
        return res;
}


/* load a file, caller is responsible for freeing the returned string */
char * load_file(char *filename) {
  char *file_str = NULL;
  //size_t str_len = strlen(filename);
  //filename[str_len-1] = 0;
  int i = 0;
  while (filename[i] == ' ' && filename[i] != 0) {
    i ++;
  }
  FILE *fp;
  printf("filename: %s\n", &filename[i]);

  if (strlen(&filename[i]) > 0) {
    errno = 0;
    fp = fopen(&filename[i], "r");
    if (!fp) {
      return NULL;
    }
    long fsize_long;
    unsigned int fsize;
    fseek(fp, 0, SEEK_END);
    fsize_long = ftell(fp);
    if (fsize_long <= 0) {
      return NULL;
    }
    fsize = (unsigned int) fsize_long;
    fseek(fp, 0, SEEK_SET);
    file_str = malloc(fsize+1);
    memset(file_str, 0 , fsize+1);
    if (fread(file_str,1,fsize,fp) != fsize) {
      free(file_str);
      file_str = NULL;
    }
    fclose(fp);
  }
  return file_str;
}


void print_ctx_info(eval_context_t *ctx, void *arg1, void *arg2) {
  (void) arg1;
  (void) arg2;

  char output[1024];

  int print_ret = print_value(output, 1024, ctx->r);

  printf("--------------------------------\n");
  printf("ContextID: %u\n", ctx->id);
  printf("Stack SP: %u\n",  ctx->K.sp);
  printf("Stack SP max: %u\n", ctx->K.max_sp);
  if (print_ret) {
    printf("Value: %s\n", output);
  } else {
    printf("Error: %s\n", output);
  }
}

void ctx_exists(eval_context_t *ctx, void *arg1, void *arg2) {

  CID id = *(CID*)arg1;
  bool *exists = (bool*)arg2;

  if (ctx->id == id) {
    *exists = true;
  }
}

static uint32_t memory[MEMORY_SIZE_8K];
static uint32_t bitmap[MEMORY_BITMAP_SIZE_8K];

int main(int argc, char **argv) {
  char str[1024];
  unsigned int len = 1024;
  int res = 0;

  pthread_t lispbm_thd;

  heap_state_t heap_state;
  unsigned int heap_size = 2048;
  cons_t *heap_storage = NULL;

  //setup_terminal();

  heap_storage = (cons_t*)malloc(sizeof(cons_t) * heap_size);
  if (heap_storage == NULL) {
    return 0;
  }

  lispbm_init(heap_storage, heap_size,
              memory, MEMORY_SIZE_8K,
              bitmap, MEMORY_BITMAP_SIZE_8K);

  eval_cps_set_ctx_done_callback(done_callback);
  eval_cps_set_timestamp_us_callback(timestamp_callback);
  eval_cps_set_usleep_callback(sleep_callback);

  res = extensions_add("print", ext_print);
  if (res)
    printf("Extension added.\n");
  else
    printf("Error adding extension.\n");


  res = extensions_add("get-bms-val", ext_get_bms_val);
  if (res)
    printf("Extension added.\n");
  else
    printf("Error adding extension.\n");

  res = extensions_add("range", ext_range);
  if (res)
    printf("Extension added.\n");
  else
    printf("Error adding extension.\n");


  /* Start evaluator thread */
  if (pthread_create(&lispbm_thd, NULL, eval_thd_wrapper, NULL)) {
    printf("Error creating evaluation thread\n");
    return 1;
  }

  printf("Lisp REPL started!\n");
  printf("Type :quit to exit.\n");
  printf("     :info for statistics.\n");
  printf("     :load [filename] to load lisp source.\n");

  char output[1024];

  while (1) {
    fflush(stdin);
    printf("# ");
    memset(str, 0 ,len);

    ssize_t n = inputline(str,len);
    fflush(stdout);

    if (n >= 5 && strncmp(str, ":info", 5) == 0) {
      printf("--(LISP HEAP)-----------------------------------------------\n");
      heap_get_state(&heap_state);
      printf("Heap size: %u Bytes\n", heap_size * 8);
      printf("Used cons cells: %d\n", heap_size - heap_num_free());
      printf("Free cons cells: %d\n", heap_num_free());
      printf("GC counter: %d\n", heap_state.gc_num);
      printf("Recovered: %d\n", heap_state.gc_recovered);
      printf("Recovered arrays: %u\n", heap_state.gc_recovered_arrays);
      printf("Marked: %d\n", heap_state.gc_marked);
      printf("--(Symbol and Array memory)---------------------------------\n");
      printf("Memory size: %u Words\n", memory_num_words());
      printf("Memory free: %u Words\n", memory_num_free());
      printf("Allocated arrays: %u\n", heap_state.num_alloc_arrays);
      printf("Symbol table size: %u Bytes\n", symrepr_size());
    }  else if (strncmp(str, ":env", 4) == 0) {
      VALUE curr = *env_get_global_ptr();
      printf("Environment:\r\n");
      while (type_of(curr) == PTR_TYPE_CONS) {
        res = print_value(output,1024, car(curr));
        curr = cdr(curr);
        printf("  %s\r\n",output);
      }
    }else if (n >= 5 && strncmp(str, ":load", 5) == 0) {
      char *file_str = load_file(&str[5]);
      if (file_str) {

        /* Get exclusive access to the heap */
        eval_cps_pause_eval();
        while(eval_cps_current_state() != EVAL_CPS_STATE_PAUSED) {
          sleep_callback(10);
        }

        VALUE f_exp = tokpar_parse(file_str);
        free(file_str);
        CID cid1 = eval_cps_program(f_exp);

        eval_cps_continue_eval();

        printf("started ctx: %u\n", cid1);
      }
    } else if (n >= 4 && strncmp(str, ":pon", 4) == 0) {
      allow_print = true;
      continue;
    } else if (n >= 5 && strncmp(str, ":poff", 5) == 0) {
      allow_print = false;
      continue;
    } else if (strncmp(str, ":ctxs", 5) == 0) {
      printf("****** Running contexts ******\n");
      eval_cps_running_iterator(print_ctx_info, NULL, NULL);
      printf("****** Blocked contexts ******\n");
      eval_cps_blocked_iterator(print_ctx_info, NULL, NULL);
      printf("****** Done contexts ******\n");
      eval_cps_done_iterator(print_ctx_info, NULL, NULL);
    } else if (strncmp(str, ":wait", 5) == 0) {
      int id = atoi(str + 5);
      bool exists = false;
      eval_cps_done_iterator(ctx_exists, (void*)&id, (void*)&exists);
      if (exists) {
        eval_cps_wait_ctx((CID)id);
      }
    } else if (n >= 5 && strncmp(str, ":quit", 5) == 0) {
      break;
    } else if (strncmp(str, ":reset", 6) == 0) {
      eval_cps_pause_eval();
      while(eval_cps_current_state() != EVAL_CPS_STATE_PAUSED) {
        sleep_callback(10);
      }

      lispbm_init(heap_storage, heap_size,
                  memory, MEMORY_SIZE_8K,
                  bitmap, MEMORY_BITMAP_SIZE_8K);

      extensions_add("print", ext_print);
    } else if (strncmp(str, ":prelude", 8) == 0) {

      eval_cps_pause_eval();
      while(eval_cps_current_state() != EVAL_CPS_STATE_PAUSED) {
        sleep_callback(10);
      }

      printf("Parsing prelude.\n");
      VALUE prelude = prelude_load();

      printf("Evaluate prelude.\n");
      eval_cps_program(prelude);

      printf("Eval resuming.\n");
      eval_cps_continue_eval();
    } else {
      VALUE t;

      /* Get exclusive access to the heap */
      eval_cps_pause_eval();
      while(eval_cps_current_state() != EVAL_CPS_STATE_PAUSED) {
        sleep_callback(10);
      }

      t = tokpar_parse(str);

      CID cid = eval_cps_program(t);

      eval_cps_continue_eval();

      printf("started ctx: %u\n", cid);
    }
  }
  free(heap_storage);

  //restore_terminal();

  return 0;
}
