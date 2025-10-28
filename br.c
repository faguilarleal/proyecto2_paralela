#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <mpi.h>
#include <openssl/des.h>

#define CHECK_INTERVAL 10000 
/* Prepara un DES_cblock (8 bytes) a partir de una clave de 56 bits (key56).
   Rellena los 8 bytes con los octetos de key56 y ajusta paridad impar requerida por DES. */
static void prepare_key_from_uint64(uint64_t key56, DES_cblock *out) {
    for (int i = 0; i < 8; ++i) {
        (*out)[7 - i] = (unsigned char)(key56 & 0xFFULL);
        key56 >>= 8;
    }
    DES_set_odd_parity(out);
}

/* Desencripta len bytes en 'data' in-place con la clave key56 (interpreta como 56-bit key). */
static void des_decrypt_blocks(uint64_t key56, unsigned char *data, int len) {
    DES_cblock key;
    DES_key_schedule ks;
    prepare_key_from_uint64(key56, &key);
    DES_set_key_unchecked(&key, &ks);

    // procesar por bloques de 8 bytes (DES ECB)
    for (int offset = 0; offset < len; offset += 8) {
        DES_cblock in, out;
        memset(in, 0, 8);
        int chunk = (len - offset >= 8) ? 8 : (len - offset);
        memcpy(in, data + offset, chunk);
        DES_ecb_encrypt(&in, &out, &ks, DES_DECRYPT);
        memcpy(data + offset, out, chunk);
        // si chunk < 8, los bytes restantes en out son irrelevantes
    }
}

/* Devuelve 1 si al desencriptar con key aparece la cadena ' the ' (heurística simple). */
static int tryKey(uint64_t key, const unsigned char *ciph, int len) {
    unsigned char temp[128];
    if (len > (int)sizeof(temp) - 1) return 0;
    memcpy(temp, ciph, len);
    des_decrypt_blocks(key, temp, len);
    // aseguramos terminador para strstr
    temp[len] = '\0';
    return strstr((char *)temp, " the ") != NULL;
}

/* Cipher original: 16 bytes (dos bloques DES). */
unsigned char cipher[] = {
    108, 245, 65, 63, 125, 200, 150, 66,
    17, 170, 207, 170, 34, 31, 70, 215
};

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);
    MPI_Comm comm = MPI_COMM_WORLD;
    int N, id;
    MPI_Comm_size(comm, &N);
    MPI_Comm_rank(comm, &id);

    // Para pruebas poner algo pequeño, por ejemplo (1ULL<<24).
    // Si quieres el espacio completo DES: uint64_t upper = (1ULL<<56);
    uint64_t upper = (1ULL << 24); // <<-- cambiar para producción
    if (argc > 1) {
        // permite pasar el exponente en la línea de comandos: e.g. ./br 28 para 2^28
        int bits = atoi(argv[1]);
        if (bits > 0 && bits <= 56) upper = (1ULL << bits);
    }

    uint64_t range_per_node = upper / (uint64_t)N;
    uint64_t mylower = range_per_node * (uint64_t)id;
    uint64_t myupper = range_per_node * (uint64_t)(id + 1) - 1;
    if (id == N - 1) myupper = upper - 1;

    uint64_t found = 0;   // 0 = no encontrado
    int ciphlen = sizeof(cipher); // 16

    uint64_t check_counter = 0;

    for (uint64_t k = mylower; k <= myupper; ++k) {
        if (found != 0) break;

        if (tryKey(k, cipher, ciphlen)) {
            found = k;
        }

        // sincronizamos de forma periódica para reducir overhead
        if ((++check_counter % CHECK_INTERVAL) == 0) {
            // Reducimos usando MAX: si algún proceso encontró (>0), todos lo verán
            MPI_Allreduce(MPI_IN_PLACE, &found, 1, MPI_UNSIGNED_LONG_LONG, MPI_MAX, comm);
            if (found != 0) break;
        }
    }

    // Una sincronización final para asegurarnos que todos conozcan 'found'
    MPI_Allreduce(MPI_IN_PLACE, &found, 1, MPI_UNSIGNED_LONG_LONG, MPI_MAX, comm);

    if (found != 0) {
        if (id == 0) {
            unsigned char out[32];
            memcpy(out, cipher, ciphlen);
            des_decrypt_blocks(found, out, ciphlen);
            out[ciphlen] = '\0';
            printf("Clave encontrada: %llu\nTexto: %s\n", (unsigned long long)found, out);
        }
    } else {
        if (id == 0) {
            printf("No se encontró la clave en el rango probado (0 .. %llu)\n", (unsigned long long)(upper - 1));
        }
    }

    MPI_Finalize();
    return 0;
}
