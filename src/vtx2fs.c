#include "vtx2fs.h"
#include <stddef.h>

static uint8_t g_scratch_block[VTX2_MAX_BLOCK_SIZE];
static uint8_t g_scratch_block2[VTX2_MAX_BLOCK_SIZE];
static uint64_t g_journal_write_offset = 0;

static void vtx2_memset(void *dst, uint8_t val, uint64_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint64_t i = 0; i < n; i++) d[i] = val;
}

static void vtx2_memcpy(void *dst, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint64_t i = 0; i < n; i++) d[i] = s[i];
}

static int vtx2_memcmp(const void *a, const void *b, uint64_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (uint64_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

static uint64_t vtx2_strlen_bounded(const char *s, uint64_t max) {
    uint64_t len = 0;
    while (len < max && s[len] != '\0') len++;
    return len;
}

static void vtx2_name_copy(char *dst, const char *src) {
    uint64_t len = vtx2_strlen_bounded(src, VTX2_MAX_NAME_LEN - 1);
    vtx2_memcpy(dst, src, len);
    dst[len] = '\0';
    if (len + 1 < VTX2_MAX_NAME_LEN) {
        vtx2_memset(dst + len + 1, 0, VTX2_MAX_NAME_LEN - len - 1);
    }
}

static uint32_t vtx2_fnv1a(const void *data, uint64_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t hash = 0x811C9DC5u;
    for (uint64_t i = 0; i < len; i++) {
        hash ^= p[i];
        hash *= 0x01000193u;
    }
    return hash;
}

static uint64_t vtx2_min_u64(uint64_t a, uint64_t b) { return (a < b) ? a : b; }
static uint64_t vtx2_max_u64(uint64_t a, uint64_t b) { return (a > b) ? a : b; }

static uint64_t vtx2_ceil_div_u64(uint64_t a, uint64_t b) {
    return (a + b - 1) / b;
}

static uint32_t vtx2_superblock_checksum(const vtx2_superblock_t *sb) {
    /* checksum alanindan ONCEKI tum alanlari kapsar (checksum ve reserved haric) */
    return vtx2_fnv1a(sb, offsetof(vtx2_superblock_t, checksum));
}

static vtx2fs_result_t vtx2_zero_blocks(vtx2fs_blockdev_t *dev, uint64_t start_block, uint64_t block_count, uint32_t block_size) {
    vtx2_memset(g_scratch_block, 0, block_size);
    for (uint64_t i = 0; i < block_count; i++) {
        vtx2fs_result_t res = dev->write_block(dev->device_context, start_block + i, block_size, g_scratch_block);
        if (res != VTX2_OK) return res;
    }
    return VTX2_OK;
}

#define VTX2_MIN_TOTAL_BLOCKS 64ull

#define VTX2_MIN_JOURNAL_BLOCKS 4ull
#define VTX2_MAX_JOURNAL_BLOCKS 256ull

#define VTX2_MIN_ENTRY_TABLE_BLOCKS 4ull
#define VTX2_MAX_ENTRY_TABLE_BLOCKS 4096ull

vtx2fs_result_t vtx2_format(vtx2fs_blockdev_t *dev, uint64_t total_blocks, uint32_t block_size) {
    if (dev == NULL || dev->read_block == NULL || dev->write_block == NULL) {
        return VTX2_ERR_INVALID;
    }
    if (block_size == 0 || (block_size % 512) != 0 || block_size > VTX2_MAX_BLOCK_SIZE) {
        return VTX2_ERR_INVALID;
    }
    if (total_blocks < VTX2_MIN_TOTAL_BLOCKS) {
        return VTX2_ERR_NO_SPACE;
    }

    uint64_t bitmap_start_block = 1;
    uint64_t bitmap_bits_needed = total_blocks;
    uint64_t bitmap_bytes_needed = vtx2_ceil_div_u64(bitmap_bits_needed, 8);
    uint64_t bitmap_block_count = vtx2_ceil_div_u64(bitmap_bytes_needed, block_size);
    if (bitmap_block_count < 1) bitmap_block_count = 1;

    uint64_t journal_start_block = bitmap_start_block + bitmap_block_count;
    uint64_t journal_block_count = vtx2_max_u64(VTX2_MIN_JOURNAL_BLOCKS, vtx2_min_u64(VTX2_MAX_JOURNAL_BLOCKS, total_blocks / 256));

    uint64_t entry_table_start_block = journal_start_block + journal_block_count;
    uint64_t entry_table_block_count = vtx2_max_u64(VTX2_MIN_ENTRY_TABLE_BLOCKS, vtx2_min_u64(VTX2_MAX_ENTRY_TABLE_BLOCKS, total_blocks / 128));

    uint64_t data_start_block = entry_table_start_block + entry_table_block_count;

    if (data_start_block >= total_blocks) {
        return VTX2_ERR_NO_SPACE;
    }

    uint64_t entry_table_capacity = (entry_table_block_count * (uint64_t)block_size) / sizeof(vtx2_entry_t);
    if (entry_table_capacity < 1) return VTX2_ERR_NO_SPACE;

    /* --- 1) Journal alanini sifirla (temiz/bos journal = mount'ta replay yok) --- */
    vtx2fs_result_t res = vtx2_zero_blocks(dev, journal_start_block, journal_block_count, block_size);
    if (res != VTX2_OK) return res;

    /* --- 2) Entry tablosunu sifirla (tum girdiler VTX2_ENTRY_FREE = 0x00 olur) --- */
    res = vtx2_zero_blocks(dev, entry_table_start_block, entry_table_block_count, block_size);
    if (res != VTX2_OK) return res;

    /* --- 3) Kok dizin girdisini olustur (entry_table'daki 0. girdi) --- */
    {
        vtx2_entry_t root;
        vtx2_memset(&root, 0, sizeof(root));
        vtx2_name_copy(root.name, "/");
        root.type = VTX2_ENTRY_DIRECTORY;
        root.size_bytes = 0;
        root.created_time = 0;
        root.modified_time = 0;
        root.extent_count = 0;
        root.indirect_extent_block = 0;
        root.parent_entry_index = 0; /* kok kendi ebeveynidir */

        vtx2_memset(g_scratch_block, 0, block_size);
        vtx2_memcpy(g_scratch_block, &root, sizeof(root));
        res = dev->write_block(dev->device_context, entry_table_start_block, block_size, g_scratch_block);
        if (res != VTX2_OK) return res;
    }

    /* --- 4) Blok bitmap'ini olustur ---
     * [0, data_start_block)      -> dolu (metadata icin ayrilmis)
     * [data_start_block, total)  -> bos (veri alani)
     * [total_blocks, ...)        -> dolu (bitmap padding'i, gercek karsiligi yok) */
    {
        uint64_t total_bytes = bitmap_block_count * (uint64_t)block_size;
        for (uint64_t bb = 0; bb < bitmap_block_count; bb++) {
            uint64_t block_byte_base = bb * (uint64_t)block_size;
            for (uint32_t off = 0; off < block_size; off++) {
                uint64_t j = block_byte_base + off;
                if (j >= total_bytes) { g_scratch_block[off] = 0xFF; continue; }
                uint64_t bit_start = j * 8;
                uint64_t bit_end = bit_start + 8;
                if (bit_end <= data_start_block) {
                    g_scratch_block[off] = 0xFF; /* tamamen ayrilmis */
                } else if (bit_start >= total_blocks) {
                    g_scratch_block[off] = 0xFF; /* tamamen padding, kullanim disi */
                } else if (bit_start >= data_start_block && bit_end <= total_blocks) {
                    g_scratch_block[off] = 0x00; /* tamamen bos veri alani */
                } else {
                    /* sinir bayti - bit bit olustur */
                    uint8_t byte = 0;
                    for (int k = 0; k < 8; k++) {
                        uint64_t bit_index = bit_start + (uint64_t)k;
                        bool used = (bit_index < data_start_block) || (bit_index >= total_blocks);
                        if (used) byte |= (uint8_t)(1u << k);
                    }
                    g_scratch_block[off] = byte;
                }
            }
            res = dev->write_block(dev->device_context, bitmap_start_block + bb, block_size, g_scratch_block);
            if (res != VTX2_OK) return res;
        }
    }

    /* --- 5) Superblock'u olustur ve yaz --- */
    {
        vtx2_superblock_t sb;
        vtx2_memset(&sb, 0, sizeof(sb));
        vtx2_memcpy(sb.signature, VTX2_SIGNATURE, VTX2_SIGNATURE_LEN);
        sb.block_size = block_size;
        sb.total_blocks = total_blocks;

        sb.bitmap_start_block = bitmap_start_block;
        sb.bitmap_block_count = bitmap_block_count;

        sb.journal_start_block = journal_start_block;
        sb.journal_block_count = journal_block_count;

        sb.entry_table_start_block = entry_table_start_block;
        sb.entry_table_block_count = entry_table_block_count;
        sb.entry_table_capacity = entry_table_capacity;

        sb.data_start_block = data_start_block;
        sb.root_entry_index = 0;
        sb.free_blocks_hint = total_blocks - data_start_block;

        sb.checksum = vtx2_superblock_checksum(&sb);

        vtx2_memset(g_scratch_block, 0, block_size);
        vtx2_memcpy(g_scratch_block, &sb, sizeof(sb));
        res = dev->write_block(dev->device_context, 0, block_size, g_scratch_block);
        if (res != VTX2_OK) return res;
    }

    return VTX2_OK;
}

/* ------------------------------------------------------------------ */
/* vtx2_mount                                                           */
/* ------------------------------------------------------------------ */

vtx2fs_result_t vtx2_mount(vtx2fs_blockdev_t *dev, vtx2_superblock_t *out_sb) {
    if (dev == NULL || dev->read_block == NULL || dev->write_block == NULL || out_sb == NULL) {
        return VTX2_ERR_INVALID;
    }

    /*
     * "Tavsan-yumurta" cozumu: block_size degeri superblock'un ICINDE,
     * ama superblock'u okumak icin bir block_size lazim. Bunun icin ilk
     * okuma HER ZAMAN sabit 512 bayt (fiziksel sektor boyutu) ile yapilir;
     * blok 0, hangi block_size secilirse secilsin diskin 0. bayt ofsetinde
     * baslar, dolayisiyla bu ilk okuma her zaman guvenlidir.
     */
    vtx2fs_result_t res = dev->read_block(dev->device_context, 0, 512, g_scratch_block);
    if (res != VTX2_OK) return res;

    if (vtx2_memcmp(g_scratch_block, VTX2_SIGNATURE, VTX2_SIGNATURE_LEN) != 0) {
        return VTX2_ERR_NOT_FORMATTED;
    }

    vtx2_memcpy(out_sb, g_scratch_block, sizeof(vtx2_superblock_t));

    if (out_sb->block_size == 0 || (out_sb->block_size % 512) != 0 ||
        out_sb->block_size > VTX2_MAX_BLOCK_SIZE) {
        return VTX2_ERR_INVALID;
    }

    uint32_t expected_checksum = vtx2_superblock_checksum(out_sb);
    if (expected_checksum != out_sb->checksum) {
        return VTX2_ERR_INVALID; /* superblock bozulmus */
    }

    /* Temel tutarlilik kontrolleri */
    if (out_sb->data_start_block > out_sb->total_blocks) return VTX2_ERR_INVALID;
    if (out_sb->entry_table_start_block + out_sb->entry_table_block_count > out_sb->total_blocks) {
        return VTX2_ERR_INVALID;
    }
    if (out_sb->root_entry_index >= out_sb->entry_table_capacity) return VTX2_ERR_INVALID;

    /* Tamamlanmamis islemler varsa uygula (crash recovery). */
    res = vtx2_journal_replay(dev, out_sb);
    if (res != VTX2_OK) return res;

    /* Basarili replay sonrasi journal alani bos/sifirlanmistir.
     * Bu mount oturumunun yazma imleci RAM'de 0'dan baslar. */
    g_journal_write_offset = 0;

    return VTX2_OK;
}

/* ------------------------------------------------------------------ */
/* Journal (metadata) tarama + yeniden uygulama (replay)                */
/* ------------------------------------------------------------------ */

/* Journal alanindan, blok sinirlarini gozeterek rastgele bayt araligi okur. */
static vtx2fs_result_t vtx2_journal_read_bytes(vtx2fs_blockdev_t *dev, const vtx2_superblock_t *sb,
                                                uint64_t byte_offset, void *out, uint64_t length) {
    uint8_t *dst = (uint8_t *)out;
    uint64_t remaining = length;
    uint64_t cur = byte_offset;
    while (remaining > 0) {
        uint64_t block_local = cur / sb->block_size;
        uint64_t in_block_off = cur % sb->block_size;
        uint64_t chunk = sb->block_size - in_block_off;
        if (chunk > remaining) chunk = remaining;

        vtx2fs_result_t res = dev->read_block(dev->device_context,
                                    sb->journal_start_block + block_local,
                                    sb->block_size, g_scratch_block2);
        if (res != VTX2_OK) return res;

        vtx2_memcpy(dst, g_scratch_block2 + in_block_off, chunk);
        dst += chunk;
        cur += chunk;
        remaining -= chunk;
    }
    return VTX2_OK;
}

/* journal_read_bytes'in yazma karsiligi - blok sinirlarini gozeterek
 * read-modify-write yapar. */
static vtx2fs_result_t vtx2_journal_write_bytes(vtx2fs_blockdev_t *dev, const vtx2_superblock_t *sb,
                                                 uint64_t byte_offset, const void *in, uint64_t length) {
    const uint8_t *src = (const uint8_t *)in;
    uint64_t remaining = length;
    uint64_t cur = byte_offset;
    while (remaining > 0) {
        uint64_t block_local = cur / sb->block_size;
        uint64_t in_block_off = cur % sb->block_size;
        uint64_t chunk = sb->block_size - in_block_off;
        if (chunk > remaining) chunk = remaining;

        vtx2fs_result_t res = dev->read_block(dev->device_context,
                                    sb->journal_start_block + block_local,
                                    sb->block_size, g_scratch_block2);
        if (res != VTX2_OK) return res;

        vtx2_memcpy(g_scratch_block2 + in_block_off, src, chunk);

        res = dev->write_block(dev->device_context, sb->journal_start_block + block_local,
                                sb->block_size, g_scratch_block2);
        if (res != VTX2_OK) return res;

        src += chunk;
        cur += chunk;
        remaining -= chunk;
    }
    return VTX2_OK;
}

/*
 * Bir metadata blogunu (entry_table veya bitmap blogu) DOGRUDAN yazmak
 * yerine once journal'a [BEGIN][blok_icerigi][COMMIT] olarak yazar, ancak
 * o transaction diske TAM ve DOGRU sekilde yazildiktan SONRA gercek
 * konumuna uygular. Ani guc kesintisi journal yazimi sirasinda olursa,
 * bir sonraki mount() bu yarim transaction'i (commit kaydi eksik oldugu
 * icin) guvenle atlar - metadata bozulmaz. Kesinti gercek-konum yazimi
 * sirasinda olursa, replay commit edilmis transaction'i tekrar uygular.
 */
static vtx2fs_result_t vtx2_journal_write_transaction(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                                                       uint64_t target_block, const void *block_data) {
    uint64_t journal_total_bytes = sb->journal_block_count * (uint64_t)sb->block_size;
    uint64_t needed = sizeof(vtx2_journal_record_header_t) + sb->block_size
                     + sizeof(vtx2_journal_record_header_t);

    if (needed > journal_total_bytes) {
        /* Journal alani tek bir transaction'a bile sigmiyor - dogrudan yaz. */
        return dev->write_block(dev->device_context, target_block, sb->block_size, block_data);
    }

    if (g_journal_write_offset + needed > journal_total_bytes) {
        g_journal_write_offset = 0;
    }

    uint64_t off = g_journal_write_offset;

    vtx2_journal_record_header_t begin_hdr;
    vtx2_memset(&begin_hdr, 0, sizeof(begin_hdr));
    begin_hdr.magic = VTX2_JOURNAL_MAGIC_BEGIN;
    begin_hdr.transaction_id = (uint32_t)(off / sb->block_size) + 1u; /* benzersizligi sart degil, 0 olmasin yeter */
    begin_hdr.target_block = target_block;
    begin_hdr.data_length = sb->block_size;
    begin_hdr.checksum = vtx2_fnv1a(block_data, sb->block_size);

    vtx2fs_result_t res = vtx2_journal_write_bytes(dev, sb, off, &begin_hdr, sizeof(begin_hdr));
    if (res != VTX2_OK) return res;
    off += sizeof(begin_hdr);

    res = vtx2_journal_write_bytes(dev, sb, off, block_data, sb->block_size);
    if (res != VTX2_OK) return res;
    off += sb->block_size;

    vtx2_journal_record_header_t commit_hdr;
    vtx2_memset(&commit_hdr, 0, sizeof(commit_hdr));
    commit_hdr.magic = VTX2_JOURNAL_MAGIC_COMMIT;
    res = vtx2_journal_write_bytes(dev, sb, off, &commit_hdr, sizeof(commit_hdr));
    if (res != VTX2_OK) return res;
    off += sizeof(commit_hdr);

    g_journal_write_offset = off;

    /* Transaction diskte guvenle duruyor - simdi asil blogu gercek
     * konumuna uygula. */
    return dev->write_block(dev->device_context, target_block, sb->block_size, block_data);
}

vtx2fs_result_t vtx2_journal_replay(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb) {
    uint64_t journal_total_bytes = sb->journal_block_count * (uint64_t)sb->block_size;
    uint64_t offset = 0;
    bool applied_any = false;

    while (offset + sizeof(vtx2_journal_record_header_t) <= journal_total_bytes) {
        vtx2_journal_record_header_t hdr;
        vtx2fs_result_t res = vtx2_journal_read_bytes(dev, sb, offset, &hdr, sizeof(hdr));
        if (res != VTX2_OK) return res;

        if (hdr.magic == 0) {
            /* journal'in kullanilan kismi bitti */
            break;
        }

        if (hdr.magic != VTX2_JOURNAL_MAGIC_BEGIN) {
            /* taninmayan/bozuk kayit - burada dur, geri kalanini uygulama */
            break;
        }

        uint64_t payload_offset = offset + sizeof(hdr);
        if (hdr.data_length == 0 || hdr.data_length > sb->block_size ||
            payload_offset + hdr.data_length + sizeof(vtx2_journal_record_header_t) > journal_total_bytes) {
            /* tutarsiz uzunluk - guvenli tarafta kal, uygulama */
            break;
        }

        res = vtx2_journal_read_bytes(dev, sb, payload_offset, g_scratch_block, hdr.data_length);
        if (res != VTX2_OK) return res;

        uint32_t payload_checksum = vtx2_fnv1a(g_scratch_block, hdr.data_length);
        if (payload_checksum != hdr.checksum) {
            /* islem yarim yazilmis (crash), atla - uygulama */
            break;
        }

        uint64_t commit_offset = payload_offset + hdr.data_length;
        vtx2_journal_record_header_t commit_hdr;
        res = vtx2_journal_read_bytes(dev, sb, commit_offset, &commit_hdr, sizeof(commit_hdr));
        if (res != VTX2_OK) return res;

        if (commit_hdr.magic != VTX2_JOURNAL_MAGIC_COMMIT) {
            /* commit yok = islem tamamlanmamis (crash), uygulama, dur */
            break;
        }

        /* Islem tam ve tutarli: kaydedilen yeni blok icerigini gercek konumuna yaz. */
        res = dev->write_block(dev->device_context, hdr.target_block, sb->block_size, g_scratch_block);
        if (res != VTX2_OK) return res;

        applied_any = true;
        offset = commit_offset + sizeof(commit_hdr);
    }

    if (applied_any) {
        /* Journal'i temizle ki bir sonraki mount'ta tekrar uygulanmasin. */
        return vtx2_zero_blocks(dev, sb->journal_start_block, sb->journal_block_count, sb->block_size);
    }

    return VTX2_OK;
}

/* ------------------------------------------------------------------ */
/* Girdi (entry) tablosu yardimcilari                                   */
/*                                                                       */
/* Entry-table ve bitmap YAZMA islemleri artik journal uzerinden        */
/* gecer (bkz. vtx2_journal_write_transaction) - create/mkdir/write/    */
/* delete sirasindaki ani guc kesintileri artik metadata'yi bozmaz.     */
/* Sadece OKUMA fonksiyonlari (vtx2_read_entry) dogrudan diskten okur.  */
/* ------------------------------------------------------------------ */

static uint64_t vtx2_entries_per_block(const vtx2_superblock_t *sb) {
    return sb->block_size / sizeof(vtx2_entry_t);
}

static vtx2fs_result_t vtx2_read_entry(vtx2fs_blockdev_t *dev, const vtx2_superblock_t *sb,
                                        uint64_t index, vtx2_entry_t *out) {
    if (index >= sb->entry_table_capacity) return VTX2_ERR_INVALID;

    uint64_t per_block = vtx2_entries_per_block(sb);
    uint64_t block_num = index / per_block;
    uint64_t in_block_index = index % per_block;

    vtx2fs_result_t res = dev->read_block(dev->device_context,
                                sb->entry_table_start_block + block_num,
                                sb->block_size, g_scratch_block);
    if (res != VTX2_OK) return res;

    vtx2_memcpy(out, g_scratch_block + in_block_index * sizeof(vtx2_entry_t), sizeof(vtx2_entry_t));
    return VTX2_OK;
}

static vtx2fs_result_t vtx2_write_entry(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                                         uint64_t index, const vtx2_entry_t *entry) {
    if (index >= sb->entry_table_capacity) return VTX2_ERR_INVALID;

    uint64_t per_block = vtx2_entries_per_block(sb);
    uint64_t block_num = index / per_block;
    uint64_t in_block_index = index % per_block;

    vtx2fs_result_t res = dev->read_block(dev->device_context,
                                sb->entry_table_start_block + block_num,
                                sb->block_size, g_scratch_block);
    if (res != VTX2_OK) return res;

    vtx2_memcpy(g_scratch_block + in_block_index * sizeof(vtx2_entry_t), entry, sizeof(vtx2_entry_t));

    return vtx2_journal_write_transaction(dev, sb, sb->entry_table_start_block + block_num, g_scratch_block);
}

static vtx2fs_result_t vtx2_find_free_entry(vtx2fs_blockdev_t *dev, const vtx2_superblock_t *sb,
                                             uint64_t *out_index) {
    uint64_t per_block = vtx2_entries_per_block(sb);

    for (uint64_t blk = 0; blk < sb->entry_table_block_count; blk++) {
        vtx2fs_result_t res = dev->read_block(dev->device_context,
                                    sb->entry_table_start_block + blk,
                                    sb->block_size, g_scratch_block);
        if (res != VTX2_OK) return res;

        for (uint64_t i = 0; i < per_block; i++) {
            uint64_t index = blk * per_block + i;
            if (index >= sb->entry_table_capacity) break;

            vtx2_entry_t *e = (vtx2_entry_t *)(g_scratch_block + i * sizeof(vtx2_entry_t));
            if (e->type == VTX2_ENTRY_FREE) {
                *out_index = index;
                return VTX2_OK;
            }
        }
    }
    return VTX2_ERR_NO_SPACE;
}

static vtx2fs_result_t vtx2_find_child(vtx2fs_blockdev_t *dev, const vtx2_superblock_t *sb,
                                        uint64_t parent_index, const char *name,
                                        uint64_t *out_index, bool *out_found) {
    char normalized[VTX2_MAX_NAME_LEN];
    vtx2_name_copy(normalized, name);

    uint64_t per_block = vtx2_entries_per_block(sb);
    *out_found = false;

    for (uint64_t blk = 0; blk < sb->entry_table_block_count; blk++) {
        vtx2fs_result_t res = dev->read_block(dev->device_context,
                                    sb->entry_table_start_block + blk,
                                    sb->block_size, g_scratch_block);
        if (res != VTX2_OK) return res;

        for (uint64_t i = 0; i < per_block; i++) {
            uint64_t index = blk * per_block + i;
            if (index >= sb->entry_table_capacity) break;
            if (index == parent_index) continue;

            vtx2_entry_t *e = (vtx2_entry_t *)(g_scratch_block + i * sizeof(vtx2_entry_t));
            if (e->type == VTX2_ENTRY_FREE) continue;
            if (e->parent_entry_index != parent_index) continue;

            if (vtx2_memcmp(e->name, normalized, VTX2_MAX_NAME_LEN) == 0) {
                *out_index = index;
                *out_found = true;
                return VTX2_OK;
            }
        }
    }
    return VTX2_OK;
}

/* ------------------------------------------------------------------ */
/* Blok bitmap ayirici                                                  */
/* ------------------------------------------------------------------ */

static vtx2fs_result_t vtx2_bitmap_set(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                                        uint64_t block_index, bool used) {
    uint64_t byte_index = block_index / 8;
    uint8_t  bit_in_byte = (uint8_t)(block_index % 8);
    uint64_t bitmap_block = byte_index / sb->block_size;
    uint64_t byte_in_block = byte_index % sb->block_size;

    if (bitmap_block >= sb->bitmap_block_count) return VTX2_ERR_INVALID;

    vtx2fs_result_t res = dev->read_block(dev->device_context,
                                sb->bitmap_start_block + bitmap_block,
                                sb->block_size, g_scratch_block);
    if (res != VTX2_OK) return res;

    if (used) g_scratch_block[byte_in_block] |= (uint8_t)(1u << bit_in_byte);
    else      g_scratch_block[byte_in_block] &= (uint8_t)~(1u << bit_in_byte);

    return vtx2_journal_write_transaction(dev, sb, sb->bitmap_start_block + bitmap_block, g_scratch_block);
}

/* Ardisik `count` adet BOS blok arar (ilk-uyan / first-fit), bulunca hemen
 * "dolu" olarak isaretler. Bitmap taramasi blok-blok yapilir (bit-bit degil)
 * ki buyuk disklerde performans kabul edilebilir kalsin. */
static vtx2fs_result_t vtx2_alloc_blocks(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                                          uint64_t count, uint64_t *out_start_block) {
    if (count == 0) return VTX2_ERR_INVALID;

    uint64_t run_start = 0;
    uint64_t run_len = 0;
    uint64_t loaded_bitmap_block = (uint64_t)-1;

    for (uint64_t b = sb->data_start_block; b < sb->total_blocks; b++) {
        uint64_t byte_index = b / 8;
        uint8_t  bit_in_byte = (uint8_t)(b % 8);
        uint64_t bitmap_block = byte_index / sb->block_size;
        uint64_t byte_in_block = byte_index % sb->block_size;

        if (bitmap_block != loaded_bitmap_block) {
            vtx2fs_result_t res = dev->read_block(dev->device_context,
                                        sb->bitmap_start_block + bitmap_block,
                                        sb->block_size, g_scratch_block);
            if (res != VTX2_OK) return res;
            loaded_bitmap_block = bitmap_block;
        }

        bool used = (g_scratch_block[byte_in_block] & (1u << bit_in_byte)) != 0;

        if (!used) {
            if (run_len == 0) run_start = b;
            run_len++;
            if (run_len == count) {
                for (uint64_t i = 0; i < count; i++) {
                    vtx2fs_result_t res = vtx2_bitmap_set(dev, sb, run_start + i, true);
                    if (res != VTX2_OK) return res;
                }
                *out_start_block = run_start;
                if (sb->free_blocks_hint >= count) sb->free_blocks_hint -= count;
                return VTX2_OK;
            }
        } else {
            run_len = 0;
        }
    }

    return VTX2_ERR_NO_SPACE;
}

static vtx2fs_result_t vtx2_free_blocks(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                                         uint64_t start_block, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        vtx2fs_result_t res = vtx2_bitmap_set(dev, sb, start_block + i, false);
        if (res != VTX2_OK) return res;
    }
    sb->free_blocks_hint += count;
    return VTX2_OK;
}

/* ------------------------------------------------------------------ */
/* Extent listesi erisimi (dogrudan + tek seviye indirect)              */
/* ------------------------------------------------------------------ */

static vtx2fs_result_t vtx2_get_extent(vtx2fs_blockdev_t *dev, const vtx2_superblock_t *sb,
                                        const vtx2_entry_t *entry, uint64_t n, vtx2_extent_t *out) {
    if (n < VTX2_DIRECT_EXTENTS) {
        *out = entry->direct_extents[n];
        return VTX2_OK;
    }
    if (entry->indirect_extent_block == 0) return VTX2_ERR_INVALID;

    uint64_t idx_in_indirect = n - VTX2_DIRECT_EXTENTS;
    uint64_t extents_per_block = sb->block_size / sizeof(vtx2_extent_t);
    if (idx_in_indirect >= extents_per_block) return VTX2_ERR_INVALID; /* cift-seviye indirect yok */

    vtx2fs_result_t res = dev->read_block(dev->device_context, entry->indirect_extent_block,
                                sb->block_size, g_scratch_block2);
    if (res != VTX2_OK) return res;

    vtx2_memcpy(out, g_scratch_block2 + idx_in_indirect * sizeof(vtx2_extent_t), sizeof(vtx2_extent_t));
    return VTX2_OK;
}

static vtx2fs_result_t vtx2_append_extent(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                                          vtx2_entry_t *entry, vtx2_extent_t new_ext) {
    uint64_t extents_per_indirect = sb->block_size / sizeof(vtx2_extent_t);
    uint64_t max_extents = VTX2_DIRECT_EXTENTS + extents_per_indirect;
    if (entry->extent_count >= max_extents) return VTX2_ERR_NO_SPACE;

    if (entry->extent_count < VTX2_DIRECT_EXTENTS) {
        entry->direct_extents[entry->extent_count] = new_ext;
    } else {
        uint64_t idx_in_indirect = entry->extent_count - VTX2_DIRECT_EXTENTS;

        if (entry->indirect_extent_block == 0) {
            uint64_t indirect_block;
            vtx2fs_result_t res = vtx2_alloc_blocks(dev, sb, 1, &indirect_block);
            if (res != VTX2_OK) return res;
            vtx2_memset(g_scratch_block2, 0, sb->block_size);
            res = dev->write_block(dev->device_context, indirect_block, sb->block_size, g_scratch_block2);
            if (res != VTX2_OK) return res;
            entry->indirect_extent_block = indirect_block;
        }

        vtx2fs_result_t res = dev->read_block(dev->device_context, entry->indirect_extent_block,
                                    sb->block_size, g_scratch_block2);
        if (res != VTX2_OK) return res;
        vtx2_memcpy(g_scratch_block2 + idx_in_indirect * sizeof(vtx2_extent_t), &new_ext, sizeof(new_ext));
        res = dev->write_block(dev->device_context, entry->indirect_extent_block,
                                sb->block_size, g_scratch_block2);
        if (res != VTX2_OK) return res;
    }

    entry->extent_count++;
    return VTX2_OK;
}

/* ------------------------------------------------------------------ */
/* Ust seviye API                                                       */
/* ------------------------------------------------------------------ */

static vtx2fs_result_t vtx2_create_entry(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                                          uint64_t parent_dir_index, const char *name,
                                          uint8_t type, uint64_t *out_entry_index) {
    if (parent_dir_index >= sb->entry_table_capacity) return VTX2_ERR_INVALID;

    vtx2_entry_t parent;
    vtx2fs_result_t res = vtx2_read_entry(dev, sb, parent_dir_index, &parent);
    if (res != VTX2_OK) return res;
    if (parent.type != VTX2_ENTRY_DIRECTORY) return VTX2_ERR_NOT_DIR;

    uint64_t name_len = vtx2_strlen_bounded(name, VTX2_MAX_NAME_LEN);
    if (name_len >= VTX2_MAX_NAME_LEN - 1) return VTX2_ERR_NAME_TOO_LONG;
    if (name_len == 0) return VTX2_ERR_INVALID;

    uint64_t existing_index;
    bool found;
    res = vtx2_find_child(dev, sb, parent_dir_index, name, &existing_index, &found);
    if (res != VTX2_OK) return res;
    if (found) return VTX2_ERR_EXISTS;

    uint64_t new_index;
    res = vtx2_find_free_entry(dev, sb, &new_index);
    if (res != VTX2_OK) return res;

    vtx2_entry_t new_entry;
    vtx2_memset(&new_entry, 0, sizeof(new_entry));
    vtx2_name_copy(new_entry.name, name);
    new_entry.type = type;
    new_entry.size_bytes = 0;
    new_entry.created_time = 0;  /* TODO: VTX2'ye bir RTC/PIT saati eklendiginde doldurulacak */
    new_entry.modified_time = 0;
    new_entry.extent_count = 0;
    new_entry.indirect_extent_block = 0;
    new_entry.parent_entry_index = parent_dir_index;

    res = vtx2_write_entry(dev, sb, new_index, &new_entry);
    if (res != VTX2_OK) return res;

    *out_entry_index = new_index;
    return VTX2_OK;
}

vtx2fs_result_t vtx2_create_file(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                                  uint64_t parent_dir_index, const char *name,
                                  uint64_t *out_entry_index) {
    return vtx2_create_entry(dev, sb, parent_dir_index, name, VTX2_ENTRY_FILE, out_entry_index);
}

vtx2fs_result_t vtx2_mkdir(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                            uint64_t parent_dir_index, const char *name,
                            uint64_t *out_entry_index) {
    return vtx2_create_entry(dev, sb, parent_dir_index, name, VTX2_ENTRY_DIRECTORY, out_entry_index);
}

vtx2fs_result_t vtx2_read(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                           uint64_t entry_index, uint64_t offset,
                           void *out_buffer, uint64_t length, uint64_t *out_read) {
    if (out_read) *out_read = 0;
    if (length == 0) return VTX2_OK;

    vtx2_entry_t entry;
    vtx2fs_result_t res = vtx2_read_entry(dev, sb, entry_index, &entry);
    if (res != VTX2_OK) return res;
    if (entry.type != VTX2_ENTRY_FILE) return VTX2_ERR_IS_DIR;

    if (offset >= entry.size_bytes) return VTX2_OK; /* EOF */

    uint64_t effective_len = length;
    if (offset + effective_len > entry.size_bytes) effective_len = entry.size_bytes - offset;

    uint8_t *dst = (uint8_t *)out_buffer;
    uint64_t remaining = effective_len;
    uint64_t logical_pos = offset;
    uint64_t total_read = 0;

    while (remaining > 0) {
        uint64_t extent_base = 0;
        uint64_t found_n = (uint64_t)-1;
        vtx2_extent_t ext = {0, 0};

        for (uint64_t n = 0; n < entry.extent_count; n++) {
            res = vtx2_get_extent(dev, sb, &entry, n, &ext);
            if (res != VTX2_OK) return res;
            uint64_t ext_bytes = ext.block_count * (uint64_t)sb->block_size;
            if (logical_pos < extent_base + ext_bytes) { found_n = n; break; }
            extent_base += ext_bytes;
        }
        if (found_n == (uint64_t)-1) return VTX2_ERR_INVALID;

        uint64_t local_off = logical_pos - extent_base;
        uint64_t block_in_extent = local_off / sb->block_size;
        uint64_t in_block_off = local_off % sb->block_size;
        uint64_t disk_block = ext.start_block + block_in_extent;

        uint64_t chunk = sb->block_size - in_block_off;
        if (chunk > remaining) chunk = remaining;

        res = dev->read_block(dev->device_context, disk_block, sb->block_size, g_scratch_block);
        if (res != VTX2_OK) return res;

        vtx2_memcpy(dst, g_scratch_block + in_block_off, chunk);

        dst += chunk;
        logical_pos += chunk;
        remaining -= chunk;
        total_read += chunk;
    }

    if (out_read) *out_read = total_read;
    return VTX2_OK;
}

vtx2fs_result_t vtx2_write(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                            uint64_t entry_index, uint64_t offset,
                            const void *in_buffer, uint64_t length, uint64_t *out_written) {
    if (out_written) *out_written = 0;
    if (length == 0) return VTX2_OK;

    vtx2_entry_t entry;
    vtx2fs_result_t res = vtx2_read_entry(dev, sb, entry_index, &entry);
    if (res != VTX2_OK) return res;
    if (entry.type != VTX2_ENTRY_FILE) return VTX2_ERR_IS_DIR;

    /* mevcut tahsis edilmis kapasiteyi hesapla (tum extent'lerin toplam bayti) */
    uint64_t capacity = 0;
    for (uint64_t n = 0; n < entry.extent_count; n++) {
        vtx2_extent_t ext;
        res = vtx2_get_extent(dev, sb, &entry, n, &ext);
        if (res != VTX2_OK) return res;
        capacity += ext.block_count * (uint64_t)sb->block_size;
    }

    uint64_t needed_end = offset + length;
    if (needed_end > capacity) {
        uint64_t extra_bytes = needed_end - capacity;
        uint64_t extra_blocks = (extra_bytes + sb->block_size - 1) / sb->block_size;

        uint64_t new_start;
        res = vtx2_alloc_blocks(dev, sb, extra_blocks, &new_start);
        if (res != VTX2_OK) return res;

        vtx2_extent_t new_ext;
        new_ext.start_block = new_start;
        new_ext.block_count = extra_blocks;

        res = vtx2_append_extent(dev, sb, &entry, new_ext);
        if (res != VTX2_OK) {
            vtx2_free_blocks(dev, sb, new_start, extra_blocks);
            return res;
        }
    }

    const uint8_t *src = (const uint8_t *)in_buffer;
    uint64_t remaining = length;
    uint64_t logical_pos = offset;
    uint64_t total_written = 0;

    while (remaining > 0) {
        uint64_t extent_base = 0;
        uint64_t found_n = (uint64_t)-1;
        vtx2_extent_t ext = {0, 0};

        for (uint64_t n = 0; n < entry.extent_count; n++) {
            res = vtx2_get_extent(dev, sb, &entry, n, &ext);
            if (res != VTX2_OK) return res;
            uint64_t ext_bytes = ext.block_count * (uint64_t)sb->block_size;
            if (logical_pos < extent_base + ext_bytes) { found_n = n; break; }
            extent_base += ext_bytes;
        }
        if (found_n == (uint64_t)-1) return VTX2_ERR_INVALID;

        uint64_t local_off = logical_pos - extent_base;
        uint64_t block_in_extent = local_off / sb->block_size;
        uint64_t in_block_off = local_off % sb->block_size;
        uint64_t disk_block = ext.start_block + block_in_extent;

        uint64_t chunk = sb->block_size - in_block_off;
        if (chunk > remaining) chunk = remaining;

        if (chunk < sb->block_size) {
            /* kismi blok yazimi - once oku, degistir, geri yaz */
            res = dev->read_block(dev->device_context, disk_block, sb->block_size, g_scratch_block);
            if (res != VTX2_OK) return res;
        }
        vtx2_memcpy(g_scratch_block + in_block_off, src, chunk);
        res = dev->write_block(dev->device_context, disk_block, sb->block_size, g_scratch_block);
        if (res != VTX2_OK) return res;

        src += chunk;
        logical_pos += chunk;
        remaining -= chunk;
        total_written += chunk;
    }

    if (needed_end > entry.size_bytes) entry.size_bytes = needed_end;

    res = vtx2_write_entry(dev, sb, entry_index, &entry);
    if (res != VTX2_OK) return res;

    if (out_written) *out_written = total_written;
    return VTX2_OK;
}

vtx2fs_result_t vtx2_list_dir(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                               uint64_t dir_entry_index,
                               uint64_t *out_indices, uint64_t max_out, uint64_t *out_count) {
    if (out_count) *out_count = 0;

    vtx2_entry_t dir_entry;
    vtx2fs_result_t res = vtx2_read_entry(dev, sb, dir_entry_index, &dir_entry);
    if (res != VTX2_OK) return res;
    if (dir_entry.type != VTX2_ENTRY_DIRECTORY) return VTX2_ERR_NOT_DIR;

    uint64_t per_block = vtx2_entries_per_block(sb);
    uint64_t count = 0;

    for (uint64_t blk = 0; blk < sb->entry_table_block_count; blk++) {
        res = dev->read_block(dev->device_context, sb->entry_table_start_block + blk,
                               sb->block_size, g_scratch_block);
        if (res != VTX2_OK) return res;

        for (uint64_t i = 0; i < per_block; i++) {
            uint64_t index = blk * per_block + i;
            if (index >= sb->entry_table_capacity) break;
            if (index == dir_entry_index) continue;

            vtx2_entry_t *e = (vtx2_entry_t *)(g_scratch_block + i * sizeof(vtx2_entry_t));
            if (e->type == VTX2_ENTRY_FREE) continue;
            if (e->parent_entry_index != dir_entry_index) continue;

            if (count < max_out) out_indices[count] = index;
            count++;
        }
    }

    if (out_count) *out_count = count;
    return VTX2_OK;
}

vtx2fs_result_t vtx2_stat(vtx2fs_blockdev_t *dev, const vtx2_superblock_t *sb,
                           uint64_t entry_index, vtx2_entry_t *out_entry) {
    return vtx2_read_entry(dev, sb, entry_index, out_entry);
}

vtx2fs_result_t vtx2_truncate(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                               uint64_t entry_index, uint64_t new_size) {
    vtx2_entry_t entry;
    vtx2fs_result_t res = vtx2_read_entry(dev, sb, entry_index, &entry);
    if (res != VTX2_OK) return res;
    if (entry.type != VTX2_ENTRY_FILE) return VTX2_ERR_IS_DIR;
    if (new_size > entry.size_bytes) return VTX2_ERR_INVALID; /* buyutme icin vtx2_write kullanilmali */

    entry.size_bytes = new_size;
    return vtx2_write_entry(dev, sb, entry_index, &entry);
}

/* ------------------------------------------------------------------ */
/* Sifreleme (v1: XOR obfuscation - GERCEK GUVENLIK DEGIL)              */
/*                                                                       */
/* UYARI: Asagidaki XOR semasi bilinen-duz-metin ve frekans analizi     */
/* saldirilarina karsi SAVUNMASIZDIR. Sadece parolasiz rastgele goz     */
/* atmayi engeller. Tuz uretimi de gercek bir RNG/entropy kaynagi       */
/* KULLANMAZ (donanim RNG'si veya PIT tabanli bir entropy havuzu henuz  */
/* yok) - ayni onyukleme oturumu icinde degisken ama REBOOT'LAR ARASI   */
/* TAHMIN EDILEBILIR. Hassas veri icin AES (encryption_scheme=2) ve     */
/* gercek bir entropy kaynagi beklenmelidir - v1'in acikca kabul        */
/* edilen bir sinirlamasidir.                                          */
/* ------------------------------------------------------------------ */

static uint32_t g_salt_counter = 0x9E3779B9u;

static void generate_weak_salt(uint64_t mix, uint8_t out_salt[16]) {
    uint32_t s = g_salt_counter;
    s ^= (uint32_t)(mix & 0xFFFFFFFFu);
    s ^= (uint32_t)(mix >> 32);
    for (int i = 0; i < 16; i++) {
        s = s * 1103515245u + 12345u;
        out_salt[i] = (uint8_t)(s >> 24);
    }
    g_salt_counter = s ^ 0xA5A5A5A5u;
}

void vtx2_derive_key(const char *password, const uint8_t salt[16], uint8_t out_key[32]) {
    uint64_t pass_len = vtx2_strlen_bounded(password, 256);
    uint8_t material[256 + 16];
    uint64_t m = 0;
    for (uint64_t i = 0; i < pass_len; i++) material[m++] = (uint8_t)password[i];
    for (int i = 0; i < 16; i++) material[m++] = salt[i];

    for (int block = 0; block < 8; block++) { /* 8*4 = 32 bayt */
        uint32_t h = 0x811C9DC5u ^ (uint32_t)((uint64_t)block * 0x9E3779B9ull);
        for (uint64_t i = 0; i < m; i++) { h ^= material[i]; h *= 0x01000193u; }
        out_key[block * 4 + 0] = (uint8_t)(h & 0xFF);
        out_key[block * 4 + 1] = (uint8_t)((h >> 8) & 0xFF);
        out_key[block * 4 + 2] = (uint8_t)((h >> 16) & 0xFF);
        out_key[block * 4 + 3] = (uint8_t)((h >> 24) & 0xFF);
    }
}

static uint32_t compute_verifier(const uint8_t key[32], const uint8_t salt[16]) {
    uint8_t material[32 + 16 + 8];
    uint64_t m = 0;
    for (int i = 0; i < 32; i++) material[m++] = key[i];
    for (int i = 0; i < 16; i++) material[m++] = salt[i];
    const char *tag = "VTX2CHK";
    for (int i = 0; tag[i]; i++) material[m++] = (uint8_t)tag[i];
    return vtx2_fnv1a(material, m);
}

bool vtx2_check_password(const uint8_t key[32], const uint8_t salt[16], const uint8_t verifier[4]) {
    uint32_t h = compute_verifier(key, salt);
    uint8_t computed[4] = {
        (uint8_t)(h & 0xFF), (uint8_t)((h >> 8) & 0xFF),
        (uint8_t)((h >> 16) & 0xFF), (uint8_t)((h >> 24) & 0xFF)
    };
    return vtx2_memcmp(computed, verifier, 4) == 0;
}

/* Konum-bagimli XOR akis baytı - ayni (key,position) her zaman ayni baytı
 * uretir, bu da rastgele-erisimli okuma/yazmada (offset'ten baslayarak)
 * dogru cozmeyi/sifrelemeyi mumkun kilar. */
static uint8_t xor_keystream_byte(const uint8_t key[32], uint64_t position) {
    uint32_t h = 0x811C9DC5u;
    for (int i = 0; i < 32; i++) { h ^= key[i]; h *= 0x01000193u; }
    h ^= (uint32_t)(position & 0xFFFFFFFFu);
    h *= 0x01000193u;
    h ^= (uint32_t)(position >> 32);
    h *= 0x01000193u;
    h ^= (h >> 15);
    return (uint8_t)(h & 0xFF);
}

/* XOR simetriktir: ayni fonksiyon hem sifreler hem cozer. */
static void xor_crypt_buffer(const uint8_t key[32], uint64_t start_position, uint8_t *buf, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(buf[i] ^ xor_keystream_byte(key, start_position + i));
    }
}

