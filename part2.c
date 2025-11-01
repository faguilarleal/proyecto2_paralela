// Parte B
// para cifrar/descifrar archivos con DES (ECB)
// librerias
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/des.h>
#include <mpi.h> //paraleliza el programa para distribuir la carga entre varios porcesos
#include <errno.h> 

// cifrado

/*
Convierte numero -> clave DES válida

Convierte una clave numerica de 64 bits en una estructuea DES_cblock de 8 bytes , 
que es el formato que requiere OpenSSL para cifrar o descifrar con DES
*/
static void key_from_ull(unsigned long long key, DES_cblock *k_out) {
    for (int i = 0; i < 8; i++)
        (*k_out)[7 - i] = (key >> (i * 8)) & 0xFF;
    //ajusta los bits de paiedad como el estandar de des
    DES_set_odd_parity(k_out);
}

/*
Aplica DES (ECB) bloque a bloque

Cifra o descifra un bloque de datos usando DES en modo ECB

Parámetros:
key: clave numérica a probar.
buf: puntero al buffer con el texto (entrada/salida).
len: longitud del buffer (múltiplo de 8).
mode: DES_ENCRYPT o DES_DECRYPT.
*/
void des_ecb_encrypt_buffer(unsigned long long key, unsigned char *buf, size_t len, int mode) {
    DES_cblock k;
    DES_key_schedule schedule;
    key_from_ull(key, &k);
    DES_set_key_unchecked(&k, &schedule);

    for (size_t i = 0; i < len; i += 8)
        DES_ecb_encrypt((DES_cblock *)(buf + i), (DES_cblock *)(buf + i), &schedule, mode);
}

/*
Padding: Asegura que el texto sea múltiplo de 8 bytes
*/
unsigned char *pad_buffer(const unsigned char *in, size_t in_len, size_t *out_len) {
    size_t pad = (8 - (in_len % 8)) % 8;
    *out_len = in_len + pad;

    unsigned char *out = malloc(*out_len);

    memcpy(out, in, in_len);
    if (pad) memset(out + in_len, 0, pad);
    return out;
}

/*
Detecta si el texto descifrado contiene la frase clave
*/
int contains_keyword(const char *text, const char *keyword) {
    return strstr(text, keyword) != NULL;
}

