//
//  main.c
//  mac2rk
//
//  Created by Alexey Khudyakov on 10/16/12.
//  Copyright (c) 2012 alexcp. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>

void usage(void)
{
	printf("Usage: mac2rk filename [baudrate]\n");
	exit (8);
}

int main(int argc, char *argv[])
{
    long baudRate = 1280;
    char *sourceFileName;
    FILE *sourceFile;
    long fileSize;
    char *buffer, *ptr;
    int ch, i;
    
    switch (argc) {
        case 3:
            baudRate = atol(argv[2]);
        case 2:
            sourceFileName = argv[1];
            break;
        default:
            usage();
            return EXIT_FAILURE;
    }

    sourceFile = fopen(sourceFileName, "rb");
    
    if (sourceFile == NULL) {
        printf("Cannot open source file %s\n", sourceFileName);
        exit(EXIT_FAILURE);
    }
    
    fseek(sourceFile, 0L, SEEK_END);
    fileSize = ftell(sourceFile);
    fseek(sourceFile, 0L, SEEK_SET);
    
    if (fileSize > 0x7600L) {
        printf("Source file is too long\n");
        exit(EXIT_FAILURE);
    }
    
    buffer = (char *)malloc(fileSize*sizeof(char));
    
    if (buffer == NULL) {
        printf("Cannot allocate memory\n");
        exit(EXIT_FAILURE);
    }
    
    ptr = buffer;
    while ((ch = fgetc(sourceFile)) != EOF && (ptr - buffer) < fileSize) *ptr++ =ch;

    

    
    ptr = buffer;
    while ((ptr - buffer) < fileSize) {
        ch = *ptr++;
        for (i = 7; i >= 0; i--) {
            if (ch & (1 << i)) {
                printf("1");
            } else {
                printf("0");
            }
        }
        printf("\n");
    }
    

    
    free (buffer);
    fclose(sourceFile);
    
    return 0;
}
