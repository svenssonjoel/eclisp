/*
    Copyright 2018 Joel Svensson	svenssonjoel@yahoo.se

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

#include "eval.h"

#include "symrepr.h"
#include "heap.h"
#include "builtin.h"
#include "print.h"
#include "env.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#define EVAL_ERROR_BUFFER_SIZE 2048
#define ERROR(...) do {\
  memset(eval_error_string, 0, EVAL_ERROR_BUFFER_SIZE);\
  snprintf(eval_error_string,EVAL_ERROR_BUFFER_SIZE, __VA_ARGS__);	\
  if (error_jmp_ptr){\
    longjmp(*error_jmp_ptr,1);\
  }\
  } while(0)

static val_t evlis(val_t ptcons, val_t env);
static val_t apply(val_t closure, val_t args); 
static val_t apply_builtin(val_t sym, val_t args); 
static val_t eval_in_env(val_t lisp, val_t env); 
static val_t eval_let_bindings(val_t bind_list, val_t env);

static char eval_error_string[EVAL_ERROR_BUFFER_SIZE]; 

static jmp_buf error_jmp_buffer;
static jmp_buf *error_jmp_ptr = NULL; 

static val_t eval_global_env;

static val_t NIL;

char *eval_get_error() {
  return eval_error_string;
}

val_t eval_get_env(void) {
  return eval_global_env;
}

val_t eval_bi(val_t lisp) {

  if (!error_jmp_ptr) {
    error_jmp_ptr = &error_jmp_buffer;

    if (setjmp(*error_jmp_ptr)) {
      error_jmp_ptr = 0;
      return enc_sym(symrepr_eerror()); 
    }
  }
  
  return eval_in_env(car(lisp),NIL);
  
}


int eval_init() {

  if (!builtin_add_function("eval", eval_bi)) return 0; 
 
  eval_global_env = built_in_gen_env();
  if (type_of(eval_global_env) == VAL_TYPE_SYMBOL) return 0; 

  NIL = enc_sym(symrepr_nil());
  
  val_t nil_entry = cons(NIL,NIL);
  if (type_of(nil_entry) == VAL_TYPE_SYMBOL) return 0;
  
  eval_global_env = cons(nil_entry, eval_global_env);
  if (type_of(eval_global_env) == VAL_TYPE_SYMBOL) return 0;

  return 1;  
}

val_t do_eval_program(val_t lisp) {
   
  // Program is a list of expressions that should be evaluated individually
  // The result of evaluating the last expression is the result of the program.
  
  val_t res = NIL; 
  val_t local_env = NIL;
  val_t curr = lisp;
  
  while ( type_of(curr) == PTR_TYPE_CONS) {

    res = eval_in_env(car(curr), local_env);
    
    curr = cdr(curr);
  }
  return res;
}

val_t eval_program(val_t lisp) {

  // Setup jmp buffer for breaking out of recursion on error.
  if (!error_jmp_ptr) {
    error_jmp_ptr = &error_jmp_buffer; 

    if (setjmp(*error_jmp_ptr)) {
      error_jmp_ptr = 0;
      return enc_sym(symrepr_eerror());
    }
  }
  
  val_t res = do_eval_program(lisp);
  
  if (error_jmp_ptr) error_jmp_ptr = 0;
  return res; 
}

val_t eval_in_env(val_t lisp, val_t env) {

  val_t tmp = 0;
  int ret;
  val_t head;
  
  switch(type_of(lisp)) {
  case VAL_TYPE_SYMBOL:
    ret = env_lookup(lisp, env, &tmp);
    if (!ret) {
      ret = env_lookup(lisp, eval_global_env, &tmp);
    }
    if (ret) return tmp;
    ERROR("Eval: Variable lookup failed: %s ",symrepr_lookup_name(dec_sym(lisp)));
    break;
  case VAL_TYPE_I28:
  case VAL_TYPE_U28:
  case VAL_TYPE_CHAR:
    return lisp;    
    break;
  case PTR_TYPE_CONS:
    head = car(lisp);

    if (type_of(head) == VAL_TYPE_SYMBOL) {

      // Special form: QUOTE
      if (dec_sym(head) == symrepr_quote()) {

	return (car (cdr (lisp)));
      }

      // Special form: LAMBDA
      if (dec_sym(head) == symrepr_lambda()) {

	val_t env_cpy;
	if (!env_copy_shallow(env, &env_cpy))
	  ERROR("OUT OF MEMORY"); 
	return cons(enc_sym(symrepr_closure()),
		    cons(car(cdr(lisp)),
			 cons(car(cdr(cdr(lisp))),
			      cons(env_cpy, NIL))));
      }

      // Special form: IF
      if (dec_sym(head) == symrepr_if()) {

	val_t pred_res = eval_in_env(car(cdr(lisp)), env);
	if (val_type(pred_res) == VAL_TYPE_SYMBOL &&
	    dec_sym(pred_res) == symrepr_true()) {
	  return eval_in_env(car(cdr(cdr(lisp))), env);
	} else {
	  // TODO: CHECK THAT IS NOT A PROGRAMMER ERROR
	  return eval_in_env(car(cdr(cdr(cdr(lisp)))), env);
	}
      }

      // Special form: DEFINE
      if (dec_sym(head) == symrepr_define()) {
	val_t key = car(cdr(lisp));
	val_t val = eval_in_env(car(cdr(cdr(lisp))), env);
	val_t curr = eval_global_env; 

	if (type_of(key) != VAL_TYPE_SYMBOL)
	  ERROR("Define expects a symbol");

	if (key == NIL)
	  ERROR("Cannot redefine nil");

	while(type_of(curr) == PTR_TYPE_CONS) {
	  if (car(car(curr)) == key) {
	    set_cdr(car(curr),val);
	    return enc_sym(symrepr_true());
	  }
	  curr = cdr(curr);
	}
	val_t keyval = cons(key,val);
	eval_global_env = cons(keyval,eval_global_env);
	return enc_sym(symrepr_true()); 	
      }

      // Special form: LET
      if (dec_sym(head) == symrepr_let()) {

	val_t new_env = eval_let_bindings(car(cdr(lisp)),env);
	return eval_in_env(car(cdr(cdr(lisp))),new_env);
      }

    } // If head is symbol
    
    // Possibly an application form:

    val_t head_val = eval_in_env(head, env); 
  
    if (type_of(head_val) == VAL_TYPE_SYMBOL) {
      return apply_builtin(head_val, evlis(cdr(lisp),env));
    }
    
    // Possibly a closure application (or programmer error)
    return apply(head_val, evlis(cdr(lisp), env));
    
    break;
  case PTR_TYPE_I32:
  case PTR_TYPE_U32:
  case PTR_TYPE_F32:
    return lisp;
    break;
  case PTR_TYPE_ARRAY:
    return lisp;
    break;
  case PTR_TYPE_REF:
  case PTR_TYPE_STREAM:
    ERROR("Arrays, refs and streams not implemented");
    break;
  default:
    ERROR("BUG! Fell through all cases in eval.");
    break;
  }

  ERROR("BUG! This cannot happen!");
  return enc_sym(symrepr_eerror());
} 

static val_t apply(val_t closure, val_t args) {

  // TODO: error checking etc
  //val_t clo_sym = car(closure);      
  val_t params  = car(cdr(closure)); // parameter list
  val_t exp     = car(cdr(cdr(closure)));
  val_t clo_env = car(cdr(cdr(cdr(closure))));

  val_t local_env;
  if (!env_build_params_args(params, args, clo_env, &local_env))
    ERROR("Could not create local environment"); 
  //printf("CLOSURE ENV: "); simple_print(local_env); printf("\n"); 
  
  return eval_in_env(exp,local_env);
}

// takes a ptr to cons and returns a ptr to cons.. 
static val_t evlis(val_t pcons, val_t env) {

  if (type_of(pcons) == PTR_TYPE_CONS) { 
    return cons(eval_in_env(car(pcons), env),
		evlis(cdr(pcons),env)); 
  }

  if (type_of(pcons) == VAL_TYPE_SYMBOL &&
      pcons == NIL) {
    return NIL;
  }

  ERROR("Evlis argument is not a list"); 
  return enc_sym(symrepr_eerror());
}

static val_t eval_let_bindings(val_t bind_list, val_t env) {

  val_t new_env = env;
  val_t curr = bind_list; 
  int res; 
  
  //setup the bindings
  while (type_of(curr) == PTR_TYPE_CONS) { 
    val_t key = car(car(curr));
    val_t val = NIL; // a temporary
    val_t binding = cons(key,val);
    new_env = cons(binding, new_env); 
    curr = cdr(curr); 
  }

  // evaluate the bodies
  curr = bind_list; 
  while (type_of(curr) == PTR_TYPE_CONS) {
    val_t key = car(car(curr));
    val_t val = eval_in_env(car(cdr(car(curr))),new_env);

    res = env_modify_binding(new_env, key, val);
    if (!res) ERROR("Unable to modify letrec bindings");
    curr = cdr(curr); 
  }

  return new_env;
}


static val_t apply_builtin(val_t sym, val_t args) {

  val_t (*f)(val_t) = builtin_lookup_function(dec_sym(sym));

  if (f == NULL) {
    ERROR("Built in function does not exist"); 
    //return enc_sym(symrepr_eerror());
  }

  return f(args); 
}
