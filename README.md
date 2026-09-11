# Vtx2

## Vtx2 nedir?
Vtx2, GUI yapısı olan bir işletim sistemidir ve UEFI kullanılması tavsiye edilir

## Bağımlılıklar nasıl indirilir
Bağımlılıkları yüklemek için, WSL veya Linux terminalinde komutları sırasıyla girin:
 ```bash
chmod +x ./get-deps.sh
sudo ./get-deps.sh
```
komutunu giriniz ve sudo şifrenizi girerek bağımlılıkları yükleyebilirsiniz

## Gereksinimler:
Depolama: en az 2 GB(tavsiye edilir)

RAM: en az 256 MB(tavsiye edilir)

TPM: yok

Disk: AHCI(Başka disk olmasın)

İşlemci: 64-bit

## Nasıl derlenir?
src/ klasörüne giderek şu komutları sırasıyla yapın:
```bash
chmod +x ./build.sh
./build.sh
```

NOT: Bu sistem GPL v3 ile lisanslanmıştır. Kodun patenti bendedir ve Tivolazition yasaktır!

UYARI: Litfen UEFI ile çalıştırın Legacy BIOS yapım aşamasındadır
