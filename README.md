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

usados para las pruebas
```cmd
mpicc -o adaptive_search part2_adaptative.c -lssl -lcrypto

# facil
mpirun -np 4 ./part2_hybrid 36028797018900000 56 100000 60

#media 
 mpirun -np 4 ./part2_hybrid 45035996273650000 56 100000 60

# Llave difícil con timeout
mpirun -np 4 ./part2_hybrid 15837603060973172 10000000 100000 60
```



Compilar y correr part2_adaptative.c
```cmd
mpicc -o adaptive_search part2_adaptative.c -lssl -lcrypto

# Sin timeout (busca hasta encontrar o agotar rango)
mpirun -np 4 ./adaptive_search 123456 10000000

# Con timeout de 30 segundos
mpirun -np 4 ./adaptive_search 123456 10000000 30

# Llave difícil con timeout
mpirun -np 4 ./adaptive_search 18014398509481984 50000000 60
```

usados para las pruebas
```cmd
mpicc -o adaptive_search part2_adaptative.c -lssl -lcrypto

# facil
mpirun -np 4 ./adaptive_search 36028797018913969 1000000 60 36028797018963969

#media 
mpirun -np 4 ./adaptive_search 45035996273654960 1000000 60 45035996273704960

# Llave difícil con timeout
mpirun -np 4 ./adaptive_search 15837603060973173 10000000 60 1583760307097317
```


Nota Importante:
DES usa 56 bits efectivos (los otros 8 son de paridad).
Por eso, aunque usamos un entero de 64 bits, solo 56 bits son realmente utilizados para el cifrado.
