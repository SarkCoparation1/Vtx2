#ifndef VTX2FS_H
#define VTX2FS_H

/*
 * VTX2FS - VTX2 OS'in kendi dosya sistemi.
 *
 * Tasarim kararlari:
 *   - Tum boyut/ofset alanlari uint64_t: 4GB+ dosya ve buyuk diskler
 *     (NVMe hedefiyle) sinirlamasi olmasin diye.
 *   - Tahsis semasi: EXTENT tabanli (baslangic blok + blok sayisi).
 *     Buyuk dosyalar az sayida extent ile temsil edilir, ESFS'teki
 *     tek-satirlik tablo mantigina rahatca oturur.
 *   - Bos alan takibi: BITMAP tabanli (1 bit = 1 blok, 0=bos, 1=dolu).
 *   - Journaling: Once sadece METADATA (superblock, bitmap, dizin
 *     tablosu bloklari). Veri journaling'i ileride ust katman olarak
 *     eklenecek (VTX2_JOURNAL_FULL modu su an rezerve).
 *
 * Bu baslik, herhangi bir disk surucusune (AHCI/NVMe) dogrudan
 * bagli degildir; vtx2fs_blockdev_t araciligiyla soyutlanmistir.
 */

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* Temel sabitler                                                      */
/* ------------------------------------------------------------------ */

#define VTX2_SIGNATURE      "VTX2FS01"   /* 8 bayt, versiyon numarasi iceriyor */
#define VTX2_SIGNATURE_LEN  8

#define VTX2_DEFAULT_BLOCK_SIZE   4096u  /* bayt. Sektor boyutunun (512) kati olmali */
#define VTX2_MAX_BLOCK_SIZE       4096u  /* statik scratch tamponlarinin destekledigi ust sinir */

#define VTX2_MAX_NAME_LEN   112          /* dosya/dizin adi, NUL dahil */
#define VTX2_DIRECT_EXTENTS 6             /* girdi basina dogrudan extent sayisi */

/* Girdi (entry) bayraklari */
#define VTX2_ENTRY_FREE        0x00      /* kullanilmiyor / silinmis */
#define VTX2_ENTRY_FILE        0x01
#define VTX2_ENTRY_DIRECTORY   0x02

/* vtx2fs_result_t donus kodlari */
typedef enum {
    VTX2_OK = 0,
    VTX2_ERR_IO,               /* blok aygitindan okuma/yazma hatasi */
    VTX2_ERR_NOT_FORMATTED,    /* imza uyusmuyor */
    VTX2_ERR_NO_SPACE,         /* bos blok/girdi kalmadi */
    VTX2_ERR_NOT_FOUND,
    VTX2_ERR_EXISTS,
    VTX2_ERR_NOT_DIR,
    VTX2_ERR_IS_DIR,
    VTX2_ERR_NAME_TOO_LONG,
    VTX2_ERR_INVALID,
    VTX2_ERR_JOURNAL_DIRTY,    /* mount aninda tamamlanmamis islem bulundu */
    VTX2_ERR_NOT_EMPTY,        /* dizin bos degil, silinemez */
} vtx2fs_result_t;

/* ------------------------------------------------------------------ */
/* Soyut blok aygiti arabirimi                                         */
/* ------------------------------------------------------------------ */

/*
 * VTX2FS bu iki fonksiyon isaretcisi disinda hicbir surucuye
 * bagli degildir. AHCI/NVMe surucusu hazir oldugunda, sadece bu
 * iki callback'i o surucunun sektor okuma/yazma fonksiyonlarina
 * baglamak yeterli olacak.
 *
 * block_index: vtx2fs blok numarasi (superblock->block_size birimiyle),
 *              sektor numarasi DEGIL. Cagiran taraf gerekli sektor
 *              donusumunu (block_size / sector_size) kendi yapar.
 */
typedef struct {
    void *device_context;   /* surucuye ozel veri (ör. AHCI port numarasi) */

    vtx2fs_result_t (*read_block)(void *device_context, uint64_t block_index,
                                   uint32_t block_size, void *out_buffer);
    vtx2fs_result_t (*write_block)(void *device_context, uint64_t block_index,
                                    uint32_t block_size, const void *in_buffer);
} vtx2fs_blockdev_t;

/* ------------------------------------------------------------------ */
/* Disk-uzeri yapilar (on-disk layout)                                 */
/* ------------------------------------------------------------------ */

