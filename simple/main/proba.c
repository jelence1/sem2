#include <stdio.h>
#include <malloc.h>

int main(void) {

    char *buffer = 0;
    long length;
    FILE *f = fopen("index.html", "r");
    if(f)
    {
        fseek(f, 0, SEEK_END);
        length = ftell(f);
        fseek(f, 0, SEEK_SET);
        buffer = malloc(length);
        if (buffer)
        {
            fread(buffer, 1, length, f);
        }
    fclose(f);
    }

    if (buffer)
    {
        printf("OVO JE JELENIN PRINT\n");
        printf("%s", buffer);
    }

    return 0;
}