#include <stdio.h>

#include "basic_defines.h"

static void
prepare(FILE* fptr)
{
  fprintf(fptr, "#pragma once\n\n");
}

static void
calculate_status_size(FILE* fptr)
{
  const size_t size = snprintf(0, 0, FORMAT_STATUS_FMT, MAX_TEMP, MAX_PWM, MAX_PWM);
  fprintf(fptr, "#define FORMAT_STATUS_SIZE %u\n", size);
}

int
main()
{
  FILE* fptr = fopen(CONSTEVAL_HEADER, "w");
  prepare(fptr);
  calculate_status_size(fptr);
  fclose(fptr);
}
