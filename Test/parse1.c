
#include <stdlib.h>
#include <stdio.h>

#include "mpc.h"
#include "parse.h"

/* void grammar(void) { */

/*   mpc_parser_t *Atom, *SExp, *Program, *Value, *Name; */

/*   Atom = mpc_new("atom"); */
/*   SExp = mpc_new("sexp"); */
/*   Program = mpc_new("program"); */
/*   Value = mpc_new("value"); */
/*   Name = mpc_new("name"); */

/*   mpca_lang(MPCA_LANG_DEFAULT, */
/* 	    "atom      : <value> | <name> ;" */
/* 	    "sexp      : <atom>  | '(' <sexp>+ ')' ;" */
/* 	    "program   : <sexp>+ ;" */
/* 	    "value     : /[0-9]+/;" */
/* 	    "name      : /[a-zA-Z+\\*\\-\\/?><=]+[a-zA-Z0-9+\\-\\*\\\/?><=]*\/;", */
/* 	    Atom, SExp, Program, Value, Name); */

/*   char *input = malloc(1024);  */
  
/*    while (1) { */
  
/*      int len; */
/*      getline(&input,&len,stdin);  */

/*      printf("You entered: %s\n", input);  */
     
/*     /\* Attempt to parse the user input *\/ */
/*     mpc_result_t r; */
/*     if (mpc_parse("<stdin>", input, Program, &r)) { */
/*       /\* On success print and delete the AST *\/ */
    
/*     } else { */
/*       /\* Otherwise print and delete the Error *\/ */
/*       mpc_err_print(r.error); */
/*       mpc_err_delete(r.error); */
/*     } */
/*   } */
  
/*   /\* Undefine and delete our parsers *\/ */
/*    mpc_cleanup(5, Atom, SExp, Program, Value, Name); */
/* } */

 

int main(int argc, char **argv) {
  char *str = malloc(1024);;
  size_t len;

  mpc_ast_t* ast = NULL; 

  printf("OK SO FAR\n"); 
  init_parser();

  
  while (1) {
    
    getline(&str,&len,stdin);

    ast = parse_string(str); 
    if (!ast) {
      printf("ERROR!\n");
      break;
    }
    mpc_ast_print(ast);
    mpc_ast_delete(ast);
  }
  
  destroy_parser(); 
  return 0;
  
}