/*
 * Superblock - diskin 0. blogunda tutulur.
 *
 * Genel disk duzeni:
 *   [ Superblock (1 blok) ]
 *   [ Blok Bitmap (bitmap_block_count blok) ]
 *   [ Journal Alani (journal_block_count blok, dairesel log) ]
 *   [ Dizin/Dosya Girdi Tablosu (entry_table_block_count blok) ]
 *   [ Veri Bloklari (kalan tum alan) ]
 */
typedef struct __attribute__((packed)) {
    char     signature[VTX2_SIGNATURE_LEN]; /* "VTX2FS01" */
    uint32_t block_size;                    /* bayt cinsinden (varsayilan 4096) */
    uint64_t total_blocks;                  /* diskteki toplam blok sayisi */

    uint64_t bitmap_start_block;
    uint64_t bitmap_block_count;

    uint64_t journal_start_block;
    uint64_t journal_block_count;

    uint64_t entry_table_start_block;
    uint64_t entry_table_block_count;
    uint64_t entry_table_capacity;          /* toplam girdi (dosya+dizin) kapasitesi */

    uint64_t data_start_block;              /* veri alaninin basladigi blok */

    uint64_t root_entry_index;              /* kok dizinin entry_table'daki indeksi */

    uint64_t free_blocks_hint;               /* performans icin onbelleklenmis bos blok sayisi
                                               * (kesin kaynak her zaman bitmap'tir) */

    uint32_t checksum;                       /* superblock'un geri kalaninin basit checksum'i */
    uint8_t  reserved[408];                  /* blogu tam 512 bayta tamamlar, ileride kullanim icin */
} vtx2_superblock_t;

/* Bir dosyanin diskteki tek bir ardisik blok araligi */
typedef struct __attribute__((packed)) {
    uint64_t start_block;  /* 0 = kullanilmiyor (bos extent slotu) */
    uint64_t block_count;
} vtx2_extent_t;

/*
 * Dizin/dosya girdisi - entry_table icindeki tek bir satir.
 *
 * TASARIM: Tum dosya/dizinler AYNI duz global tabloda (entry_table) yasar -
 * dizinlerin AYRI bir "icerik blogu" YOKTUR. Bir dizinin "cocuklari",
 * entry_table'daki parent_entry_index alani KENDI indeksine esit olan
 * tum girdilerdir (bkz. vtx2_list_dir). Bu, ESFS'teki "duz tablo"
 * yaklasiminin doganal genisletilmis hali: basit, sabit-kapasiteli,
 * indeksleme kolay. Dizin girdilerinde direct_extents/indirect_extent_block
 * hep bos (0) kalir - sadece DOSYA girdileri veri bloklarina isaret eder.
 */
typedef struct __attribute__((packed)) {
    char     name[VTX2_MAX_NAME_LEN];
    uint8_t  type;                 /* VTX2_ENTRY_FREE / _FILE / _DIRECTORY */
    uint8_t  _pad[7];

    uint64_t size_bytes;           /* dosya: gercek boyut. dizin: kullanilan bayt sayisi */

    uint64_t created_time;         /* epoch benzeri, PIT/RTC kaynakli */
    uint64_t modified_time;

    uint32_t extent_count;         /* direct_extents icinde kac tanesi dolu */
    uint32_t _pad2;

    vtx2_extent_t direct_extents[VTX2_DIRECT_EXTENTS];

    /*
     * Bir dosya VTX2_DIRECT_EXTENTS'ten fazla parcaya bolunmusse
     * (agir fragmantasyon), bu alan ek extentlerin tutuldugu bir
     * "indirect" bloguna isaret eder. 0 = yok.
     * O blok, art arda vtx2_extent_t kayitlarindan olusan duz bir
     * dizidir (block_size / sizeof(vtx2_extent_t) adet extent alir).
     */
    uint64_t indirect_extent_block;

    uint64_t parent_entry_index;   /* ust dizinin entry_table indeksi (kok icin kendisi) */

    /*
     * --- Sifreleme (v1: XOR obfuscation, ileride AES) ---
     * UYARI: encryption_scheme=1 (XOR) GERCEK KRIPTOGRAFIK GUVENLIK
     * SAGLAMAZ. Sadece parolasiz rastgele goz atmayi engeller; bilinen-
     * duz-metin veya frekans analiziyle kolayca kirilabilir. Hassas veri
     * icin encryption_scheme=2 (AES, henuz uygulanmadi) beklenmelidir.
     */
    uint8_t  encryption_scheme;    /* 0=sifresiz, 1=XOR (zayif), 2=AES (henuz yok) */
    uint8_t  salt[16];             /* parola turetme tuzu (rastgele) */
    uint8_t  verifier[4];          /* parola dogrulama izi - FNV1a(parola||tuz||"VTX2CHK")'in ilk 4 baytı */
    uint8_t  name_encrypted;       /* 1 ise `name` alani sifreli (cozulmeden okunamaz) */
    uint8_t  _crypto_pad[6];       /* hizalama */
} vtx2_entry_t;

