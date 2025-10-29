// para cifrar/descifrar archivos con DES (ECB)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/des.h>
#include <mpi.h>

// Convierte un entero (unsigned long long) en un DES_cblock (8 bytes)
static void key_from_ull(unsigned long long key, DES_cblock *k_out) {
    for (int i = 0; i < 8; i++) {
        (*k_out)[7 - i] = (key >> (i * 8)) & 0xFF;
    }
    DES_set_odd_parity(k_out);
}

// Lee todo un archivo en memoria
// Devuelve buffer y setea *len
// Caller debe free() el buffer
unsigned char *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) { 
        fclose(f); return NULL; 
    }
    unsigned char *buf = malloc(len);
    if (!buf) { 
        fclose(f); return NULL; 
    }
    if (fread(buf, 1, len, f) != (size_t)len) {
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *len_out = (size_t)len;
    return buf;
}

// Escribe buffer de len bytes a archivo
int write_file(const char *path, const unsigned char *buf, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (fwrite(buf, 1, len, f) != len) { 
        fclose(f);
        return -1; 
    }
    fclose(f);
    return 0;

}

// Padding: pad con ceros hasta múltiplo de 8
// Devolvemos nuevo buffer (malloc), y new_len (multiplo de 8)
unsigned char *pad_buffer(const unsigned char *in, size_t in_len, size_t *out_len) {
    size_t rem = in_len % 8;
    size_t pad = (rem == 0) ? 0 : (8 - rem);
    *out_len = in_len + pad;
    
    unsigned char *out = malloc(*out_len);
    if (!out) return NULL;
    
    memcpy(out, in, in_len);
    if (pad) memset(out + in_len, 0, pad);
    return out;
}

void des_ecb_encrypt_buffer(unsigned long long key, unsigned char *buf, size_t len) {
    DES_cblock k;
    DES_key_schedule schedule;
    key_from_ull(key, &k);
    DES_set_key_unchecked(&k, &schedule);

    for (size_t i = 0; i < len; i += 8) {
        DES_ecb_encrypt((DES_cblock *)(buf + i), (DES_cblock *)(buf + i), &schedule, DES_ENCRYPT);
    }
}

void des_ecb_decrypt_buffer(unsigned long long key, unsigned char *buf, size_t len) {
    DES_cblock k;
    DES_key_schedule schedule;
    key_from_ull(key, &k);
    DES_set_key_unchecked(&k, &schedule);

    for (size_t i = 0; i < len; i += 8) {
        DES_ecb_encrypt((DES_cblock *)(buf + i), (DES_cblock *)(buf + i), &schedule, DES_DECRYPT);
    }
}

void usage(const char *prog) {
    fprintf(stderr,
        "Uso:\n"
        "  %s encrypt <input.txt> <out.bin> <key>\n"
        "  %s decrypt <in.bin> <out.txt> <key>\n"
        "Ejemplo:\n"
        "  %s encrypt msg.txt msg.enc 42\n"
        "  %s decrypt msg.enc msg.dec.txt 42\n",
        prog, prog, prog, prog);
}

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv); 
    if (argc != 5) {
        usage(argv[0]);
        MPI_Finalize();
        return 1;
    }

    const char *mode = argv[1];
    const char *inpath = argv[2];
    const char *outpath = argv[3];
    unsigned long long key = strtoull(argv[4], NULL, 10);

    if (strcmp(mode, "encrypt") == 0) {
        size_t in_len;
        unsigned char *inbuf = read_file(inpath, &in_len);
        if (!inbuf) { 
            fprintf(stderr, "Error leyendo %s\n", inpath); 
            MPI_Finalize(); 
            return 2; 
        }

        size_t padded_len;
        unsigned char *pbuf = pad_buffer(inbuf, in_len, &padded_len);
        free(inbuf);
        if (!pbuf) { 
            fprintf(stderr, "Error en padding\n"); 
            MPI_Finalize(); 
            return 3; 
        }

        des_ecb_encrypt_buffer(key, pbuf, padded_len);

        // Guardar
        // primero escribimos el tamaño original (8 bytes little-endian) para poder recover exacto
        FILE *f = fopen(outpath, "wb");
        if (!f) { 
            fprintf(stderr, "Error creando %s\n", outpath); 
            free(pbuf); 
            MPI_Finalize(); 
            return 4; 
        }
        // escribir original length (8 bytes)
        unsigned long long orig_len = (unsigned long long)in_len;
        fwrite(&orig_len, sizeof(unsigned long long), 1, f);
        // escribir bloques cifrados
        fwrite(pbuf, 1, padded_len, f);
        fclose(f);
        printf("Archivo cifrado escrito en %s (longitud original: %llu bytes, bloques: %zu bytes)\n", outpath, orig_len, padded_len);
        free(pbuf);

    } else if (strcmp(mode, "decrypt") == 0) {
        // leer archivo
        // primero length original (8 bytes), luego los datos
        FILE *f = fopen(inpath, "rb");
        if (!f) { 
            fprintf(stderr, "Error leyendo %s\n", inpath); MPI_Finalize(); 
            return 2; 
        }
        unsigned long long orig_len;
        if (fread(&orig_len, sizeof(unsigned long long), 1, f) != 1) { 
            fprintf(stderr, "Formato invalido (no hay header)\n"); 
            fclose(f); 
            MPI_Finalize(); 
            return 5;
        }
        // leer rest
        fseek(f, 0, SEEK_END);
        long file_size = ftell(f);
        long enc_size = file_size - sizeof(unsigned long long);
        if (enc_size <= 0) { 
            fprintf(stderr, "Archivo cifrado vacio\n"); 
            fclose(f); 
            MPI_Finalize(); 
            return 6; 
        }
        fseek(f, sizeof(unsigned long long), SEEK_SET);
        unsigned char *encbuf = malloc(enc_size);
        if (!encbuf) { 
            fclose(f); MPI_Finalize(); 
            return 7; 
        }
        if (fread(encbuf, 1, enc_size, f) != (size_t)enc_size) { 
            fprintf(stderr, "Error leyendo bloque cifrado\n"); 
            free(encbuf); 
            fclose(f); 
            MPI_Finalize(); 
            return 8; 
        }
        fclose(f);

        if (enc_size % 8 != 0) { 
            fprintf(stderr, "Tamaño cifrado no múltiplo de 8\n"); 
            free(encbuf); 
            MPI_Finalize(); 
            return 9; 
        }

        des_ecb_decrypt_buffer(key, encbuf, enc_size);

        // escribir solo los primeros orig_len bytes
        FILE *out = fopen(outpath, "wb");
        if (!out) { 
            fprintf(stderr, "Error creando %s\n", outpath); 
            free(encbuf); 
            MPI_Finalize(); 
            return 10; 
        }
        fwrite(encbuf, 1, orig_len, out);
        fclose(out);
        free(encbuf);
        printf("Archivo descifrado escrito en %s (original: %llu bytes)\n", outpath, orig_len);
    } else {
        usage(argv[0]);
        MPI_Finalize();
        return 11;
    }

    MPI_Finalize();
    return 0;
}