static vtx2fs_result_t vtx2_finish_encrypted_entry(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                                                     uint64_t idx, const char *password) {
    vtx2_entry_t entry;
    vtx2fs_result_t res = vtx2_read_entry(dev, sb, idx, &entry);
    if (res != VTX2_OK) return res;

    generate_weak_salt(idx ^ entry.parent_entry_index, entry.salt);

    uint8_t key[32];
    vtx2_derive_key(password, entry.salt, key);

    uint32_t h = compute_verifier(key, entry.salt);
    entry.verifier[0] = (uint8_t)(h & 0xFF);
    entry.verifier[1] = (uint8_t)((h >> 8) & 0xFF);
    entry.verifier[2] = (uint8_t)((h >> 16) & 0xFF);
    entry.verifier[3] = (uint8_t)((h >> 24) & 0xFF);

    entry.encryption_scheme = 1; /* XOR (v1) */
    entry.name_encrypted = 1;
    xor_crypt_buffer(key, 0, (uint8_t *)entry.name, VTX2_MAX_NAME_LEN);

    return vtx2_write_entry(dev, sb, idx, &entry);
}

vtx2fs_result_t vtx2_create_file_encrypted(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                                            uint64_t parent_dir_index, const char *name,
                                            const char *password, uint64_t *out_entry_index) {
    uint64_t idx;
    vtx2fs_result_t res = vtx2_create_entry(dev, sb, parent_dir_index, name, VTX2_ENTRY_FILE, &idx);
    if (res != VTX2_OK) return res;
    res = vtx2_finish_encrypted_entry(dev, sb, idx, password);
    if (res != VTX2_OK) return res;
    *out_entry_index = idx;
    return VTX2_OK;
}

