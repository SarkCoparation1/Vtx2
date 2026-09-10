#!/bin/bash
set -e

echo "=========================================="
echo " VTX2 Dependency Installer"
echo "=========================================="

if [ "$EUID" -ne 0 ]; then
    echo "[!] Root olarak çalıştırılmalı."
    echo "    sudo ./get-deps.sh"
    exit 1
fi

echo "[1/3] Paket listesi güncelleniyor..."
apt update

echo "[2/3] Bağımlılıklar kuruluyor..."
apt install -y \
    build-essential \
    gcc \
    binutils \
    libc6-dev \
    gnu-efi \
    nasm \
    dosfstools \
    mtools \
    xorriso \
    qemu-system-x86 \
    qemu-utils

echo "[3/3] Kontrol ediliyor..."

for cmd in gcc ld objcopy nasm mkfs.vfat mcopy xorriso qemu-system-x86_64 qemu-img; do
    if command -v "$cmd" >/dev/null 2>&1; then
        echo "[OK] $cmd"
    else
        echo "[HATA] $cmd bulunamadı!"
        exit 1
    fi
done

echo
echo "=========================================="
echo " Tüm bağımlılıklar hazır!"
echo "=========================================="