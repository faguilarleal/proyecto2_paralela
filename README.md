```cmd
sudo apt update
sudo apt install -y libssl-dev openmpi-bin libopenmpi-dev

```

```cmd
mpicc br.c -lssl -lcrypto -o br
```

```cmd
mpirun -np 4 ./br
```
