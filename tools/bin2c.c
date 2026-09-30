/* Intègre un fichier dans un en-tête C, comme « xxd -i » (absent sous Windows) :
 *   bin2c assets/fonts/Poppins-Bold.ttf  →  unsigned char Poppins_Bold_ttf[] = {…}; unsigned int Poppins_Bold_ttf_len = …;
 * Le nom de la variable est celui du fichier, caractères non alphanumériques remplacés par « _ ». */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage : bin2c fichier > sortie.h\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    const char *base = argv[1];
    for (const char *p = argv[1]; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    char name[256];
    size_t n = 0;
    for (const char *p = base; *p && n + 1 < sizeof name; p++) name[n++] = isalnum((unsigned char)*p) ? *p : '_';
    name[n] = '\0';

    printf("unsigned char %s[] = {", name);
    unsigned long len = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        printf("%s0x%02x", len % 16 ? ", " : (len ? ",\n  " : "\n  "), c);
        len++;
    }
    printf("\n};\nunsigned int %s_len = %lu;\n", name, len);
    fclose(f);
    return 0;
}
