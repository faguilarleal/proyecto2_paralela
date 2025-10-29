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

Compilar
```cmd
mpicc part2.c -o part2 -lcrypto
```

Crear o sobreescribir el archivo msg
```cmd
echo -n "Esta es una prueba de proyecto 2" > msg.txt
```

Cifrar con clave 42
```cmd
./part2 encrypt msg.txt msg.enc 42
```


Decifrar con clave
```cmd
./part2 decrypt msg.enc msg.dec.txt 42
```


Verificar el contenido
```cmd
cat msg.dec.txt
```



