/*Elkulator v1.0 by Tom Walker
  Movie export by David Boddie*/

#include <allegro.h>
#include <stdio.h>
#include <png.h>
#include "elk.h"

int wantmovieframe=0;
FILE *moviefile;
BITMAP *moviebitmap;

uint16_t sndstreambuf[626];
int sndstreamindex = 0;
int sndstreamcount = 0;
int movieframes;

png_structp png_ptr;
png_infop png_info_ptr;
png_bytep png_row;

/* Record positions in the file that need to be filled in before it is closed. */
long moviepos[4];

void writeword(unsigned int v)
{
    fputc(v & 0xff, moviefile);
    fputc((v >> 8) & 0xff, moviefile);
    fputc((v >> 16) & 0xff, moviefile);
    fputc((v >> 24) & 0xff, moviefile);
}

void stopmovie()
{
    int i;
    wantmovieframe = 0;
    if (moviefile != NULL) {

        /* Write the file length to the file. */
        long length = ftell(moviefile);
        fseek(moviefile, 4, SEEK_SET);
        writeword(length);

        /* Write the number of frames to the relevant places in the file. */
        for (i = 0; i < 3; ++i) {
            fseek(moviefile, moviepos[i], SEEK_SET);
            writeword(movieframes);
        }

        /* Write the length of the "movi" list. */
        fseek(moviefile, moviepos[3], SEEK_SET);
        writeword(length - moviepos[3] - 4);

        fclose(moviefile);
        destroy_bitmap(moviebitmap);

        free(png_row);

        moviefile = NULL;
    }
}

void startmovie()
{
    stopmovie();

    moviefile = fopen(moviename, "wb");
    if (moviefile == NULL)
        return;

    moviebitmap=create_bitmap_ex(24, 640, 256);
    wantmovieframe = 1;
    sndstreamindex = 0;
    sndstreamcount = 0;

    /* Write the stream header. */
    fprintf(moviefile, "RIFF");
    writeword(0);
    fprintf(moviefile, "AVI ");

    fprintf(moviefile, "LIST");
    writeword(0x124);           /* Length of list */
    fprintf(moviefile, "hdrl");

        fprintf(moviefile, "avih");
        writeword(0x38);            /* Length of chunk */
        writeword(20000);           /* Time per frame (in microseconds) */
        writeword(0);
        writeword(0);
        writeword(0);
        moviepos[0] = ftell(moviefile);
        writeword(0);               /* Number of frames - fill in later */
        writeword(0);               /* Initial frame */
        writeword(2);               /* Number of streams - audio and video */
        writeword(0);
        writeword(640);             /* Frame width */
        writeword(512);             /* Frame height */
        writeword(0);
        writeword(0);
        writeword(0);
        writeword(0);

        fprintf(moviefile, "LIST");
        writeword(0x74);                /* Length of list */
        fprintf(moviefile, "strl");

            fprintf(moviefile, "strh");
            writeword(0x38);            /* Length of chunk */
            fprintf(moviefile, "vids");
            fprintf(moviefile, "MPNG");
            writeword(0);               /* Flags */
            writeword(0);               /* Priority and language */
            writeword(0);               /* Initial frame */
            writeword(1);               /* Time scale */
            writeword(50);              /* Frames per second */
            writeword(0);               /* Starting time */
            moviepos[1] = ftell(moviefile);
            writeword(0);               /* Length - fill in later */
            writeword(0);
            writeword(0);
            writeword(0);
            writeword(0);
            writeword(0);

            fprintf(moviefile, "strf");
            writeword(0x28);            /* Length of chunk */
            writeword(0);
            writeword(0);
            writeword(0);
            writeword(0x00180001);      /* 1 bit plane, 24 bits per pixel */
            fprintf(moviefile, "MPNG");
            writeword(0);
            writeword(0);
            writeword(0);
            writeword(0);
            writeword(0);

        fprintf(moviefile, "LIST");
        writeword(0x5c);                /* Length of list */
        fprintf(moviefile, "strl");

            fprintf(moviefile, "strh");
            writeword(0x38);            /* Length of chunk */
            fprintf(moviefile, "auds");
            writeword(0);
            writeword(0);               /* Flags */
            writeword(0);               /* Priority and language */
            writeword(0);               /* Initial frame */
            writeword(1);               /* Time scale */
            writeword(50);              /* Frames per second */
            writeword(0);               /* Starting time */
            moviepos[2] = ftell(moviefile);
            writeword(0);               /* Length - fill in later */
            writeword(0);
            writeword(0);
            writeword(0);
            writeword(0);
            writeword(0);

            fprintf(moviefile, "strf");
            writeword(0x10);            /* Length of chunk */
            writeword(0x00010001);      /* Format (PCM), channels (1) */
            writeword(31250);           /* Sample rate */
            writeword(31250);           /* Sample rate */
            writeword(0x00100002);      /* Block align (channels * bits)/8 (2),
                                           bits  (16) */

    fprintf(moviefile, "JUNK");
    writeword(0x800 - ftell(moviefile) - 4);

    int i = ftell(moviefile);
    while (i < 0x800) {
        writeword(0x0);
        i += 4;
    }

    fprintf(moviefile, "LIST");
    moviepos[3] = ftell(moviefile);
    writeword(0x0);                 /* Length of list - fill in later */
    fprintf(moviefile, "movi");

    png_row = (png_bytep) malloc(3 * 640 * sizeof(png_byte));

    movieframes = 0;
}

