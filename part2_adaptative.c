// Adaptive Search - Búsqueda Adaptativa con Work Stealing
// Implementa distribución dinámica de trabajo entre procesos MPI

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/des.h>
#include <mpi.h>
#include <errno.h>
#include <time.h>

// Tags para comunicación MPI
#define TAG_WORK_REQUEST 1
#define TAG_WORK_RESPONSE 2
#define TAG_FOUND 3
#define TAG_TERMINATE 4

// Configuración de búsqueda adaptativa
#define INITIAL_BLOCK_SIZE 100000ULL    // Tamaño inicial de bloque
#define MIN_BLOCK_SIZE 10000ULL         // Tamaño mínimo de bloque
#define POLL_INTERVAL 5000ULL           // Intervalo para revisar mensajes
#define ADAPTATION_FACTOR 1.5           // Factor de crecimiento de bloques

// Estructura para trabajo dinámico
typedef struct {
    unsigned long long start;
    unsigned long long end;
    int available;
} WorkBlock;

/*
Convierte numero -> clave DES válida
*/
static void key_from_ull(unsigned long long key, DES_cblock *k_out) {
    for (int i = 0; i < 8; i++)
        (*k_out)[7 - i] = (key >> (i * 8)) & 0xFF;
    DES_set_odd_parity(k_out);
}

