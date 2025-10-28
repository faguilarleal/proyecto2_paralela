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

char search[] = " the ";
int tryKey(unsigned long long key, char *ciph, int len) {
  char temp[len + 1];
  memcpy(temp, ciph, len);
  temp[len] = 0;
  decrypt(key, temp, len);
  return strstr(temp, search) != NULL;
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

  for (unsigned long long i = mylower; i < myupper && (found == 0); ++i) {
    if (tryKey(i, (char *)cipher, ciphlen)) {
      found = i;
      for (int node = 0; node < N; node++) {
        MPI_Send(&found, 1, MPI_UNSIGNED_LONG_LONG, node, 0, MPI_COMM_WORLD);
      }
      break;
    }
  }

  if (id == 0) {
    MPI_Wait(&req, &st);
    decrypt(found, (char *)cipher, ciphlen);
    printf("Clave encontrada: %llu\nTexto: %s\n", found, cipher);
  }

  MPI_Finalize();
  return 0;
}