int write_png()
{
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) {
        fclose(moviefile);
        moviefile = NULL;
        return 1;
    }

    png_info_ptr = png_create_info_struct(png_ptr);
    if (png_info_ptr == NULL) {
        fclose(moviefile);
        png_destroy_write_struct(&png_ptr, png_infopp_NULL);
        moviefile = NULL;
        return 2;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_free_data(png_ptr, png_info_ptr, PNG_FREE_ALL, -1);
        png_destroy_write_struct(&png_ptr, png_infopp_NULL);
        return 3;
    }

    png_init_io(png_ptr, moviefile);

    png_set_IHDR(png_ptr, png_info_ptr, 640, 512, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE,
                 PNG_FILTER_TYPE_BASE);

    png_write_info(png_ptr, png_info_ptr);

    int y;
    for (y = 0; y < 256; ++y) {
        memcpy(png_row, (png_byte *)(moviebitmap->dat + (y * 640 * 3)), 640 * 3);
        png_write_row(png_ptr, png_row);
        png_write_row(png_ptr, png_row);
    }

    png_write_end(png_ptr, NULL);

    png_free_data(png_ptr, png_info_ptr, PNG_FREE_ALL, -1);
    png_destroy_write_struct(&png_ptr, png_infopp_NULL);

    return 0;
}

void saveframe(BITMAP *b)
{
    if (moviefile == NULL)
        return;

    int start;
    if (sndstreamcount == 624) {
        /* Take the last 625 samples. */
        start = (sndstreamindex + 1) % sizeof(sndstreambuf);
    } else if (sndstreamcount == 626) {
        /* Take the first 625 samples from the 626 obtained and leave the last
           one for the next frame. */
        start = sndstreamindex;
    }

    fprintf(moviefile, "LIST");
    int listptr = ftell(moviefile);
    writeword(0);                   /* Length of the list - to be filled in */
    fprintf(moviefile, "rec ");

    fprintf(moviefile, "01wb");
    writeword(625*2);               /* Length of chunk */

    int remaining = sizeof(sndstreambuf) - start;
    if (remaining >= 625)
        fwrite(&sndstreambuf[start], 2, 625, moviefile);
    else {
        fwrite(&sndstreambuf[start], 2, remaining, moviefile);
        fwrite(sndstreambuf, 2, 625 - remaining, moviefile);
    }

    fprintf(moviefile, "00dc");
    int dcptr = ftell(moviefile);
    writeword(0);                   /* Length of chunk  - to be filled in */

    blit(b,moviebitmap,0,0,0,0,640,256);
    if (write_png() != 0)
    {
        fclose(moviefile);
        moviefile = NULL;
        wantmovieframe = 0;
        return;
    }

    if (ftell(moviefile) % 2 != 0)
        fputc(0, moviefile);

    /* Fill in the fields in the AVI file structure. */
    int afterptr = ftell(moviefile);
    fseek(moviefile, listptr, SEEK_SET);
    writeword(afterptr - listptr - 4);
    fseek(moviefile, dcptr, SEEK_SET);
    writeword(afterptr - dcptr - 4);
    fseek(moviefile, afterptr, SEEK_SET);

    sndstreamcount = 0;
    movieframes++;
}
