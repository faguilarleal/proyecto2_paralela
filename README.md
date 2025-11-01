```cmd
sudo apt update
sudo apt install -y libssl-dev openmpi-bin libopenmpi-dev

```

## Parte A

Secuencial
```cmd
gcc -o des_sequential des_sequential.c -lssl -lcrypto
```

```cmd
mpicc br.c -lssl -lcrypto -o br
```

```cmd
mpirun -np 4 ./br
```


## Parte B

Descripcion del programa: 
implementa brute force sobre un texto cifrado con el algoritmo des en modo ECB (Electronic Codebook), utilizando MPI (Message Passing Interface) para distribuir el trabajo entre varios procesos. 
Y cada proceso prueba un rango distinto de las llaves hasta que llegue a encontrar aquella que logre descifrar correctamente el texto original. 

ECB: es el modo de operacion mas simplel de un cifrador por bloques, en donde cada bloque de datos se cifra de manera independiente con la misma clave.  

Compilar
```cmd
mpicc part2.c -o part2 -lcrypto

```

Crear o sobreescribir el archivo msg
```cmd
echo -n "Esta es una prueba de proyecto 2" > msg.txt
```


```cmd
mpirun -np 4 ./part2 123456L 56
```


correr todo de una vez
```cmd
mpicc part2.c -o part2 -lcrypto &&  mpirun -np 4 ./part2 123456L 56
```


Compilar y correr part2_hybrid.c
```cmd
mpicc -fopenmp part2_hybrid.c -o part2_hybrid -lcrypto && mpirun -np 4 ./part2_hybrid 18014398509481984L 56 100000 60
```



Compilar y correr part2_adaptative.c
```cmd
mpicc -o adaptive_search part2_adaptative.c -lssl -lcrypto

# Llave fácil (encontrada rápido)
mpirun -np 4 ./adaptive_search 123456 10000000

# Llave mediana (2^56/2 + 2^56/8)
mpirun -np 4 ./adaptive_search 11258999068426240 20000000

# Llave difícil (posición impredecible)
mpirun -np 8 ./adaptive_search 18014398509481984 50000000
```


Nota Importante:
DES usa 56 bits efectivos (los otros 8 son de paridad).
Por eso, aunque usamos un entero de 64 bits, solo 56 bits son realmente utilizados para el cifrado.
