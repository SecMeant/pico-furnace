#include <stdio.h>

#include "basic_defines.h"

static void
prepare(FILE* fptr)
{
  fprintf(fptr, "#pragma once\n\n");
}

int
main()
{
  FILE* fptr = fopen(CONSTEVAL_HEADER, "w");
  prepare(fptr);
  fclose(fptr);
}