vtx2fs_result_t vtx2_mkdir_encrypted(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                                      uint64_t parent_dir_index, const char *name,
                                      const char *password, uint64_t *out_entry_index) {
    uint64_t idx;
    vtx2fs_result_t res = vtx2_create_entry(dev, sb, parent_dir_index, name, VTX2_ENTRY_DIRECTORY, &idx);
    if (res != VTX2_OK) return res;
    res = vtx2_finish_encrypted_entry(dev, sb, idx, password);
    if (res != VTX2_OK) return res;
    *out_entry_index = idx;
    return VTX2_OK;
}

vtx2fs_result_t vtx2_read_encrypted(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                                     uint64_t entry_index, const char *password,
                                     uint64_t offset, void *out_buffer, uint64_t length,
                                     uint64_t *out_read) {
    if (out_read) *out_read = 0;

    vtx2_entry_t entry;
    vtx2fs_result_t res = vtx2_read_entry(dev, sb, entry_index, &entry);
    if (res != VTX2_OK) return res;
    if (entry.encryption_scheme == 0) return VTX2_ERR_INVALID;

    uint8_t key[32];
    vtx2_derive_key(password, entry.salt, key);
    if (!vtx2_check_password(key, entry.salt, entry.verifier)) return VTX2_ERR_INVALID;

    uint64_t read_count = 0;
    res = vtx2_read(dev, sb, entry_index, offset, out_buffer, length, &read_count);
    if (res != VTX2_OK) return res;

    xor_crypt_buffer(key, offset, (uint8_t *)out_buffer, read_count);

    if (out_read) *out_read = read_count;
    return VTX2_OK;
}

