#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <unistd.h>
#include <openssl/des.h>

void decrypt(unsigned long long key, char *ciph, int len) {
  DES_cblock k;
  DES_key_schedule schedule;

  // Generar bytes de clave desde el entero
  for (int i = 0; i < 8; i++) {
    k[7 - i] = (key >> (i * 8)) & 0xFF;
  }
  DES_set_odd_parity(&k);
  DES_set_key_unchecked(&k, &schedule);

  for (int i = 0; i < len; i += 8) {
    DES_ecb_encrypt((DES_cblock *)(ciph + i), (DES_cblock *)(ciph + i), &schedule, DES_DECRYPT);
  }
}

void encrypt(unsigned long long key, char *ciph, int len) {
  DES_cblock k;
  DES_key_schedule schedule;

  for (int i = 0; i < 8; i++) {
    k[7 - i] = (key >> (i * 8)) & 0xFF;
  }
  DES_set_odd_parity(&k);
  DES_set_key_unchecked(&k, &schedule);

  for (int i = 0; i < len; i += 8) {
    DES_ecb_encrypt((DES_cblock *)(ciph + i), (DES_cblock *)(ciph + i), &schedule, DES_ENCRYPT);
  }
}

int tryKey(unsigned long long key, char *ciph, int len) {
  char temp[len + 1];
  memcpy(temp, ciph, len);
  temp[len] = 0;
  decrypt(key, temp, len);
  
  // Buscar diferentes variantes de "the"
  if (strstr(temp, " the ") != NULL ||
      strstr(temp, "the ") != NULL ||
      strstr(temp, " the") != NULL ||
      strstr(temp, "The ") != NULL ||
      strstr(temp, " The") != NULL) {
    return 1;
  }
  
  int printable = 0;
  for(int i = 0; i < len; i++) {
    if((temp[i] >= 32 && temp[i] <= 126) || temp[i] == '\n' || temp[i] == '\t')
      printable++;
  }
  
  if(printable > len * 0.85) {
    return 1;
  }
  
  return 0;
}
unsigned char cipher[] = {
  108, 245, 65, 63, 125, 200, 150, 66,
  17, 170, 207, 170, 34, 31, 70, 215, 0
};

int main(int argc, char *argv[]) {
  int N, id;
  unsigned long long upper = (1ULL << 24);
  unsigned long long mylower, myupper;
  MPI_Status st;
  MPI_Request req;
  int ciphlen = sizeof(cipher) - 1;
  MPI_Comm comm = MPI_COMM_WORLD;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(comm, &N);
  MPI_Comm_rank(comm, &id);

  unsigned long long range_per_node = upper / N;
  mylower = range_per_node * id;
  myupper = range_per_node * (id + 1) - 1;
  if (id == N - 1)
    myupper = upper;

  unsigned long long found = 0;
  MPI_Irecv(&found, 1, MPI_UNSIGNED_LONG_LONG, MPI_ANY_SOURCE, MPI_ANY_TAG, comm, &req);

  printf("Rank %d: iniciando rango [%llu, %llu)\n", id, mylower, myupper);

  for (unsigned long long i = mylower; i < myupper && (found == 0); ++i) {
    if (tryKey(i, (char *)cipher, ciphlen)) {
      found = i;
      for (int node = 0; node < N; node++) {
        MPI_Send(&found, 1, MPI_UNSIGNED_LONG_LONG, node, 0, MPI_COMM_WORLD);
      }
      printf("Rank %d: encontró la clave %llu\n", id, found);
      break;
    }
    if ((i % 1000000) == 0) {
      printf("Rank %d: probadas %llu claves...\n", id, i - mylower);
      fflush(stdout);
    }
  }

  printf("Rank %d: terminó su bucle sin encontrar clave.\n", id);

  unsigned long long global_found = 0;
  MPI_Allreduce(&found, &global_found, 1, MPI_UNSIGNED_LONG_LONG, MPI_MAX, comm);

  if (global_found == 0) {
    // nadie encontró la clave se cancela para evitar bloqueo
    MPI_Cancel(&req);
    MPI_Request_free(&req);
    if (id == 0) {
      printf("Nadie encontró la clave. cancelado Irecv y saliendo.\n");
      fflush(stdout);
    }
  } else {
    // alguien encontró la clave se espera el mensaje
    if (id == 0) {
      printf("Algún proceso encontró la clave, esperando MPI_Wait para recibir valor...\n");
      fflush(stdout);
    }
    MPI_Wait(&req, &st);
  }


  if (id == 0 && global_found != 0) {
    decrypt(global_found, (char *)cipher, ciphlen);
    printf("Clave encontrada: %llu\n", (unsigned long long)global_found);
    fflush(stdout);
  } 

  MPI_Finalize();
  return 0;
}
