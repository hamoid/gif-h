#include "gif.h"

// Compare an already paletted frame to the previous one.
// nextFrame8 is 8-bit, lastFrame and outFrame are 32-bit.
void GifDeltaImage( const uint8_t * lastFrame, const uint8_t * nextFrame8,
                    uint8_t * outFrame, uint32_t width, uint32_t height,
                    const GifPalette * pPal ) {
    uint32_t numPixels = width * height;

    for( uint32_t ii = 0; ii < numPixels; ++ii ) {
        int ind = nextFrame8[ii];

        // if a previous color is available, and it matches the current color,
        // set the pixel to transparent
        if(lastFrame &&
                lastFrame[0] == pPal->r[ind] &&
                lastFrame[1] == pPal->g[ind] &&
                lastFrame[2] == pPal->b[ind]) {
            outFrame[0] = lastFrame[0];
            outFrame[1] = lastFrame[1];
            outFrame[2] = lastFrame[2];
            outFrame[3] = kGifTransIndex;
        } else {
            outFrame[0] = pPal->r[ind];
            outFrame[1] = pPal->g[ind];
            outFrame[2] = pPal->b[ind];
            outFrame[3] = ind;
        }

        if(lastFrame) {
            lastFrame += 4;
        }
        outFrame += 4;
    }
}

// insert a single bit
void GifWriteBit( GifBitStatus & stat, uint32_t bit ) {
    bit = bit & 1;
    bit = bit << stat.bitIndex;
    stat.byte |= bit;

    ++stat.bitIndex;
    if( stat.bitIndex > 7 ) {
        // move the newly-finished byte to the chunk buffer
        stat.chunk[stat.chunkIndex++] = stat.byte;
        // and start a new byte
        stat.bitIndex = 0;
        stat.byte = 0;
    }
}

// write all bytes so far to the file
void GifWriteChunk( FILE * f, GifBitStatus & stat ) {
    fputc(stat.chunkIndex, f);
    fwrite(stat.chunk, 1, stat.chunkIndex, f);

    stat.bitIndex = 0;
    stat.byte = 0;
    stat.chunkIndex = 0;
}

void GifWriteCode( FILE * f, GifBitStatus & stat, uint32_t code,
                   uint32_t length ) {
    for( uint32_t ii = 0; ii < length; ++ii ) {
        GifWriteBit(stat, code);
        code = code >> 1;

        if( stat.chunkIndex == 255 ) {
            GifWriteChunk(f, stat);
        }
    }
}

// write an image palette to the file
void GifWritePalette( const GifPalette * pPal, FILE * f ) {
    fputc(0, f);  // first color: transparency
    fputc(0, f);
    fputc(0, f);
    for(int ii = 1; ii < (1 << pPal->bitDepth); ++ii) {
        uint32_t r = pPal->r[ii];
        uint32_t g = pPal->g[ii];
        uint32_t b = pPal->b[ii];

        fputc(r, f);
        fputc(g, f);
        fputc(b, f);
    }
}

// write the image header, LZW-compress and write out the image
// localPalette is true to write out pPal as a local palette; otherwise it is the global palette.
void GifWriteLzwImage(FILE * f, uint8_t * image, uint32_t left, uint32_t top,
                      uint32_t width, uint32_t height, uint32_t delay, const GifPalette * pPal) {
    // graphics control extension
    fputc(0x21, f);
    fputc(0xf9, f);
    fputc(0x04, f);
    fputc(0x05, f);    // leave prev frame in place, this frame has transparency
    fputc(delay & 0xff, f);
    fputc((delay >> 8) & 0xff, f);
    fputc(kGifTransIndex, f); // transparent color index
    fputc(0, f);

    fputc(0x2c, f); // image descriptor block

    fputc(left & 0xff, f);           // corner of image in canvas space
    fputc((left >> 8) & 0xff, f);
    fputc(top & 0xff, f);
    fputc((top >> 8) & 0xff, f);

    fputc(width & 0xff, f);          // width and height of image
    fputc((width >> 8) & 0xff, f);
    fputc(height & 0xff, f);
    fputc((height >> 8) & 0xff, f);

    fputc(0, f); // no local color table

    const int minCodeSize = pPal->bitDepth;
    const uint32_t clearCode = 1 << pPal->bitDepth;

    fputc(minCodeSize, f); // min code size 8 bits

    GifLzwNode * codetree = (GifLzwNode *)GIF_TEMP_MALLOC(sizeof(
                                GifLzwNode) * 4096);

    memset(codetree, 0, sizeof(GifLzwNode) * 4096);
    int32_t curCode = -1;
    uint32_t codeSize = minCodeSize + 1;
    uint32_t maxCode = clearCode + 1;

    GifBitStatus stat;
    stat.byte = 0;
    stat.bitIndex = 0;
    stat.chunkIndex = 0;

    GifWriteCode(f, stat, clearCode,
                 codeSize);  // start with a fresh LZW dictionary

    for(uint32_t yy = 0; yy < height; ++yy) {
        for(uint32_t xx = 0; xx < width; ++xx) {
            uint8_t nextValue = image[(yy * width + xx) * 4 + 3];

            // "loser mode" - no compression, every single code is followed immediately by a clear
            //WriteCode( f, stat, nextValue, codeSize );
            //WriteCode( f, stat, 256, codeSize );

            if( curCode < 0 ) {
                // the first value in the image
                curCode = nextValue;
            } else if( codetree[curCode].m_next[nextValue] ) {
                // current run already in the dictionary
                curCode = codetree[curCode].m_next[nextValue];
            } else {
                // finish the current run, write a code
                GifWriteCode( f, stat, curCode, codeSize );

                // insert the new run into the dictionary
                codetree[curCode].m_next[nextValue] = ++maxCode;

                if( maxCode >= (1ul << codeSize) ) {
                    // dictionary entry count has broken a size barrier,
                    // we need more bits for codes
                    codeSize++;
                }
                if( maxCode == 4095 ) {
                    // the dictionary is full, clear it out and begin anew
                    GifWriteCode(f, stat, clearCode, codeSize); // clear tree

                    memset(codetree, 0, sizeof(GifLzwNode) * 4096);

                    codeSize = minCodeSize + 1;
                    maxCode = clearCode + 1;
                }
                curCode = nextValue;
            }
        }
    }

    // compression footer
    GifWriteCode( f, stat, curCode, codeSize );
    GifWriteCode( f, stat, clearCode, codeSize );
    GifWriteCode( f, stat, clearCode + 1, minCodeSize + 1 );

    // write out the last partial chunk
    while( stat.bitIndex ) {
        GifWriteBit(stat, 0);
    }
    if( stat.chunkIndex ) {
        GifWriteChunk(f, stat);
    }

    fputc(0, f); // image block terminator

    GIF_TEMP_FREE(codetree);
}

