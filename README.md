# bcst
Minimal pub/sub implementation for POSIX

## Requirements
A POSIX environment

## Installation
```sh
make
sudo make install
```

## Usage
Create a socket and publish lines received from stdin
(one process per socket)
```sh
bcst pub channel.sock
```

Subscribe to a socket and output received lines to stdout
(multiple processes per socket)
```sh
bcst sub channel.sock
```
