/*
  Plus 1 parallel output / Plus 1 emulation.
  Copyright (C) 2019, David Boddie
  Part of Elkulator by Sarah Walker.
*/
#include "elk.h"

void writeparallel(uint8_t val)
{
    fputc(val, stdout);
    fflush(stdout);
}
