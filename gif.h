//
// gif.h
// by Charlie Tangora
// Public domain.
// Email me : ctangora -at- gmail -dot- com
//
// This file offers a simple, very limited way to create animated GIFs directly in code.
//
// Those looking for particular cleverness are likely to be disappointed; it's pretty
// much a straight-ahead implementation of the GIF format with optional Floyd-Steinberg
// dithering. (It does at least use delta encoding - only the changed portions of each
// frame are saved.)
//
// So resulting files are often quite large. The hope is that it will be handy nonetheless
// as a quick and easily-integrated way for programs to spit out animations.
//
// There are two supported input formats, RGBA8 (the alpha is ignored), and
// 8-bit paletted (with a power-of-two palette size).
// (In the latter case you can save up to 768 bytes per frame by providing a global palette
// and reusing it for some frames.)
// You can freely mix 32-bit and 8-bit input frames.
//
// USAGE:
// Create a GifWriter struct. Pass it to GifBegin() to initialize and write the header.
// Pass subsequent frames to GifWriteFrame() or GifWriteFrame8().
// Finally, call GifEnd() to close the file handle and free memory.
//
// A frame is of the type uint8_t*, or more specific, uint8_t[width][height][4], such that
//    frame[x][y] = [red, green, blue, alpha]

#ifndef gif_h
#define gif_h

#include <stdio.h>   // for FILE*
#include <string.h>  // for memcpy and bzero
#include <stdint.h>  // for integer typedefs

// Define these macros to hook into a custom memory allocator.
// TEMP_MALLOC and TEMP_FREE will only be called in stack fashion - frees in the reverse order of mallocs
// and any temp memory allocated by a function will be freed before it exits.
// MALLOC and FREE are used only by GifBegin and GifEnd respectively (to allocate a buffer the size of the image, which
// is used to find changed pixels for delta-encoding.)

#ifndef GIF_TEMP_MALLOC
    #include <stdlib.h>
    #define GIF_TEMP_MALLOC malloc
#endif

#ifndef GIF_TEMP_FREE
    #include <stdlib.h>
    #define GIF_TEMP_FREE free
#endif

#ifndef GIF_MALLOC
    #include <stdlib.h>
    #define GIF_MALLOC malloc
#endif

#ifndef GIF_FREE
    #include <stdlib.h>
    #define GIF_FREE free
#endif

const int kGifTransIndex = 0;

struct GifPalette {
    int bitDepth;

    uint8_t r[256];
    uint8_t g[256];
    uint8_t b[256];
};

void GifDeltaImage( const uint8_t * lastFrame, const uint8_t * nextFrame8,
                    uint8_t * outFrame, uint32_t width, uint32_t height,
                    const GifPalette * pPal );

// Simple structure to write out the LZW-compressed portion of the image
// one bit at a time
struct GifBitStatus {
    uint8_t bitIndex;  // how many bits in the partial byte written so far
    uint8_t byte;      // current partial byte

    uint32_t chunkIndex;
    uint8_t chunk[256];   // bytes are written in here until we have 256 of them, then written to the file
};

// insert a single bit
void GifWriteBit( GifBitStatus & stat, uint32_t bit );
void GifWriteChunk( FILE * f, GifBitStatus & stat );
void GifWriteCode( FILE * f, GifBitStatus & stat, uint32_t code,
                   uint32_t length );

// The LZW dictionary is a 256-ary tree constructed as the file is encoded,
// this is one node
struct GifLzwNode {
    uint16_t m_next[256];
};

struct GifWriter {
    FILE * f;
    uint8_t * oldImage;
    bool firstFrame;
    GifPalette * globalPal;
};

void GifWritePalette( const GifPalette * pPal, FILE * f );
void GifWriteLzwImage(FILE * f, uint8_t * image, uint32_t left, uint32_t top,
                      uint32_t width, uint32_t height, uint32_t delay, const GifPalette * pPal);
bool GifBegin( GifWriter * writer, FILE * file, uint32_t width, uint32_t height,
               uint32_t delay, const GifPalette * globalPal = NULL );
bool GifWriteFrame8( GifWriter * writer, const uint8_t * image, uint32_t width,
                     uint32_t height, uint32_t delay);
bool GifEnd( GifWriter * writer );

#endif