/* ------------------------------------------------------------------ */
/* Journal (sadece metadata) yapilari                                  */
/* ------------------------------------------------------------------ */

#define VTX2_JOURNAL_MAGIC_BEGIN  0x54584A42u /* "TXJB" - islem basladi */
#define VTX2_JOURNAL_MAGIC_COMMIT 0x54584A43u /* "TXJC" - islem tamamlandi */

/*
 * Journal, dairesel bir log alanidir. Her "islem" (transaction),
 * degisecek metadata bloklarinin ESKI HALINI DEGIL, YENI HALINI
 * (redo log) sirayla yazar, en sonunda bir commit kaydi eklenir.
 *
 * Akis:
 *   1. Degisecek her metadata blogu icin bir vtx2_journal_record_t
 *      + o blogun tam icerigi journale yazilir (begin_magic ile).
 *   2. Tum kayitlar yazildiktan sonra bir commit kaydi (yalnizca
 *      header, veri yok) yazilir.
 *   3. Ancak commit kaydi diskte goruldukten SONRA asil bloklar
 *      (bitmap, entry table, superblock) gercek konumlarina yazilir.
 *   4. Mount sirasinda: journal taranir. Commit edilmis ama asil
 *      konumuna henuz yazilmadigi anlasilan (checksum/versiyon
 *      karsilastirmasiyla) islemler yeniden uygulanir (replay).
 *      Commit edilmemis yari-yazilmis islemler yok sayilir (discard).
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;             /* VTX2_JOURNAL_MAGIC_BEGIN / _COMMIT */
    uint32_t transaction_id;    /* monoton artan islem numarasi */
    uint64_t target_block;      /* bu kayit BEGIN ise: hedef metadata blogu.
                                  * COMMIT ise: bu islemdeki toplam kayit sayisi */
    uint32_t data_length;       /* BEGIN icin: sonra gelen veri uzunlugu (block_size).
                                  * COMMIT icin: kullanilmiyor (0) */
    uint32_t checksum;
} vtx2_journal_record_header_t;

/* ------------------------------------------------------------------ */
/* Ust seviye API (henuz uygulanmadi - bir sonraki adim vtx2fs.c)      */
/* ------------------------------------------------------------------ */

vtx2fs_result_t vtx2_format(vtx2fs_blockdev_t *dev, uint64_t total_blocks, uint32_t block_size);
vtx2fs_result_t vtx2_mount(vtx2fs_blockdev_t *dev, vtx2_superblock_t *out_sb);

vtx2fs_result_t vtx2_create_file(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                                  uint64_t parent_dir_index, const char *name,
                                  uint64_t *out_entry_index);

vtx2fs_result_t vtx2_mkdir(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                            uint64_t parent_dir_index, const char *name,
                            uint64_t *out_entry_index);

vtx2fs_result_t vtx2_read(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                           uint64_t entry_index, uint64_t offset,
                           void *out_buffer, uint64_t length, uint64_t *out_read);

vtx2fs_result_t vtx2_write(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                            uint64_t entry_index, uint64_t offset,
                            const void *in_buffer, uint64_t length, uint64_t *out_written);

/*
 * Bir dizinin dogrudan cocuklarinin entry_table indekslerini out_indices'e
 * yazar (en fazla max_out adet). out_count, BULUNAN TOPLAM cocuk sayisini
 * (max_out'u asabilir) bildirir - caller out_count > max_out gorurse daha
 * buyuk bir arabellekle tekrar cagirabilir.
 */
vtx2fs_result_t vtx2_list_dir(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                               uint64_t dir_entry_index,
                               uint64_t *out_indices, uint64_t max_out, uint64_t *out_count);

