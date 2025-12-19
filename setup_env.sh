#!/bin/bash
set -e

echo "Установка зависимостей..."
apt update
apt install -y --no-install-recommends \
    build-essential \
    g++ \
    make \
    libfuse3-dev \
    pkg-config \
    fuse3 \
    sudo

echo "Настройка прав для FUSE..."
chmod 666 /dev/fuse || true
mkdir -p /opt/users
chmod 777 /opt/users

echo "Сборка и установка пакета..."
make clean
make deb
apt install -y --reinstall ./kubsh.deb

echo "Проверка установки..."
which kubsh
ls -la /usr/bin/kubsh
ldd /usr/bin/kubsh | grep fuse

echo "Проверка работоспособности..."
kubsh <<EOF
\e $PATH
\q
EOF

echo "Окружение настроено успешно!"

