//
//  main.c
//  mac2rk
//
//  Created by Alexey Khudyakov on 10/16/12.
//  Copyright (c) 2012 alexcp. All rights reserved.
//

#define SAMPLE_RATE         (48000)
#define DEFAULT_BAUD_RATE   (1300L)
#define OUTPUT_LEVEL        (0.1F)
#define MAGIC_BYTE          (0xE6)
#define PADDING_BYTE        (0x00)
#define PADDING_SIZE        (256L)

#include <stdio.h>
#include <stdlib.h>
#include "/opt/local/include/portaudio.h" // HEADER_SEARCH_PATHS or USER_HEADER_SEARCH_PATHS did not work

typedef struct
{
    char *buffer;   // binary file contents
    long bufSize;   // number of bytes in the buffer
    long baudRate;  // baud rate
    char *ptr;      // pointer to the byte being transmitted
    int bit;        // bit being transmitted (7 = MSB, 0 = LSB)
    int sample;     // sample being transmitted
    int invert;     // 1 if the half bit being transmitted is inverted, 0 otherwise
    int eof;        // file was transmitted in full
} paData;

static paData data;

void usage(void)
{
	fprintf(stderr, "Usage: mac2rk [-b] filename [baudrate]\n");
}

static int paCallback(const void *inputBuffer,
                      void *outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void *userData)
{
    /* Cast data passed through stream to our structure. */
    paData *data = (paData*)userData;
    float *out = (float*)outputBuffer;
    unsigned int i;
    (void) inputBuffer; /* Prevent unused variable warning. */
    
    for(i = 0; i < framesPerBuffer; i++) {
        if (data->eof != 0) {
            *out++ = 0.0f;  /* left */
            *out++ = 0.0f;  /* right */
        } else {
            if ((*data->ptr >> data->bit & 1) ^ (data->invert & 1)) {
                *out++ = OUTPUT_LEVEL;  /* left */
                *out++ = OUTPUT_LEVEL;  /* right */
            } else {
                *out++ = -OUTPUT_LEVEL;  /* left */
                *out++ = -OUTPUT_LEVEL;  /* right */
            }
            
            if (--data->sample < 0) {
                data->sample = SAMPLE_RATE / (data->baudRate * 2);
                if (data->invert == 1) {
                    data->invert = 0;
                } else {
                    data->invert = 1;
                    if (--data->bit < 0) {
                        data->bit = 7;
                        if ((data->ptr - data->buffer) < data->bufSize) {
                            data->ptr++;
                        } else {
                            data->eof = 1;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    char *sourceFileName;
    FILE *sourceFile;
    int ch;
    PaStream *stream;
    PaError err;
    
    // Parse command line
    data.baudRate = DEFAULT_BAUD_RATE;
    switch (argc)
    {
        case 3:
            data.baudRate = atol(argv[2]);
            if (data.baudRate <= 0 || data.baudRate > (SAMPLE_RATE / 2)) {
                fprintf(stderr, "Baud rate maust be between 1 and %d\n", SAMPLE_RATE / 2);
                exit(EXIT_FAILURE);
            }
        case 2:
            sourceFileName = argv[1];
            break;
        default:
            usage();
            exit(EXIT_FAILURE);
    }
    
    // Open source file and find out its size
    sourceFile = fopen(sourceFileName, "rb");
    
    if (sourceFile == NULL)
    {
        fprintf(stderr, "Cannot open input file %s\n", sourceFileName);
        exit(EXIT_FAILURE);
    }
    
    fseek(sourceFile, 0L, SEEK_END);
    data.bufSize = ftell(sourceFile);
    fseek(sourceFile, 0L, SEEK_SET);
    
    if (data.bufSize > 0x7600L)
    {
        fprintf(stderr, "Input file is too long\n");
        fclose(sourceFile);
        exit(EXIT_FAILURE);
    }
    
    data.bufSize += PADDING_SIZE; // Room for zero padding bytes
        
    ch = fgetc(sourceFile);
    if (ch == EOF) {
        fprintf(stderr, "Input file is empty\n");
        fclose(sourceFile);
        exit(EXIT_FAILURE);
    }
    if (ch != MAGIC_BYTE) data.bufSize += 1;
    
    // Allocate and fill the buffer
    data.buffer = (char *)malloc(data.bufSize*sizeof(char));
    
    if (data.buffer == NULL) {
        fprintf(stderr, "Cannot allocate memory\n");
        fclose(sourceFile);
        exit(EXIT_FAILURE);
    }
    
    data.ptr = data.buffer;
    
    while ((data.ptr - data.buffer) < PADDING_SIZE) {
        *data.ptr++ = PADDING_BYTE;
    }

    if (ch != MAGIC_BYTE) *data.ptr++ = MAGIC_BYTE;
    *data.ptr++ = ch;

    while ((ch = fgetc(sourceFile)) != EOF && (data.ptr - data.buffer) < data.bufSize) {
        *data.ptr++ =ch;
    }
    fclose(sourceFile);
    
    // Initialize data strucutre for use by pACallback()
    data.ptr = data.buffer;
    data.bit = 7;
    data.invert = 1;
    data.sample = SAMPLE_RATE / (data.baudRate * 2);
    data.eof = 0;
    
    // Initialize library before making any other calls
    err = Pa_Initialize();
    if (err != paNoError) goto error;
    
    /* Open an audio I/O stream. */
    err = Pa_OpenDefaultStream(&stream,
                               0,          /* no input channels */
                               2,          /* stereo output */
                               paFloat32,  /* 32 bit floating point output */
                               SAMPLE_RATE,
                               256,        /* frames per buffer */
                               paCallback,
                               &data);
    if (err != paNoError) goto error;
    
    err = Pa_StartStream(stream);
    if (err != paNoError) goto error;
    
    /* Sleep for enough time for the file to transmit */
    Pa_Sleep(data.bufSize * 8 * 1000 / data.baudRate + 1000);
    
    err = Pa_StopStream(stream);
    if (err != paNoError) goto error;
    
    err = Pa_CloseStream(stream);
    if (err != paNoError) goto error;
    
    Pa_Terminate();
    free(data.buffer);
    exit(err);

error:
    Pa_Terminate();
    fprintf( stderr, "An error occured while using the portaudio stream\n" );
    fprintf( stderr, "Error number: %d\n", err );
    fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
    free(data.buffer);
    exit(err);
}