/*
Aplicar DES (ECB) bloque a bloque
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
MASTER: Gestiona la distribución dinámica de trabajo
*/
void master_adaptive_search(int size, unsigned long long total_range, 
                           unsigned long long start_key,
                           unsigned char *encrypted, size_t padded_len,
                           const char *keyword, unsigned char *ptext,
                           double timeout_seconds, unsigned long long *found_key_out) {
    
    // Inicializar bloques de trabajo
    unsigned long long block_size = INITIAL_BLOCK_SIZE;
    unsigned long long current_pos = start_key;
    unsigned long long found_key = 0;
    int found = 0;
    
    int active_workers = size - 1;
    int *worker_status = calloc(size, sizeof(int)); // 0=idle, 1=working
    
    double start_time = MPI_Wtime();
    
    // Dar trabajo inicial a todos los workers
    for (int i = 1; i < size && current_pos < start_key + total_range; i++) {
        unsigned long long block[2];
        block[0] = current_pos;
        block[1] = current_pos + block_size;
        if (block[1] > start_key + total_range)
            block[1] = start_key + total_range;
        
        MPI_Send(block, 2, MPI_UNSIGNED_LONG_LONG, i, TAG_WORK_RESPONSE, MPI_COMM_WORLD);
        worker_status[i] = 1;
        current_pos = block[1];
    }
    
    // Manejar solicitudes de trabajo dinámicamente
    while (active_workers > 0 && !found) {
        MPI_Status status;
        int message_available;
        
        // Verificar timeout
        if (timeout_seconds > 0.0) {
            double elapsed = MPI_Wtime() - start_time;
            if (elapsed >= timeout_seconds) {
                // Enviar terminación a todos
                for (int i = 1; i < size; i++) {
                    unsigned long long termination[2] = {0, 0};
                    MPI_Send(termination, 2, MPI_UNSIGNED_LONG_LONG, i, TAG_TERMINATE, MPI_COMM_WORLD);
                }
                break;
            }
        }
        
        // Revisar si hay mensajes (no bloqueante)
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &message_available, &status);
        
        if (message_available) {
            if (status.MPI_TAG == TAG_WORK_REQUEST) {
                // Worker solicita más trabajo
                int worker_rank;
                MPI_Recv(&worker_rank, 1, MPI_INT, status.MPI_SOURCE, TAG_WORK_REQUEST, 
                        MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                
                worker_status[worker_rank] = 0;
                
                // Asignar nuevo bloque si hay trabajo disponible
                if (current_pos < start_key + total_range) {
                    unsigned long long block[2];
                    block[0] = current_pos;
                    block[1] = current_pos + block_size;
                    if (block[1] > start_key + total_range)
                        block[1] = start_key + total_range;
                    
                    MPI_Send(block, 2, MPI_UNSIGNED_LONG_LONG, worker_rank, 
                            TAG_WORK_RESPONSE, MPI_COMM_WORLD);
                    worker_status[worker_rank] = 1;
                    current_pos = block[1];
                    
                    // Adaptar tamaño de bloque si es necesario
                    if (block_size < total_range / (size * 10)) {
                        block_size = (unsigned long long)(block_size * ADAPTATION_FACTOR);
                    }
                } else {
                    // No hay más trabajo, enviar señal de terminación
                    unsigned long long termination[2] = {0, 0};
                    MPI_Send(termination, 2, MPI_UNSIGNED_LONG_LONG, worker_rank, 
                            TAG_TERMINATE, MPI_COMM_WORLD);
                    active_workers--;
                }
                
            } else if (status.MPI_TAG == TAG_FOUND) {
                // Clave encontrada por algún worker
                MPI_Recv(&found_key, 1, MPI_UNSIGNED_LONG_LONG, status.MPI_SOURCE, 
                        TAG_FOUND, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                found = 1;
                
                printf("Master: Clave encontrada %llu por rank %d\n", 
                       found_key, status.MPI_SOURCE);
                
                // Notificar a todos los workers que terminen
                for (int i = 1; i < size; i++) {
                    if (i != status.MPI_SOURCE) {
                        MPI_Send(&found_key, 1, MPI_UNSIGNED_LONG_LONG, i, TAG_FOUND, 
                                MPI_COMM_WORLD);
                    }
                }
                active_workers = 0;
            }
        }
    }
    
    *found_key_out = found_key;
    
    free(worker_status);
}

/*
WORKER: Procesa bloques de trabajo dinámicamente
*/
void worker_adaptive_search(int rank, unsigned char *encrypted, size_t padded_len,
                           const char *keyword, unsigned char *ptext,
                           unsigned long long *local_tested_out) {
    
    unsigned long long local_tested = 0;
    unsigned long long found_key = 0;
    int should_continue = 1;
    
    unsigned char decrypted[256];
    
    // Preparar recepción no bloqueante para notificación de clave encontrada
    MPI_Request found_req;
    unsigned long long found_msg = 0;
    MPI_Irecv(&found_msg, 1, MPI_UNSIGNED_LONG_LONG, 0, TAG_FOUND, 
             MPI_COMM_WORLD, &found_req);
    
    while (should_continue) {
        // Solicitar trabajo al master
        MPI_Send(&rank, 1, MPI_INT, 0, TAG_WORK_REQUEST, MPI_COMM_WORLD);
        
        // Recibir bloque de trabajo
        unsigned long long block[2];
        MPI_Status status;
        MPI_Recv(block, 2, MPI_UNSIGNED_LONG_LONG, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        
        if (status.MPI_TAG == TAG_TERMINATE) {
            // No hay más trabajo
            should_continue = 0;
            break;
        }
        
        unsigned long long start = block[0];
        unsigned long long end = block[1];
        
        // Buscar en el bloque asignado
        for (unsigned long long key = start; key < end && should_continue; key++) {
            // Revisar periódicamente si otro proceso encontró la clave
            if ((key - start) % POLL_INTERVAL == 0) {
                int got = 0;
                MPI_Test(&found_req, &got, MPI_STATUS_IGNORE);
                if (got) {
                    found_key = found_msg;
                    should_continue = 0;
                    break;
                }
            }
            
            // Probar clave actual
            memcpy(decrypted, encrypted, padded_len);
            des_ecb_encrypt_buffer(key, decrypted, padded_len, DES_DECRYPT);
            local_tested++;
            
            if (contains_keyword((char *)decrypted, keyword)) {
                // Verificar que realmente es la clave correcta
                unsigned char re_encrypted[256];
                memcpy(re_encrypted, ptext, padded_len);
                des_ecb_encrypt_buffer(key, re_encrypted, padded_len, DES_ENCRYPT);
                
                if (memcmp(re_encrypted, encrypted, padded_len) == 0) {
                    found_key = key;
                    should_continue = 0;
                    
                    // Notificar al master
                    MPI_Send(&found_key, 1, MPI_UNSIGNED_LONG_LONG, 0, TAG_FOUND, 
                            MPI_COMM_WORLD);
                    break;
                }
            }
        }
    }
    
    // Limpiar solicitud pendiente
    int done = 0;
    MPI_Test(&found_req, &done, MPI_STATUS_IGNORE);
    if (!done) {
        MPI_Cancel(&found_req);
        MPI_Request_free(&found_req);
    }
    
    *local_tested_out = local_tested;
}

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size < 2) {
        if (rank == 0)
            printf("Error: Se requieren al menos 2 procesos (1 master + 1 worker)\n");
        MPI_Finalize();
        return 1;
    }

    if (argc < 2) {
        if (rank == 0)
            printf("Uso: %s <clave_inicial> [rango_bits_o_cantidad] [timeout_seg]\n", argv[0]);
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
    unsigned long long range = (1ULL << 56);
    double timeout_seconds = 0.0;  // 0 = sin timeout

    if (argc >= 3) {
        errno = 0;
        char *endptr = NULL;
        unsigned long long v = strtoull(argv[2], &endptr, 10);
        
        if (errno != 0 || endptr == NULL || *endptr != '\0') {
            if (rank == 0) 
                fprintf(stderr, "Argumento inválido para rango/bits: '%s'\n", argv[2]);
            MPI_Finalize();
            return 1;
        }

        if (v == 0) {
            if (rank == 0) fprintf(stderr, "Segundo argumento no puede ser 0\n");
            MPI_Finalize();
            return 1;
        }

        if (v <= 63) {
            range = (1ULL << v);
        } else {
            range = v;
        }
    }
    
    // Timeout opcional
    if (argc >= 4) {
        timeout_seconds = atof(argv[3]);
        if (rank == 0 && timeout_seconds > 0)
            printf("Timeout configurado: %.1f segundos\n", timeout_seconds);
    }

    // Preparar buffers
    unsigned char encrypted[256];
    memset(encrypted, 0, sizeof(encrypted));

    size_t padded_len;
    unsigned char *ptext = pad_buffer((unsigned char *)text, text_len, &padded_len);

    // Cifrar texto original (solo proceso 0/master)
    if (rank == 0) {
        memcpy(encrypted, ptext, padded_len);
        des_ecb_encrypt_buffer(start_key, encrypted, padded_len, DES_ENCRYPT);
    }

    // Compartir datos a todos los procesos
    MPI_Bcast(encrypted, 256, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);
    MPI_Bcast(&padded_len, 1, MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD);

    // Iniciar búsqueda adaptativa
    double start_time = MPI_Wtime();
    unsigned long long local_tested = 0;
    unsigned long long found_key = 0;

    if (rank == 0) {
        // Master coordina la distribución de trabajo
        master_adaptive_search(size, range, start_key, encrypted, padded_len, keyword, ptext, 
                             timeout_seconds, &found_key);
    } else {
        // Workers procesan bloques dinámicamente
        worker_adaptive_search(rank, encrypted, padded_len, keyword, ptext, &local_tested);
    }

    double end_time = MPI_Wtime();
    double total_time = end_time - start_time;
    
    // Recolectar métricas
    unsigned long long total_tested = 0;
    double max_elapsed = 0.0;
    
    MPI_Reduce(&local_tested, &total_tested, 1, MPI_UNSIGNED_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&total_time, &max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        double rate = max_elapsed > 0.0 ? (double)total_tested / max_elapsed : 0.0;
        
        printf("== Métricas ==\n");
        printf("Tiempo total de ejecucion (max entre procesos): %.2f s\n", max_elapsed);
        printf("Tiempo total de ejecucion: %.2f s\n", total_time);
        printf("Total claves probadas: %llu\n", total_tested);
        printf("Velocidad estimada: %.2f keys/s\n", rate);
        
        if (found_key > 0) {
            // Descifrar con la clave encontrada
            unsigned char final_dec[256];
            memcpy(final_dec, encrypted, padded_len);
            des_ecb_encrypt_buffer(found_key, final_dec, padded_len, DES_DECRYPT);
            
            // Asegurar terminación de string
            final_dec[text_len] = '\0';
            
            printf("Resultado: Clave encontrada = %llu\n", found_key);
            printf("Texto descifrado: %s\n", (char*)final_dec);
        } else if (timeout_seconds > 0.0 && total_time >= timeout_seconds) {
            printf("Resultado: TIMEOUT (%.0f s)\n", timeout_seconds);
        } else {
            printf("Resultado: Rango agotado sin clave.\n");
        }
        printf("===============\n");
    }

    free(ptext);
    MPI_Finalize();
    return 0;
}