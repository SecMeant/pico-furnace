#include <stdio.h>

#include "max318xx.h"

int max318xx_init(void)
{
  max318xx_config();

  return max318xx_sanity_check();
}

