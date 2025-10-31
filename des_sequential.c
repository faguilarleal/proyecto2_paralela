#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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
  unsigned long long upper = (1ULL << 24);
  int ciphlen = sizeof(cipher) - 1;
  unsigned long long found = 0;

  printf("Iniciando búsqueda de clave en rango [0, %llu)\n", upper);

  // Búsqueda secuencial de la clave
  for (unsigned long long i = 0; i < upper; ++i) {
    if (tryKey(i, (char *)cipher, ciphlen)) {
      found = i;
      printf("¡Clave encontrada: %llu!\n", found);
      break;
    }
    
    // // Mostrar progreso cada millón de claves probadas
    // if ((i % 1000000) == 0) {
    //   // printf("Probadas %llu claves...\n", i);
    //   fflush(stdout);
    // }
  }

  // Mostrar resultado final
  if (found != 0) {
    decrypt(found, (char *)cipher, ciphlen);
    printf("\n=== RESULTADO ===\n");
    printf("Clave encontrada: %llu\n", found);
    printf("Texto descifrado: %s\n", cipher);
  } else {
    printf("\nNo se encontró la clave en el rango especificado.\n");
  }

  return 0;
}