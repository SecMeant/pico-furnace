#include <stdio.h>

#include "max318xx.h"

int max318xx_init(void)
{
#if CONFIG_THERMO > CONFIG_THERMO_NONE
  max318xx_config();

  return max318xx_sanity_check();
#endif
  return -1;
}

