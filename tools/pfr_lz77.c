#include <stdio.h>
#include <stdlib.h>

#include "decompress.h"

static unsigned char *ReadFile(const char *path, size_t *size_out)
{
    FILE *file = fopen(path, "rb");
    unsigned char *buffer;
    long size;

    if (file == NULL)
    {
        perror(path);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        perror("fseek");
        fclose(file);
        return NULL;
    }

    size = ftell(file);
    if (size < 0)
    {
        perror("ftell");
        fclose(file);
        return NULL;
    }
    rewind(file);

    buffer = malloc((size_t)size);
    if (buffer == NULL)
    {
        fprintf(stderr, "allocation failed for %ld bytes\n", size);
        fclose(file);
        return NULL;
    }

    if (fread(buffer, 1, (size_t)size, file) != (size_t)size)
    {
        perror("fread");
        free(buffer);
        fclose(file);
        return NULL;
    }

    fclose(file);
    *size_out = (size_t)size;
    return buffer;
}

static int WriteFile(const char *path, const unsigned char *buffer, size_t size)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL)
    {
        perror(path);
        return 1;
    }

    if (fwrite(buffer, 1, size, file) != size)
    {
        perror("fwrite");
        fclose(file);
        return 1;
    }

    fclose(file);
    return 0;
}

int main(int argc, char **argv)
{
    unsigned char *input;
    unsigned char *output;
    size_t input_size;
    size_t output_size;
    int rc;

    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <input.lz77> <output.bin>\n", argv[0]);
        return 1;
    }

    input = ReadFile(argv[1], &input_size);
    if (input == NULL)
        return 1;

    if (input_size < 4)
    {
        fprintf(stderr, "input too small for GBA LZ77 header\n");
        free(input);
        return 1;
    }

    output_size = GetDecompressedDataSize(input);
    output = malloc(output_size);
    if (output == NULL)
    {
        fprintf(stderr, "allocation failed for %zu bytes\n", output_size);
        free(input);
        return 1;
    }

    LZDecompressWram(input, output);
    rc = WriteFile(argv[2], output, output_size);

    free(output);
    free(input);
    return rc;
}
