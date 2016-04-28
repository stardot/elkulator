/*Elkulator v1.0 by Tom Walker
  Savestate handling*/
#include <stdio.h>
#include "elk.h"

void savestate()
{
        wantsavestate=1;
}

void loadstate()
{
        wantloadstate=1;
}

void dosavestate()
{
        FILE *f=fopen(ssname,"wb");
        putc('E',f); putc('L',f); putc('K',f); putc('S',f);
        putc('N',f); putc('A',f); putc('P',f); putc('1',f);

        putc(turbo,f);
        putc(mrb,f);
        putc(mrbmode,f);
        putc(usedrom6,f);
        
        putc(plus3,f);
        putc(adfsena,f);
        putc(dfsena,f);
        putc(0,f);
        
        putc(sndex,f);
        putc(plus1,f);
        putc(firstbyte,f); putc(0,f);
        
        save6502state(f);
        saveulastate(f);
        savememstate(f);
        
        fclose(f);
        
        wantsavestate=0;
}
        
void doloadstate()
{
        int c;
        FILE *f=fopen(ssname,"rb");
        for (c=0;c<8;c++) getc(f);
        
        turbo=getc(f);
        mrb=getc(f);
        mrbmode=getc(f);
        usedrom6=getc(f);
        
        plus3=getc(f);
        adfsena=getc(f);
        dfsena=getc(f);
        getc(f);
        
        sndex=getc(f);
        plus1=getc(f);
        firstbyte=getc(f);
        getc(f);
        
        load6502state(f);
        loadulastate(f);
        loadmemstate(f);
        
        fclose(f);
        
        wantloadstate=0;
}
