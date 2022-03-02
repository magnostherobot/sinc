This project is built using LLVM 10.

The Makefile tries to call llvm-config, but this is ambiguous and may not work
in some situations (since there are more versions of LLVM than just v10). The
version 10-specific llvm-config binary is often called llvm-config-10; adjust
the Makefile if this is the case for your distribution.

# Building on Ubuntu 21.04

See https://apt.llvm.org/ for information. Briefly:

```bash
wget --no-check-certificate -O - https://apt.llvm.org/llvm-snapshot.gpg.key \
    | sudo apt-key add -
sudo add-apt-repository 'deb http://apt.llvm.org/bionic/ llvm-toolchain-bionic-10 main'
sudo apt update
audo apt install llvm-10-dev
```

The last command may fail when looking for libFFI 6, in which case you will have
to install this dependency manually from Ubuntu 19.04's pacakges (available
[here](http://mirrors.kernel.org/ubuntu/pool/main/libf/libffi/libffi6_3.2.1-8_amd64.deb)).