vtx2fs_result_t vtx2_write_encrypted(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb,
                                      uint64_t entry_index, const char *password,
                                      uint64_t offset, const void *in_buffer, uint64_t length,
                                      uint64_t *out_written) {
    if (out_written) *out_written = 0;
    if (length == 0) return VTX2_OK;

    vtx2_entry_t entry;
    vtx2fs_result_t res = vtx2_read_entry(dev, sb, entry_index, &entry);
    if (res != VTX2_OK) return res;
    if (entry.encryption_scheme == 0) return VTX2_ERR_INVALID;

    uint8_t key[32];
    vtx2_derive_key(password, entry.salt, key);
    if (!vtx2_check_password(key, entry.salt, entry.verifier)) return VTX2_ERR_INVALID;

    const uint8_t *src = (const uint8_t *)in_buffer;
    uint64_t remaining = length;
    uint64_t pos = offset;
    uint64_t total = 0;
    uint8_t chunk[512];

    while (remaining > 0) {
        uint64_t take = remaining > sizeof(chunk) ? sizeof(chunk) : remaining;
        vtx2_memcpy(chunk, src, take);
        xor_crypt_buffer(key, pos, chunk, take);

        uint64_t written_now = 0;
        res = vtx2_write(dev, sb, entry_index, pos, chunk, take, &written_now);
        if (res != VTX2_OK) return res;

        src += take;
        pos += take;
        remaining -= take;
        total += written_now;
        if (written_now < take) break;
    }

    if (out_written) *out_written = total;
    return VTX2_OK;
}

