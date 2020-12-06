
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <zstd.h>
#include <openssl/sha.h>
#include <errno.h>


/*
dps format

dps-binary-pkg
file-details-len in bytes
file-details x N
blob x N

file-details

blob_hash
blob_start
blob_length
blob_final_length
dest_path_count u32
dest_path x N
*/

struct filepathpair {
    char* local;
    char** dest;
    u_int32_t dest_count;

    //for use in write_dps_bin_pkg
    unsigned char blob_hash[SHA512_DIGEST_LENGTH];
    u_int64_t blob_start, blob_length, blob_final_length;
};

void write_dps_bin_pkg(struct filepathpair* fpp, int32_t fppcount, char* pkgname)
{
    FILE* file = fopen("pkg.dpsbp", "w");
    fputs("dpsXbinaryXpkg", file);
    fputs(pkgname, file);
    fputs(":", file);

    u_int64_t fd_start = ftell(file);

    //skip file-details length
    for(u_int32_t k = 0; k < 8; k++) { fputs("d", file); }
    //skip file-details for now
    for(int32_t i = 0; i < fppcount; i++)
    {
        for(u_int32_t k = 0; k < SHA512_DIGEST_LENGTH; k++) { fputs("h", file); }
        for(u_int32_t k = 0; k < 8; k++) { fputs("s", file); }
        for(u_int32_t k = 0; k < 8; k++) { fputs("l", file); }
        for(u_int32_t k = 0; k < 8; k++) { fputs("f", file); }
        fwrite(&fpp[i].dest_count, 4, 1, file);
        for(int32_t j = 0; j < fpp[i].dest_count; j++)
        {
            u_int32_t l = strlen(fpp[i].dest[j]);
            for(u_int32_t k = 0; k < l; k++) { fputs("D", file); }
            fputs(":", file);
        }
    }
    u_int64_t fd_end = ftell(file);
    u_int64_t fd_len = fd_end - fd_start;

    char* tmpbuf = malloc(4096 * sizeof(char));
    char* cmpbuf = malloc(4096 * sizeof(char));

    // write blobs and get hash
    for(int32_t i = 0; i < fppcount; i++)
    {
        FILE* f = fopen(fpp[i].local, "r");
        if(f == NULL)
        {
            printf("failed to open file errno: %d\n", errno);
            assert(0);
        }
        fseek(f, 0, SEEK_END);
        u_int64_t f_size = ftell(f);
        rewind(f);

        tmpbuf = realloc(tmpbuf, f_size);
        cmpbuf = realloc(cmpbuf, ZSTD_compressBound(f_size));
        fread(tmpbuf, f_size, 1, f);
        fclose(f);

        size_t z = ZSTD_compress(cmpbuf, ZSTD_compressBound(f_size), tmpbuf, f_size, 19);
        if(ZSTD_isError(z))
        {
            printf("ZSTD error %s\n", ZSTD_getErrorName(z));
            assert(0);
        }

        cmpbuf = realloc(cmpbuf, z);

        SHA512(cmpbuf, z, fpp[i].blob_hash);
        printf("%d%% compressed size, %d bytes -> %d bytes, name: %s hash: ",
            (int32_t)(100.0 * ((double)z)/((double)f_size)), f_size, z, fpp[i].local);
        for(int j = 0; j < SHA512_DIGEST_LENGTH; j++)
        {
            printf("%x", fpp[i].blob_hash[j]);
        }
        printf("\n");

        fpp[i].blob_start = ftell(file);
        fpp[i].blob_length = z;
        fpp[i].blob_final_length = f_size;

        fwrite(cmpbuf, z, 1, file);
    }

    fseek(file, 0, SEEK_SET); // write out full file-details

    fputs("dps-binary-pkg", file); // we write the magic again to show that
                                   // there was no failure reading the pkg files

    fputs(pkgname, file);
    fputs(":", file);

    fwrite(&fd_len, 8, 1, file); // File details length
    //file details
    for(int32_t i = 0; i < fppcount; i++)
    {
        fwrite(&fpp[i].blob_hash, SHA512_DIGEST_LENGTH, 1, file);
        fwrite(&fpp[i].blob_start, 8, 1, file);
        fwrite(&fpp[i].blob_length, 8, 1, file);
        fwrite(&fpp[i].blob_final_length, 8, 1, file);
        fwrite(&fpp[i].dest_count, 4, 1, file);
        for(int32_t j = 0; j < fpp[i].dest_count; j++)
        {
            fputs(fpp[i].dest[j], file);
            fputs(":", file);
        }
    }
}

int main(int argc, char* argv[])
{
    if(argc < 2) {
        printf("please provide a pkg name as arg1\n");
        return 2;
    }
    printf("building %s...\n", argv[1]);

    system("rm -drf build && mkdir build && cd build && bsdtar -xf ../build.tar && chmod +x build.sh && ./build.sh");

    FILE* reg = fopen("build/register", "r");
    
    int32_t fppcount = 1;
    struct filepathpair* fpp = malloc(fppcount * sizeof(struct filepathpair));
    int32_t fppindex = 0;

    bool firsthalf = true;
    int32_t count = 0;
    int32_t start = 0;
    while(true){
        int g = fgetc(reg);
        if(g == EOF) { break; }
        if(firsthalf && g == ':')
        {
            firsthalf = false;
            count++;
            char* p1 = malloc((count + 6) * sizeof(char)); //"build/" comes before
            strcpy(p1, "build/");
            fseek(reg, start, SEEK_SET);
            fgets(p1 + 6, count, reg);
            start = start+count;
            fseek(reg, start, SEEK_SET);
            count = 0;

            fpp[fppindex].local = p1;
        } else if(!firsthalf && g == '\n')
        {
            firsthalf = true;
            count++;
            char* p2 = malloc(count * sizeof(char));
            fseek(reg, start, SEEK_SET);
            fgets(p2, count, reg);
            start = start+count;
            fseek(reg, start, SEEK_SET);
            count = 0;

            bool dup = false;
            for(int32_t i = 0; i < fppindex; i++)
            {
                if(strcmp(fpp[fppindex].local, fpp[i].local) == 0)
                {
                    fpp[i].dest_count += 1;
                    fpp[i].dest = realloc(fpp[i].dest, fpp[i].dest_count * sizeof(char*));
                    fpp[i].dest[fpp[i].dest_count - 1] = p2;
                    dup = true; break;
                }
            }
            if(!dup)
            {
                fpp[fppindex].dest = malloc(sizeof(char*));
                fpp[fppindex].dest[0] = p2;
                fpp[fppindex].dest_count = 1;
                fppindex += 1;
                if(fppindex == fppcount) 
                { fppcount += 1; fpp = realloc(fpp, fppcount * sizeof(struct filepathpair)); }
            }
        } else {
            count++;
        }
    }
    fclose(reg);
    fppcount = fppindex;
    fpp = realloc(fpp, fppcount * sizeof(struct filepathpair));

    for(int32_t i = 0; i < fppcount; i++)
    {
        printf("local: %s, dest:\n", fpp[i].local);
        for(int32_t j = 0; j < fpp[i].dest_count; j++)
        {
            printf("  - %s\n", fpp[i].dest[j]);
        }
    }
    write_dps_bin_pkg(fpp, fppcount, argv[1]);
}
