// EXPECT: stdio-read-ok
#include <stdio.h>
#include <string.h>
#include <errno.h>
int main(void) {
    char name[L_tmpnam], missing[L_tmpnam], buf[32], user_buffer[7];
    FILE *fp;
    FILE *reader;
    tmpnam(name);
    fp = fopen(name, "wb+");
    if (!fp) return 1;
    if (setvbuf(fp, user_buffer, _IOFBF, sizeof(user_buffer))) return 2;
    if (fwrite("abcdef", 2, 3, fp) != 3 || ftell(fp) != 6) return 3;
    reader = fopen(name, "r");
    if (!reader || fgetc(reader) != EOF || fclose(reader)) return 30;
    if (fseek(fp, 0, SEEK_SET) || fread(buf, 0, 10, fp) != 0) return 4;
    if (fread(buf, 10, 0, fp) != 0) return 5;
    if (fgetc(fp) != 'a' || ftell(fp) != 1) return 6;
    if (ungetc('Z', fp) != 'Z' || ftell(fp) != 0) return 7;
    if (ungetc('Y', fp) != EOF || fgetc(fp) != 'Z' || fgetc(fp) != 'b') return 8;
    if (fseek(fp, 0, SEEK_CUR) || ftell(fp) != 2 || fgetc(fp) != 'c') return 9;
    if (fseek(fp, 0, SEEK_SET) || fread(buf, 1, 20, fp) != 6 || !feof(fp)) return 10;
    if (ungetc(EOF, fp) != EOF || !feof(fp)) return 11;
    if (ungetc('Q', fp) != 'Q' || feof(fp) || fgetc(fp) != 'Q') return 12;
    if (ungetc('R', fp) != 'R' || fseek(fp, 0, SEEK_SET) || fgetc(fp) != 'a') return 13;
    if (fseek(fp, 2, SEEK_SET) || fputc('X', fp) == EOF) return 14;
    if (fseek(fp, 0, SEEK_SET) || fread(buf, 1, 6, fp) != 6 || memcmp(buf, "abXdef", 6)) return 15;
    if (fread(buf, (size_t)-1, 2, fp) || !ferror(fp)) return 16;
    clearerr(fp);
    if (setvbuf(fp, NULL, 99, 12) == 0) return 17;
    if (setvbuf(fp, NULL, _IONBF, 0) || fseek(fp, 0, SEEK_END)) return 18;
    if (fputs("!", fp) < 0) return 19;
    reader = fopen(name, "r");
    if (!reader || fread(buf, 1, 20, reader) != 7 || memcmp(buf, "abXdef!", 7)) return 20;
    if (fclose(reader)) return 21;
    if (setvbuf(fp, NULL, _IOLBF, 11) || fputs("line\n", fp) < 0) return 22;
    if (fflush(NULL) || fclose(fp)) return 23;
    fp = fopen(name, "r");
    if (!fp || fgetc(fp) != 'a') return 24;
    if (freopen(name, "r+b", fp) != fp || fgetc(fp) != 'a') return 25;
    if (fclose(fp)) return 26;
    fp = fopen(name, "r");
    tmpnam(missing);
    remove(missing);
    if (!fp || freopen(missing, "r", fp) != NULL) return 27;
    if (fileno(fp) != -1) return 28;
    fp = fopen(name, "a+");
    if (!fp || fseek(fp, 0, SEEK_SET) || fputc('A', fp) == EOF) return 36;
    if (ftell(fp) != 13 || fflush(fp) || ftell(fp) != 13) return 37;
    if (fseek(fp, 0, SEEK_SET) || fread(buf, 1, 20, fp) != 13) return 38;
    if (memcmp(buf, "abXdef!line\nA", 13) || fclose(fp)) return 39;
    fp = fopen(name, "w");
    if (!fp || _onyx_close(fileno(fp)) < 0) return 31;
    if (fwrite("pending", 1, 7, fp) != 7) return 32;
    if (fflush(NULL) != EOF || !ferror(fp)) return 33;
    if (fseek(fp, 0, SEEK_SET) != EOF) return 34;
    if (fclose(fp) != EOF || fileno(fp) != -1) return 35;
    if (remove(name)) return 29;
    fp = tmpfile();
    if (!fp || fwrite("tmp", 1, 3, fp) != 3 || fseek(fp, 0, SEEK_SET)) return 40;
    if (fread(buf, 1, 3, fp) != 3 || memcmp(buf, "tmp", 3) || fclose(fp)) return 41;
    printf("stdio-read-ok\n");
    return 0;
}
