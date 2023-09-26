- REQUIREMENTS
- BUILD
- EXAMPLE

## REQUIREMENTS

This project is build on `Ubuntu 22.04.2 LTS` with following dependencies:

- clang 14.0.0

- cmake 3.22.1

- boost c++ library 1.65

- OpenSSL *1.0*

  > Please note: Newer versions of OpenSSL may cause build failures

- gf-complete

- snappy

- [leveldb](https://github.com/google/leveldb)

- [sockpp](https://github.com/fpagliughi/sockpp)

- [bshoshany-thread-pool](https://github.com/bshoshany/thread-pool)

- [xdelta3](https://github.com/jmacd/xdelta)

`Leveldb`, `sockpp`, `bshoshany-thread-pool` and `xdelta3` are packed in source.

The remaining dependencies can be installed via `apt-get`:

```bash
$ apt-get install llvm cmake libboost-all-dev libssl1.0-dev libgf-complete-dev libsnappy-dev
```

## BUILD

Build this project with cmake

```bash
$ mkdir -p ./build && cd ./build
$ cmake .. -DCMAKE_BUILD_TYPE=RELEASE -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
$ make -j 6
$ cd ..
```

And the executables of `server` and `client` will be built to the directory `./bin`

## EXAMPLE

### server

It's required to run **four** server instances simultaneously **in different directories**.

The server will try to read the json configuration file when it starts, and the format of the configuration file is as follows:

```json
{
  "cluster": [
    {"ip": "127.0.0.1", "port": "6000"},
    {"ip": "127.0.0.1", "port": "6001"},
    {"ip": "127.0.0.1", "port": "6002"},
    {"ip": "127.0.0.1", "port": "6003"}
  ],
  "database dir": "./meta/DedupDB/",
  "container dir": "./meta/Container/",
  "clean": true
}

```

- `cluster` specifies the addresses of the four servers
- `database dir` specifies the path where the database files are stored
- `container dir` specifies the path where the share data container files are stored
- `clean` specifies whether the server will clear the files saved during previous runs

> Note that the `database dir` and ` container dir`  for the four server instances need to be different, so it is recommended to use relative paths

Usage of server:

```bash
$ server <port index> [config file]
```

- `<port>` specifies the addresses index of this server node in the config file
- `[config file]` is an optinal, from which the configuration file will be loaded, otherwise a default configuration will be loaded if not specified or fail to load the given one.

### client

The client will try to read a configuration file named `config` in the **current directory**, and the configuration file specifies the addresses of the four servers in turn:

```
0.0.0.0:6000
0.0.0.0:6001
0.0.0.0:6002
0.0.0.0:6003
```

Usage of client:

To upload a file:

```bash
$ client <target file> <user id> -u
```

- `<target file>` specifies the path to the file to upload
- `<user id>` specifies the user id

To download a file:

```bash
$ client <target file> <user id> -d
```

- `<target file>` specifies a previously uploaded file
- `<user id>` specifies the user id

If the download is successful, a file named `<target file>.decode` will appear in the same directory as the uploaded file

And we can use `md5sum`  to check whether the uploaded one and the downloaded one are identical:

```bash
$ md5sum <upload file> <download file>
```