vtx2fs_result_t vtx2_decrypt_name(const vtx2_entry_t *entry, const char *password, char *out_name) {
    if (!entry->name_encrypted) {
        vtx2_memcpy(out_name, entry->name, VTX2_MAX_NAME_LEN);
        return VTX2_OK;
    }

    uint8_t key[32];
    vtx2_derive_key(password, entry->salt, key);
    if (!vtx2_check_password(key, entry->salt, entry->verifier)) return VTX2_ERR_INVALID;

    vtx2_memcpy(out_name, entry->name, VTX2_MAX_NAME_LEN);
    xor_crypt_buffer(key, 0, (uint8_t *)out_name, VTX2_MAX_NAME_LEN);
    return VTX2_OK;
}

vtx2fs_result_t vtx2_delete(vtx2fs_blockdev_t *dev, vtx2_superblock_t *sb, uint64_t entry_index) {
    if (entry_index == sb->root_entry_index) return VTX2_ERR_INVALID;

    vtx2_entry_t entry;
    vtx2fs_result_t res = vtx2_read_entry(dev, sb, entry_index, &entry);
    if (res != VTX2_OK) return res;
    if (entry.type == VTX2_ENTRY_FREE) return VTX2_ERR_NOT_FOUND;

    if (entry.type == VTX2_ENTRY_DIRECTORY) {
        uint64_t dummy;
        uint64_t child_count = 0;
        res = vtx2_list_dir(dev, sb, entry_index, &dummy, 0, &child_count);
        if (res != VTX2_OK) return res;
        if (child_count > 0) return VTX2_ERR_NOT_EMPTY;
    } else {
        for (uint64_t n = 0; n < entry.extent_count; n++) {
            vtx2_extent_t ext;
            res = vtx2_get_extent(dev, sb, &entry, n, &ext);
            if (res != VTX2_OK) return res;
            if (ext.block_count > 0) {
                res = vtx2_free_blocks(dev, sb, ext.start_block, ext.block_count);
                if (res != VTX2_OK) return res;
            }
        }
        if (entry.indirect_extent_block != 0) {
            res = vtx2_free_blocks(dev, sb, entry.indirect_extent_block, 1);
            if (res != VTX2_OK) return res;
        }
    }

    vtx2_entry_t freed;
    vtx2_memset(&freed, 0, sizeof(freed));
    freed.type = VTX2_ENTRY_FREE;
    return vtx2_write_entry(dev, sb, entry_index, &freed);
}