/* Bir girdinin tam bilgisini (isim/tip/boyut/zaman/ebeveyn) okur.
 * Dosya yoneticisi gibi ust katmanlarin listeleme sonrasi detay
 * gormesi icin kullanilir. */
vtx2fs_result_t vtx2_stat(vtx2fs_blockdev_t *dev, const vtx2_superblock_t *sb,
                           uint64_t entry_index, vtx2_entry_t *out_entry);

/*
 * Bir dosyanin MANTIKSAL boyutunu (size_bytes) kucultur. new_size, mevcut
 * size_bytes'tan BUYUK olamaz (buyutmek icin vtx2_write kullanilmali).
 * NOT: Bu fonksiyon artik kullanilmayan bloklari SERBEST BIRAKMAZ (basitlik
 * icin) - sadece mantiksal boyutu kisaltir. Yer israfi (fragmentation)
 * pahasina basit tutuldu; gercek blok geri kazanimi ileride eklenebilir.
 */
vtx2fs_result_t vtx2_truncate(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                               uint64_t entry_index, uint64_t new_size);

/*
 * Bir dosyayi ya da BOS bir dizini siler (girdiyi FREE yapar, dosyanin
 * bloklarini bitmap'e geri verir). Kok dizin silinemez. Bos olmayan bir
 * dizin silinmeye calisilirsa VTX2_ERR_NOT_EMPTY doner.
 */
vtx2fs_result_t vtx2_delete(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb, uint64_t entry_index);

/* Bir mount sirasinda tamamlanmamis islemleri diske uygulayip journali temizler. */
vtx2fs_result_t vtx2_journal_replay(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb);

/* ------------------------------------------------------------------ */
/* Sifreli dosya/dizin API'si (v1: XOR obfuscation)                    */
/*                                                                       */
/* Bu fonksiyonlar YUKARIDAKI duz (plain) API'nin YANINDA, onu          */
/* BOZMADAN eklenmis paralel bir yoldur. Mevcut vtx2_create_file/mkdir/  */
/* read/write/list_dir HICBIR SEKILDE degismedi - sifresiz calismaya     */
/* devam ederler. Sifreleme SADECE bu _encrypted fonksiyonlariyla        */
/* olusturulan/erisilen girdiler icin gecerlidir.                       */
/*                                                                       */
/* GUVENLIK NOTU: v1 (XOR) gercek kriptografik guvenlik SAGLAMAZ, sadece */
/* parolasiz rastgele goz atmayi engeller. AES (v2) henuz uygulanmadi.   */
/* ------------------------------------------------------------------ */

/* Verilen parola+tuz'den turetilen anahtar materyalini (32 bayt) uretir. */
void vtx2_derive_key(const char *password, const uint8_t salt[16], uint8_t out_key[32]);

/* out_key/verifier zaten turetilmis anahtar materyaliyle, parolanin
 * dogru olup olmadigini (verifier'a gore) kontrol eder. */
bool vtx2_check_password(const uint8_t key[32], const uint8_t salt[16], const uint8_t verifier[4]);

/* Sifreli bir dosya olusturur (isim de sifrelenir). parent_dir_index
 * sifresiz VEYA sifreli bir dizin olabilir - sadece bu YENI girdi
 * sifrelenir. */
vtx2fs_result_t vtx2_create_file_encrypted(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                                            uint64_t parent_dir_index, const char *name,
                                            const char *password, uint64_t *out_entry_index);

/* Sifreli bir dizin olusturur (isim de sifrelenir). */
vtx2fs_result_t vtx2_mkdir_encrypted(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                                      uint64_t parent_dir_index, const char *name,
                                      const char *password, uint64_t *out_entry_index);

/* Sifreli bir dosyayi okur - once parola dogrulanir (yanlissa
 * VTX2_ERR_INVALID doner), sonra icerik cozulerek out_buffer'a yazilir. */
vtx2fs_result_t vtx2_read_encrypted(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                                     uint64_t entry_index, const char *password,
                                     uint64_t offset, void *out_buffer, uint64_t length,
                                     uint64_t *out_read);

vtx2fs_result_t vtx2_write_encrypted(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb, uint64_t entry_index, const char *password, uint64_t offset, const void *in_buffer, uint64_t length, uint64_t *out_written);

vtx2fs_result_t vtx2_decrypt_name(const vtx2_entry_t *entry, const char *password, char *out_name);

#endif