//
//  main.c
//  mac2rk
//
//  Created by Alexey Khudyakov on 10/16/12.
//  Copyright (c) 2012 alexcp. All rights reserved.
//

#define SAMPLE_RATE         (48000L)
#define DEFAULT_BAUDRATE    (1300L)
#define OUTPUT_LEVEL        (0.1F)
#define MAGIC_BYTE          (0xE6)
#define PADDING_BYTE        (0x00)
#define PADDING_SIZE        (256L)
#define FILESIZE_LIMIT      (0x7600L)

#include <stdio.h>
#include <stdlib.h>
#include "/opt/local/include/portaudio.h" // HEADER_SEARCH_PATHS or USER_HEADER_SEARCH_PATHS did not work

void usage(void)
{
	fprintf(stderr, "usage: mac2rk [-b [-a loadaddr]] [-r baudrate] filename\n");
	fprintf(stderr, "       use -b for plain binary files, such as compiler output\n");
	fprintf(stderr, "       use without -b for .rkr, .gam and other tape images\n");
    fprintf(stderr, "       loadaddr must be between 0h and %lXh; default is 0h\n", FILESIZE_LIMIT);
    fprintf(stderr, "       baudrate must be between 1 and %ld; default is %ld\n", SAMPLE_RATE/2, DEFAULT_BAUDRATE);
}

typedef struct
{
    unsigned char *buffer;  // binary file contents
    long bufSize;           // number of bytes in the buffer
    long baudRate;          // baud rate
    unsigned char *ptr;     // pointer to the byte being transmitted
    int bit;                // bit being transmitted (7 = MSB, 0 = LSB)
    int sample;             // sample being transmitted
    int invert;             // 1 if the half bit being transmitted is inverted, 0 otherwise
    int eof;                // file was transmitted in full
} paData;

static paData data;

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

long checksum(unsigned char* buffer, long size)
{
    long loByte = 0;
    long hiByte = 0;
    long i = 0;

    for (;;) {
        loByte += *buffer;
        if (++i >= size) {
            return (loByte & 0xFF) + (hiByte << 8);
        }
        hiByte += *buffer;
        hiByte += ((loByte >> 8) & 1);
        loByte &= 0xFF; hiByte &= 0xFF;
        buffer++;
    }
}

int main(int argc, char *argv[])
{
    char *inputFileName;
    FILE *inputFile;
    long fileSize;
    int ch;
    PaStream *stream;
    PaError err;
    int binary = 0;
    long startAddress = 0;
    
    // Parse command line

	if (argc == 1) {
        usage();
        exit(EXIT_FAILURE);
    }

    data.baudRate = DEFAULT_BAUDRATE;
    
    while ((argc > 1)) {
		if (argv[1][0] == '-') {
            
            switch (argv[1][1]) {
			
                case 'b':
                    binary = 1;
                    break;
                
                case 'r':
                    if (argv[1][2] != '\0') {
                        data.baudRate = strtol(&argv[1][2], NULL, 0);
                        break;
                    } else if (argc > 2) {
                        data.baudRate = strtol(argv[2], NULL, 0);
                        ++argv; --argc;
                        break;
                    }

                case 'a':
                    if (argv[1][2] != '\0') {
                        startAddress = strtol(&argv[1][2], NULL, 0);
                        break;
                    } else if (argc > 2) {
                        startAddress = strtol(argv[2], NULL, 0);
                        ++argv; --argc;
                        break;
                    }
                    
                default:
                    usage();
                    exit(EXIT_FAILURE);
            }
		} else {
            inputFileName = argv[1];
        }
		++argv;
		--argc;
	}

    if (startAddress < 0 || startAddress > FILESIZE_LIMIT)
    {
        fprintf(stderr, "loadadd must be between 0 and %lX\n", FILESIZE_LIMIT);
        exit(EXIT_FAILURE);
    }
    
    if (data.baudRate <= 0 || data.baudRate > SAMPLE_RATE/2)
    {
        fprintf(stderr, "baudrate must be between 1 and %ld\n", SAMPLE_RATE/2);
        exit(EXIT_FAILURE);
    }
    
    // Open source file and find out its size
    inputFile = fopen(inputFileName, "rb");
    
    if (inputFile == NULL)
    {
        fprintf(stderr, "Cannot open input file %s\n", inputFileName);
        exit(EXIT_FAILURE);
    }
    
    fseek(inputFile, 0L, SEEK_END);
    fileSize = ftell(inputFile);
    fseek(inputFile, 0L, SEEK_SET);
    
    if (fileSize > FILESIZE_LIMIT)
    {
        fprintf(stderr, "Input file is too long\n");
        fclose(inputFile);
        exit(EXIT_FAILURE);
    }
    
    data.bufSize = fileSize + PADDING_SIZE;
    
    if (binary) {
        data.bufSize += 10; // start address, end address, sync byte, two zero bytes, sync byte, checksum
    } else {
        ch = fgetc(inputFile);
        if (ch == EOF) {
            fprintf(stderr, "Input file is empty\n");
            fclose(inputFile);
            exit(EXIT_FAILURE);
        }
        if (ch != MAGIC_BYTE) data.bufSize += 1;
    }
    
    // Allocate and fill the buffer
    data.buffer = (unsigned char *)malloc(data.bufSize*sizeof(unsigned char));
    
    if (data.buffer == NULL) {
        fprintf(stderr, "Cannot allocate memory\n");
        fclose(inputFile);
        exit(EXIT_FAILURE);
    }
    
    data.ptr = data.buffer;
    
    while ((data.ptr - data.buffer) < PADDING_SIZE) {
        *data.ptr++ = PADDING_BYTE;
    }
    
    if (binary) {
        *data.ptr++ = MAGIC_BYTE; // sync byte
        *data.ptr++ = (startAddress >> 8) & 0xFF;  *data.ptr++ = startAddress & 0xFF; // start address
        *data.ptr++ = ((startAddress + fileSize - 1) >> 8) & 0xFF;  // end address, high byte
        *data.ptr++ = (startAddress + fileSize - 1) & 0xFF;         // end address, low byte
        fprintf(stderr, "Start address %04lX\n", startAddress);
        fprintf(stderr, "End address   %04lX\n", startAddress + fileSize - 1);
    } else {
        if (ch != MAGIC_BYTE) *data.ptr++ = MAGIC_BYTE;
        *data.ptr++ = ch;
    }

    while ((ch = fgetc(inputFile)) != EOF && (data.ptr - data.buffer) < data.bufSize) {
        *data.ptr++ =ch;
    }
    
    fclose(inputFile);
    
    if (binary) {
        *data.ptr++ = PADDING_BYTE;  *data.ptr++ = PADDING_BYTE;
        *data.ptr++ = MAGIC_BYTE; // sync byte
        long sum = checksum(data.buffer + PADDING_SIZE + 5, fileSize);
        *data.ptr++ = (sum >> 8) & 0xFF;  *data.ptr++ = sum & 0xFF;
        fprintf(stderr, "Checksum      %04lX\n", sum);
    }
    
    // Initialize data structure for use by pACallback()
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
    
    /* Sleep for enough time for the buffer to be transmitted */
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