// Creates a gif file.
// The input GIFWriter is assumed to be uninitialized.
// The delay value is the time between frames in hundredths of a second - note that not all viewers pay much attention to this value.
// globalPal is a default palette to use for GifWriteFrame8(). It is not used by GifWriteFrame().
bool GifBegin( GifWriter * writer, FILE * file, uint32_t width, uint32_t height,
               uint32_t delay, const GifPalette * globalPal) {
    if(!file) {
        return false;
    }
    writer->f = file;

    writer->firstFrame = true;

    // allocate
    writer->oldImage = (uint8_t *)GIF_MALLOC(width * height * 4);

    fputs("GIF89a", writer->f);

    // screen descriptor
    fputc(width & 0xff, writer->f);
    fputc((width >> 8) & 0xff, writer->f);
    fputc(height & 0xff, writer->f);
    fputc((height >> 8) & 0xff, writer->f);

    if( globalPal ) {
        fputc(0xf0 + (globalPal->bitDepth - 1),
              writer->f);    // there is an unsorted global color table
    } else {
        fputc(0xf0,
              writer->f);    // there is an unsorted global color table of 2 entries
    }
    fputc(0, writer->f);     // background color
    fputc(0, writer->f);     // pixels are square (we need to specify this because it's 1989)

    writer->globalPal = (GifPalette *)GIF_MALLOC(sizeof(GifPalette));
    memcpy(writer->globalPal, globalPal, sizeof(GifPalette));
    // write the global palette
    GifWritePalette(globalPal, writer->f);

    if( delay != 0 ) {
        // animation header
        fputc(0x21, writer->f); // extension
        fputc(0xff, writer->f); // application specific
        fputc(11, writer->f); // length 11
        fputs("NETSCAPE2.0", writer->f); // yes, really
        fputc(3, writer->f); // 3 bytes of NETSCAPE2.0 data

        fputc(1, writer->f); // JUST BECAUSE
        fputc(0, writer->f); // loop infinitely (byte 0)
        fputc(0, writer->f); // loop infinitely (byte 1)

        fputc(0, writer->f); // block terminator
    }

    return true;
}


// (See also GifWriteFrame.)
// If palette is NULL, or if it is identical to the global palette, then the global palette is used.
bool GifWriteFrame8( GifWriter * writer, const uint8_t * image, uint32_t width,
                     uint32_t height, uint32_t delay) {
    if(!writer->f) {
        return false;
    }
    if(!writer->globalPal) {
        return false;
    }

    const uint8_t * oldImage = writer->firstFrame ? NULL : writer->oldImage;
    writer->firstFrame = false;

    GifDeltaImage(oldImage, image, writer->oldImage, width, height,
                  writer->globalPal);

    GifWriteLzwImage(writer->f, writer->oldImage, 0, 0, width, height, delay,
                     writer->globalPal);

    return true;
}

// Writes the EOF code, closes the file handle, and frees temp memory used by a GIF.
// Many if not most viewers will still display a GIF properly if the EOF code is missing,
// but it's still a good idea to write it out.
bool GifEnd( GifWriter * writer ) {
    if(!writer->f) {
        return false;
    }

    fputc(0x3b, writer->f); // end of file
    fclose(writer->f);
    GIF_FREE(writer->oldImage);
    GIF_FREE(writer->globalPal);

    writer->f = NULL;
    writer->oldImage = NULL;
    writer->globalPal = NULL;

    return true;
}
