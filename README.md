```cmd
sudo apt update
sudo apt install -y libssl-dev openmpi-bin libopenmpi-dev

```

## Parte A

```cmd
mpicc br.c -lssl -lcrypto -o br
```

```cmd
mpirun -np 4 ./br
```


## Parte B

Descripcion del programa: 
implementa brute force sobre un texto cifrado con el algoritmo des en modo ECB (), utilizando MPI para distribuir el trabajo entre varios procesos. 
Y cada proceso prueba un rango distinto de las llaves hasta que llegue a encontrar aquella que logre descifrar correctamente el texto original. 

Compilar
```cmd
mpicc part2.c -o part2 -lcrypto

```

Crear o sobreescribir el archivo msg
```cmd
echo -n "Esta es una prueba de proyecto 2" > msg.txt
```


```cmd
mpirun -np 4 ./part2 123456L 42
```


correr todo de una vez
```cmd
mpicc part2.c -o part2 -lcrypto &&  mpirun -np 4 ./part2 123456L 42
```