/*
Lee texto, cifra, reparte trabajo MPI, busca la clave y mide tiempos
*/
int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc < 2) {
        if (rank == 0)
            printf("Uso: %s <clave inicial> [rango_bits_o_cantidad]\n", argv[0]);
        MPI_Finalize();
        return 1;
    }

    // Leer texto desde archivo
    FILE *file = fopen("msg.txt", "r");
    if (!file) {
        if (rank == 0) perror("Error al abrir msg.txt");
        MPI_Finalize();
        return 1;
    }

    char text[256];
    fgets(text, sizeof(text), file);
    fclose(file);
    size_t text_len = strlen(text);

    const char *keyword = "es una prueba de";
    unsigned long long start_key = strtoull(argv[1], NULL, 10);

    //unsigned long long range = 1000000000ULL;  // 1e9
    // unsigned long long range = (1ULL << 56);

    // default si no se pasa argv[2]
    unsigned long long range = (1ULL << 56);

    // en el caso que si hayan 3 argumentos
    if (argc >= 3) {
        errno = 0;
        char *endptr = NULL;
        unsigned long long v = strtoull(argv[2], &endptr, 10);
        if (errno != 0 || endptr == NULL || *endptr != '\0') {
            if (rank == 0) fprintf(stderr, "Argumento inválido para rango/bits: '%s'\n", argv[2]);
            MPI_Finalize();
            return 1;
        }

        if (v == 0) {
            if (rank == 0) fprintf(stderr, "Segundo argumento no puede ser 0\n");
            MPI_Finalize();
            return 1;
        }

        if (v <= 63) {
            // numero de bits: range = 2^v
            if (v > 56 && rank == 0) {
                fprintf(stderr, "Advertencia: está pidiendo %llu bits; DES efectivo usa 56 bits y esto puede ser impráctico.\n", v);
            }
            if (v >= 64) {
                if (rank == 0) fprintf(stderr, "Bits >= 64 no son soportados de forma segura.\n");
                MPI_Finalize();
                return 1;
            }
            range = (1ULL << v);
        } else {
            //cantidad de claves
            range = v;
        }

    }

    // Division del rango 
    unsigned long long base = range / size;
    unsigned long long resto = range % size;

    unsigned long long local_start_key = start_key + rank * base + (rank < resto ? rank : resto);
    unsigned long long local_end_key   = local_start_key + base + (rank < resto ? 1 : 0);

    unsigned char encrypted[256];
    unsigned char decrypted[256];
    memset(encrypted, 0, sizeof(encrypted));
    memset(decrypted, 0, sizeof(decrypted));

    size_t padded_len;
    unsigned char *ptext = pad_buffer((unsigned char *)text, text_len, &padded_len);

    //  Cifrar texto original (solo proceso 0) 
    if (rank == 0) {
        memcpy(encrypted, ptext, padded_len);
        des_ecb_encrypt_buffer(start_key, encrypted, padded_len, DES_ENCRYPT);
        printf("Texto cifrado (DES ECB): ");
        for (size_t i = 0; i < padded_len; i++)
            printf("%02X ", encrypted[i]);
        printf("\n");
    }

    // Compartir texto cifrado a todos
    MPI_Bcast(encrypted, 256, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);
    MPI_Bcast(&padded_len, 1, MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD);

    //  Búsqueda paralela 
    double start_time = MPI_Wtime();
    unsigned long long local_tested = 0;
    unsigned long long found_key = 0;
    int global_found = 0;

    //  Postear recepción no bloqueante para notificación de clave encontrada
    unsigned long long found_msg = 0;
    MPI_Request found_req;
    MPI_Irecv(&found_msg, 1, MPI_UNSIGNED_LONG_LONG, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &found_req);

    const unsigned long long POLL_INTERVAL = 10000ULL;

    for (unsigned long long key = local_start_key; key < local_end_key; ++key) {
        // Revisar cada cierto intervalo si otro proceso encontró la clave
        if ((key - local_start_key) % POLL_INTERVAL == 0) {
            int got = 0;
            MPI_Test(&found_req, &got, MPI_STATUS_IGNORE);
            if (got) {
                found_key = found_msg;
                global_found = 1;
                break;
            }
        }

        // Probar clave actual
        memcpy(decrypted, encrypted, padded_len);
        des_ecb_encrypt_buffer(key, decrypted, padded_len, DES_DECRYPT);

        local_tested++;

        if (contains_keyword((char *)decrypted, keyword)) {
            unsigned char re_encrypted[256];
            memcpy(re_encrypted, ptext, padded_len);
            des_ecb_encrypt_buffer(key, re_encrypted, padded_len, DES_ENCRYPT);

            if (memcmp(re_encrypted, encrypted, padded_len) == 0) {
                found_key = key;
                global_found = 1;
                printf("Proceso %d confirmó la clave correcta: %llu\n", rank, key);
                fflush(stdout);

                // Notificar a todos los procesos
                for (int dest = 0; dest < size; ++dest)
                    MPI_Send(&found_key, 1, MPI_UNSIGNED_LONG_LONG, dest, 0, MPI_COMM_WORLD);
                break;
            }
        }
    }

    // Limpiar
    int done = 0;
    MPI_Test(&found_req, &done, MPI_STATUS_IGNORE);
    if (!done) {
        MPI_Cancel(&found_req);
        MPI_Request_free(&found_req);
    } else if (found_msg && !global_found) {
        found_key = found_msg;
        global_found = 1;
    }

    // Métricas
    double end_time = MPI_Wtime();
    double total_time = end_time - start_time;
    unsigned long long total_tested = 0;
    double max_elapsed = 0.0;

    MPI_Reduce(&local_tested, &total_tested, 1, MPI_UNSIGNED_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&total_time, &max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        double rate_global = (max_elapsed > 0.0) ? (double)total_tested / max_elapsed : 0.0;

        if (global_found)
            printf("Clave encontrada: %llu\n", found_key);
        else
            printf("No se encontró la clave en el rango.\n");

        printf("== Metricas: ==\n");
        printf("Tiempo total de ejecución: %.6f segundos\n", total_time);
        printf("Total de claves probadas: %llu\n", total_tested);
        printf("Velocidad estimada: %.2f keys/s\n", rate_global);
        printf("Máx. tiempo entre procesos: %.6f s\n", max_elapsed);
        printf("================\n");
    }

    free(ptext);
    MPI_Finalize();
    return 0;
}
