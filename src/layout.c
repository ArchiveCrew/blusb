#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <libusb.h>
#include "blusb.h"
#include "layout.h"
#include "usb.h"

/**
 * Layout is a type for an object that represents the keyboard layout in 
 * up to 6 different layers.
 */

/**
 * Convert the layout matrix to a sequential byte array that can be used 
 * by the bl_usb api. The data actually contains 16 bit numbers, so every two 
 * bytes make up a number in little endian order. The first two bytes in the 
 * array represent the number of layers, the rest of the data are the layers, 
 * and for every layer, row per row.
 *
 * @param layout The layout struct from which to get the matrix data.
 * @return Newly allocated array of bytes representing the matrix data, must  
 *         be freed after use.
 */
uint8_t *
bl_layout_convert(bl_layout_t *layout) {
    uint8_t *data = (uint8_t *) malloc(sizeof(uint16_t) * (1 + layout->nlayers * NUMCOLS * NUMROWS));
    ((uint16_t *)data)[0] = layout->nlayers;
    int n = 0;
    for (int layer=0; layer<layout->nlayers; layer++) {
        for (int row=0; row<NUMROWS; row++) {
            for (int col=0; col<NUMCOLS; col++) {
                int n = layer * NUMROWS * NUMCOLS + 
                    row * NUMCOLS +
                    col;
                ((uint16_t *)data)[n] = layout->matrix[layer][row][col];
            }
        }
    }

    return data;
}

/**
 * Create an empty layout struct. Set the number of layers, the
 * matrix array is allocated but left undefined.
 *
 * @param nlayers The number of layers to create in the struct
 */
bl_layout_t *
bl_create_layout(int nlayers) {
    bl_layout_t *layout = (bl_layout_t *) malloc( sizeof(bl_layout_t) );
    layout->nlayers = nlayers;

    return layout;
}

/**
 * Parse the file and return a layout struct. The memory for the layout is allocated and
 * must be freed after use.
 */
bl_layout_t*
bl_parse_layout_file(char *fname) {
    FILE *f = fopen(fname, "r");
    if (f == NULL) {
        printf("Could not open file %s\n", fname);
        return NULL;
    }

    /*
     * Format to be parsed is:
     *
     * LAYOUT = LAYERS
     * LAYERS = KEYS
     *        | KEYS '\n' LAYERS
     * KEYS = KEY ','
     *      | KEY
     * KEY = DIGIT+
     * DIGIT= [0-9]
     *
     */
    const int BL_STATE_DIGIT = 0;
    const int BL_STATE_WHITESPACE = 1;
    bl_layout_t *layout = bl_create_layout(NUMLAYERS_MAX);
    int layer = 0;
    int col = 0;
    int row = 0;
    int state = BL_STATE_WHITESPACE;
    char parse_buffer[20];
    parse_buffer[0] = 0;
    char ch = fgetc(f);
    while (!feof(f)) {
        if (state == BL_STATE_DIGIT) {
            if (isdigit(ch)) {
                if (strlen(parse_buffer) >= sizeof(parse_buffer) -1) {
                    printf("Error: ran out of buffer space for parsing, comma missing? Line %d, key %d, (byte position=%ld)\n", layer+1, col+1, ftell(f));
                    free(layout);
                    return NULL;
                } else {
                    int i = strlen(parse_buffer);
                    parse_buffer[i] = ch;
                    parse_buffer[i+1] = 0;
                }
            } else if (ch == ',') {
                uint16_t key = atoi(parse_buffer);
                layout->matrix[layer][row][col] = key;
                parse_buffer[0] = 0;
                if (col == NUMCOLS-1) {
                    col = 0;
                    row++;
                } else {
                    col++;
                }
                state = BL_STATE_WHITESPACE;
            } else if (ch == '\n' || ch == '\r') {
                layout->matrix[layer][row][col] = atoi(parse_buffer);
                parse_buffer[0] = 0;
                state = BL_STATE_WHITESPACE;
                if (col < NUMCOLS-1) {
                    printf("Invalid number of keys in row, actually %d, expected %d at line %d, key %d (byte position=%ld)\n", col+1, NUMCOLS, layer+1, col+1, ftell(f));
                    free(layout);
                    return NULL;
                }
                if (row < NUMROWS-1) {
                    printf("Invalid number of rows in layer, actually %d, expected %d at line %d, key %d (byte position=%ld)\n", row+1, NUMROWS, layer+1, col+1, ftell(f));
                    free(layout);
                    return NULL;
                }
                layer++;
                col = 0;
                row = 0;
            } else {
                printf("Unexpected character encountered while parsing digits: %c, at line %d, key %d (position=%ld)\n", ch, layer+1, col+1, ftell(f));
                free(layout);
                return NULL;
            }
            ch = fgetc(f);
        } else if (state == BL_STATE_WHITESPACE) {
            if (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t') {
                // ignore
                ch = fgetc(f);
            } else if (isdigit(ch)) {
                state = BL_STATE_DIGIT;
                // don't read the next character, we need the current character to be processed as a digit in the BL_STATE_DIGIT state.
            } else {
                printf("Unexpected character encountered while skipping whitespace: %c, at line %d, key %d (position=%ld)\n", ch, layer, col+row*col, ftell(f));
            }
        }
    }
    /*
     * We need to add the last key in the parse_buffer if there is one.
     */
    if (strlen(parse_buffer) > 0) {
        layout->matrix[layer][row][col] = atoi(parse_buffer);
    }
    layout->nlayers = layer;

    if ((row > 0 && row < NUMROWS-1) || (col > 0 && col < NUMCOLS-1)) {
        printf("Invalid layout file, not enough key entries for layer %d, actually %d, expected %d at line=%d\n",
               layer+1, 1+col+row*col, NUMROWS*NUMCOLS, layer+1);
        free(layout);
        fclose(f);
        return NULL;
    } else {
        fclose(f);
        return layout;
    }
}

/**
 * Pretty print the layout file
 */
void
bl_layout_print(bl_layout_t *layout) {
    printf("Number of layers: %d\n\n", layout->nlayers);
    for (int layer=0; layer<layout->nlayers; layer++) {
        printf("Layer %d\n\n", layer);
        printf("    ");
        for (int col=0; col<NUMCOLS; col++) {
            printf("C%-5u", col+1);
        }
        printf("\n");
        for (int row=0; row<NUMROWS; row++) {
            printf("R%d  ", row+1);
            for (int col=0; col<NUMCOLS; col++) {
                printf("%-6u", layout->matrix[layer][row][col]);
            }
            printf("\n");
        }
        printf("\n");
    }
}

/**
 * Write the layout in the given file to the controller
 */
int
bl_layout_write(char *fname) {
    bl_layout_t *layout = bl_parse_layout_file(fname);
    uint8_t *data = bl_layout_convert(layout);

    bl_usb_write_layout(data, layout->nlayers);

    return 0;
}