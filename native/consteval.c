#include <stdio.h>

#include "common.h"

static void
prepare(FILE* fptr)
{
  fprintf(fptr, "#pragma once\n\n");
}

static void
calculate_status_size(FILE* fptr)
{
  const size_t size = snprintf(0, 0, FORMAT_STATUS_FMT, MAX_TEMP, MAX_TEMP, MAX_PWM, MAX_PWM, MAX_AUTO) + 1;
  fprintf(fptr, "#define FORMAT_STATUS_SIZE %u\n", size);
}

#if CONFIG_PWM_MAPPER
static void
calculate_mapper_size(FILE* fptr)
{
  const size_t size = snprintf(0, 0, MAPPER_STATUS_FMT, MAX_PWM, MAX_TEMP) + 1;
  fprintf(fptr, "#define MAPPER_STATUS_SIZE %u\n", size);
}
#endif

int
main()
{
  FILE* fptr = fopen(CONSTEVAL_HEADER, "w");
  prepare(fptr);
  calculate_status_size(fptr);
  calculate_mapper_size(fptr);
  fclose(fptr);
}
