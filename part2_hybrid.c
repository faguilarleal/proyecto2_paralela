#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/des.h>
#include <mpi.h>
#include <omp.h>

#define REQ_TAG 1
#define WORK_TAG 2
#define STOP_TAG 3
#define FOUND_TAG 4
#define CHECK_EVERY 1024UL

/*
Convierte numero -> clave DES válida

Convierte una clave numerica de 64 bits en una estructuea DES_cblock de 8 bytes , 
que es el formato que requiere OpenSSL para cifrar o descifrar con DES
*/
static void key_from_ull(unsigned long long key, DES_cblock *k_out) {
    for (int i = 0; i < 8; i++) (*k_out)[7 - i] = (key >> (i * 8)) & 0xFF;
    DES_set_odd_parity(k_out);
}

/*
Aplicar DES (ECB) bloque a bloque

Cifra o descifra un bloque de datos usando DES en modo ECB

Parámetros:
key: clave numérica a probar
buf: puntero al buffer con el texto (entrada/salida)
len: longitud del buffer (múltiplo de 8)
mode: DES_ENCRYPT o DES_DECRYPT
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
    if (!out) { perror("malloc"); exit(1); }
    memcpy(out, in, in_len);
    if (pad) memset(out + in_len, 0, pad);
    return out;
}

/* búsqueda segura de keyword dentro de un buffer binario (no depende de '\0') */
int contains_keyword_bin(const unsigned char *buf, size_t buf_len, const char *keyword, size_t key_len) {
    if (key_len == 0 || buf_len < key_len) return 0;
    for (size_t i = 0; i + key_len <= buf_len; ++i) {
        if (memcmp(buf + i, keyword, key_len) == 0) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    int rank, size; 
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc < 3) {
        if (rank == 0)
            printf("Uso: %s <start_key> <bits_or_range> [chunk_size] [timeout_seconds]\n", argv[0]);
        MPI_Finalize();
        return 1;
    }

    unsigned long long start_key = strtoull(argv[1], NULL, 10);
    unsigned long long v = strtoull(argv[2], NULL, 10);
    unsigned long long range = (v <= 63) ? (1ULL << v) : v;
    unsigned long long chunk_size = (argc >= 4) ? strtoull(argv[3], NULL, 10) : 1000000ULL;
    double timeout_seconds = (argc >= 5) ? atof(argv[4]) : 0.0;

    /* Master lee msg.txt y prepara datos */
    char text[256] = {0};
    size_t text_len = 0;
    size_t padded_len = 0;
    unsigned char *ptext = NULL;
    unsigned char encrypted[256];
    memset(encrypted, 0, sizeof(encrypted));

    if (rank == 0) {
        FILE *f = fopen("msg.txt","r");
        if (!f) { perror("msg.txt"); MPI_Finalize(); return 1; }
        if (!fgets(text, sizeof(text), f)) { 
            fprintf(stderr,"msg.txt vacio o error\n"); 
            fclose(f); 
            MPI_Finalize(); 
            return 1; 
        }
        fclose(f);
        /* quitar '\n' final si existe */
        text_len = strlen(text);
        if (text_len && text[text_len - 1] == '\n') { text[--text_len] = '\0'; }

        ptext = pad_buffer((unsigned char *)text, text_len, &padded_len);
        if (padded_len > sizeof(encrypted)) {
            fprintf(stderr, "Texto demasiado largo (max %zu)\n", sizeof(encrypted));
            free(ptext);
            MPI_Finalize();
            return 1;
        }

        /* master cifra el plaintext con start_key para crear el "encrypted" de prueba */
        memcpy(encrypted, ptext, padded_len);
        des_ecb_encrypt_buffer(start_key, encrypted, padded_len, DES_ENCRYPT);
    }

    /* Broadcast: primero padded_len y text_len, luego el buffer cifrado (solo padded_len bytes) */
    unsigned long long b_padded_len = padded_len;
    unsigned long long b_text_len = text_len;
    MPI_Bcast(&b_padded_len, 1, MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD);
    MPI_Bcast(&b_text_len, 1, MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD);
    padded_len = (size_t)b_padded_len;
    text_len = (size_t)b_text_len;

    /* ahora broadcast del buffer cifrado (usar padded_len como count) */
    MPI_Bcast(encrypted, (int)padded_len, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);

    double start_time = MPI_Wtime();
    unsigned long long local_tested = 0ULL;
    unsigned long long found_key = 0ULL;
    int found = 0;

    if (rank == 0) {
        /* MASTER */
        unsigned long long next = 0;
        int workers_active = size - 1;
        MPI_Status st;

        while (workers_active > 0) {
            double now = MPI_Wtime();
            if (timeout_seconds > 0.0 && (now - start_time) >= timeout_seconds) {
                printf("Master: Timeout alcanzado (%.1f s). Enviando STOP a todos...\n", timeout_seconds);
                for (int i = 1; i < size; i++)
                    MPI_Send(NULL, 0, MPI_UNSIGNED_LONG_LONG, i, STOP_TAG, MPI_COMM_WORLD);
                break;
            }

            int dummy;
            MPI_Recv(&dummy, 1, MPI_INT, MPI_ANY_SOURCE, REQ_TAG, MPI_COMM_WORLD, &st);
            int src = st.MPI_SOURCE;

            if (next >= range) {
                MPI_Send(NULL, 0, MPI_UNSIGNED_LONG_LONG, src, STOP_TAG, MPI_COMM_WORLD);
                workers_active--;
            } else {
                unsigned long long s = start_key + next;
                unsigned long long e = start_key + ((next + chunk_size < range) ? next + chunk_size : range);
                unsigned long long rng[2] = {s, e};
                MPI_Send(rng, 2, MPI_UNSIGNED_LONG_LONG, src, WORK_TAG, MPI_COMM_WORLD);
                next += chunk_size;
            }

            /* comprobar si algún worker notificó FOUND */
            int flag = 0;
            MPI_Iprobe(MPI_ANY_SOURCE, FOUND_TAG, MPI_COMM_WORLD, &flag, &st);
            if (flag) {
                unsigned long long recv_key;
                MPI_Recv(&recv_key, 1, MPI_UNSIGNED_LONG_LONG, st.MPI_SOURCE, FOUND_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                found = 1;
                found_key = recv_key;
                printf("Master: Clave encontrada %llu por rank %d\n", recv_key, st.MPI_SOURCE);
                for (int i = 1; i < size; i++)
                    MPI_Send(NULL, 0, MPI_UNSIGNED_LONG_LONG, i, STOP_TAG, MPI_COMM_WORLD);
                break;
            }
        }
    } else {
        /* WORKERS */
        MPI_Status st;
        unsigned long long rng[2];

        while (1) {
            int req = 1;
            MPI_Send(&req, 1, MPI_INT, 0, REQ_TAG, MPI_COMM_WORLD);
            MPI_Recv(rng, 2, MPI_UNSIGNED_LONG_LONG, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &st);
            if (st.MPI_TAG == STOP_TAG) break;

            unsigned long long s = rng[0], e = rng[1];

            int stop_omp = 0;

            #pragma omp parallel shared(stop_omp)
            {
                unsigned long long local_count = 0ULL;

                #pragma omp for schedule(static)
                for (unsigned long long key = s; key < e; ++key) {
                    #pragma omp flush(stop_omp)
                    if (stop_omp) continue;

                    unsigned char tmp[256];
                    memcpy(tmp, encrypted, padded_len);
                    des_ecb_encrypt_buffer(key, tmp, padded_len, DES_DECRYPT);
                    local_count++;

                    if ((local_count % CHECK_EVERY) == 0) {
                        double now = MPI_Wtime();
                        if (timeout_seconds > 0.0 && (now - start_time) >= timeout_seconds) {
                            #pragma omp critical
                            { stop_omp = 1; }
                            #pragma omp flush(stop_omp)
                            continue;
                        }
                    }

                    if (contains_keyword_bin(tmp, padded_len, "es una prueba de", strlen("es una prueba de"))) {
                        /* si aparenta contener la keyword, re-encriptar el plaintext original con esa clave y comparar con encrypted */
                        unsigned char reenc[256];
                        /* reenc = ptext cifrado con 'key' -- pero workers no tienen ptext (solo master),
                           así que en lugar de reenc-compare local, avisamos al master con la clave candidata;
                           master hará la verificación final y propagará STOP. */
                        #pragma omp critical
                        {
                            MPI_Send(&key, 1, MPI_UNSIGNED_LONG_LONG, 0, FOUND_TAG, MPI_COMM_WORLD);
                            stop_omp = 1;
                        }
                        #pragma omp flush(stop_omp)
                        continue;
                    }
                } /* fin for */

                #pragma omp atomic
                local_tested += local_count;
            } /* fin parallel */

            /* después de procesar rango, loop pide nuevo trabajo o STOP según master */
        } /* fin while worker loop */

        /* NO Reduce aquí (se hace más abajo) */
    }

    /* reduce global de claves probadas */
    unsigned long long total_tested = 0;
    MPI_Reduce(&local_tested, &total_tested, 1, MPI_UNSIGNED_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

    double total_time = MPI_Wtime() - start_time;

    double max_elapsed = 0.0;
    MPI_Reduce(&total_time, &max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        // double rate = total_time > 0 ? (double)total_tested / total_time : 0;
        double rate = max_elapsed > 0 ? (double)total_tested / max_elapsed : 0;

        printf("\n== Métricas ==\n");
        
        printf("Tiempo total de ejecucion (max entre procesos): %.2f s\n", max_elapsed);
        printf("Tiempo total de ejecucion: %.2f s\n", total_time);
        printf("Total claves probadas: %llu\n", total_tested);
        printf("Velocidad estimada: %.2f keys/s\n", rate);

        if (found) {
            /* master descifra el texto con la clave encontrada y muestra solo los text_len bytes */
            unsigned char final_dec[256];
            memcpy(final_dec, encrypted, padded_len);
            des_ecb_encrypt_buffer(found_key, final_dec, padded_len, DES_DECRYPT);
            /* asegurar terminador */
            if (text_len < sizeof(final_dec)) final_dec[text_len] = '\0';
            printf("Resultado: Clave encontrada = %llu\n", found_key);
            printf("Texto descifrado: %s\n", (char*)final_dec);
        } else if (timeout_seconds > 0.0 && total_time >= timeout_seconds) {
            printf("Resultado: TIMEOUT (%.0f s)\n", timeout_seconds);
        } else {
            printf("Resultado: Rango agotado sin clave.\n");
        }
        printf("===============\n");
    }

    if (ptext) free(ptext);
    MPI_Finalize();
    return 0;
}
