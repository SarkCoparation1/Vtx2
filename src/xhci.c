#include "xhci.h"
#include "pci.h"
#include "pmm.h"
#include "pit.h"
#include "io.h"
#include "paging.h"
#include <stddef.h>

#define XHCI_CLASS 0x0C
#define XHCI_SUBCLASS 0x03
#define XHCI_PROGIF 0x30

#define XHCI_MAX_SLOTS_CAP 32
#define XHCI_MAX_PORTS_CAP 32

#define XHCI_TIMEOUT_TICKS 1000u
#define XHCI_TIMEOUT_SPINS 4096u

#define COMMAND_RING_TRB_COUNT 256
#define EVENT_RING_TRB_COUNT 256

#define PORT_DEBOUNCE_STABLE_COUNT 3

#define TRB_TYPE_LINK 6
#define TRB_TYPE_ENABLE_SLOT 9
#define TRB_TYPE_ADDRESS_DEVICE 11
#define TRB_TYPE_SETUP_STAGE 2
#define TRB_TYPE_DATA_STAGE 3
#define TRB_TYPE_STATUS_STAGE 4
#define TRB_TYPE_TRANSFER_EVENT 32
#define TRB_TYPE_CMD_COMPLETION 33
#define TRB_TYPE_PORT_STATUS_CHG 34

#define TRB_COMPLETION_SUCCESS 1
#define USB_REQ_GET_DESCRIPTOR 6
#define USB_DESC_DEVICE 1

#define PORTSC_CCS (1u << 0)
#define PORTSC_PED (1u << 1)
#define PORTSC_PR (1u << 4)
#define PORTSC_PP (1u << 9)
#define PORTSC_SPEED_SHIFT 10
#define PORTSC_SPEED_MASK 0xFu
#define PORTSC_CSC (1u << 17)
#define PORTSC_PEC (1u << 18)
#define PORTSC_WRC (1u << 19)
#define PORTSC_OCC (1u << 20)
#define PORTSC_PRC (1u << 21)
#define PORTSC_PLC (1u << 22)
#define PORTSC_CEC (1u << 23)
#define PORTSC_RW1C_MASK (PORTSC_CSC|PORTSC_PEC|PORTSC_WRC|PORTSC_OCC|PORTSC_PRC|PORTSC_PLC|PORTSC_CEC)

typedef struct {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} __attribute__((packed)) xhci_trb_t;

typedef struct {
    uint64_t ring_segment_base;
    uint32_t ring_segment_size;
    uint32_t reserved;
} __attribute__((packed)) xhci_erst_entry_t;

static volatile uint8_t *g_cap_base = NULL;
static volatile uint8_t *g_op_base = NULL;
static volatile uint8_t *g_rt_base = NULL;
static volatile uint8_t *g_db_base = NULL;
static volatile uint8_t *g_port_base = NULL;

static uint8_t g_max_slots = 0;
static uint8_t g_max_ports = 0;
static bool g_initialized = false;

static uint32_t g_context_size = 32;

static xhci_trb_t *g_cmd_ring = NULL;
static uint32_t g_cmd_enqueue_index = 0;
static uint32_t g_cmd_cycle = 1;

static xhci_trb_t *g_evt_ring = NULL;
static uint32_t g_evt_dequeue_index = 0;
static uint32_t g_evt_cycle = 1;
static volatile uint8_t *g_ir0 = NULL;

static uint64_t *g_dcbaa = NULL;

static uint8_t g_port_last_ccs[XHCI_MAX_PORTS_CAP];
static uint8_t g_port_stable_count[XHCI_MAX_PORTS_CAP];
static uint8_t g_port_slot[XHCI_MAX_PORTS_CAP];
typedef struct {
    xhci_trb_t *ep0_ring;
    uint32_t ep0_enqueue_index;
    uint32_t ep0_cycle;
    uint8_t device_descriptor[18];
    bool descriptor_valid;
    const char *error;
} xhci_device_t;
static xhci_device_t g_devices[XHCI_MAX_SLOTS_CAP + 1];

static const char *g_last_error = "xhci_init() hic cagirilmadi";

#define REG8(base, off) (*((volatile uint8_t  *)((base) + (off))))
#define REG32(base, off) (*(volatile uint32_t *)((base) + (off)))
#define REG64(base, off) (*(volatile uint64_t *)((base) + (off)))

#define FAIL(msg) do { g_last_error = (msg); return false; } while (0)
#define XHCI_MARK(c) outb(0xE9, (uint8_t)(c))

static bool wait_until_clear32(volatile uint8_t *base, uint32_t off, uint32_t mask) {
    uint64_t deadline = pit_get_ticks() + XHCI_TIMEOUT_TICKS;
    for (uint32_t spins = 0; spins < XHCI_TIMEOUT_SPINS; spins++) {
        if ((REG32(base, off) & mask) == 0) return true;
        if (pit_get_ticks() >= deadline) return false;
        __asm__ volatile ("pause");
    }
    return false;
}

static bool wait_until_set32(volatile uint8_t *base, uint32_t off, uint32_t mask) {
    uint64_t deadline = pit_get_ticks() + XHCI_TIMEOUT_TICKS;
    for (uint32_t spins = 0; spins < XHCI_TIMEOUT_SPINS; spins++) {
        if ((REG32(base, off) & mask) == mask) return true;
        if (pit_get_ticks() >= deadline) return false;
        __asm__ volatile ("pause");
    }
    return false;
}

static void zero_page(void *p) {
    volatile uint8_t *b = (volatile uint8_t *)p;
    for (uint32_t i = 0; i < PMM_PAGE_SIZE; i++) {
        b[i] = 0;
    }
}

bool xhci_init(void) {
    g_initialized = false;
    XHCI_MARK('X');

    pci_device_t dev;
    if (!pci_find_device_by_class(XHCI_CLASS, XHCI_SUBCLASS, XHCI_PROGIF, &dev)) {
        FAIL("PCI'de xHCI controller bulunamadi");
    }

    pci_enable_bus_mastering(&dev);
    XHCI_MARK('I');

    uint64_t bar0 = pci_get_bar_address(&dev, 0);
    if (bar0 == 0) {
        FAIL("BAR0 gecersiz/0 (MMIO degil)");
    }

    XHCI_MARK('J');
    g_cap_base = (volatile uint8_t *)bar0;

    if (!paging_identity_map_region(bar0, 0x10000)) {
        FAIL("BAR0 icin sayfa haritasi genisletilemedi (bellek tukendi mi?)");
    }

    uint8_t cap_length = REG8(g_cap_base, 0x00);
    g_op_base = g_cap_base + cap_length;

    uint32_t hcsparams1 = REG32(g_cap_base, 0x04);
    uint32_t max_slots_hw = hcsparams1 & 0xFFu;
    uint32_t max_ports_hw = (hcsparams1 >> 24) & 0xFFu;

    g_max_slots = (uint8_t)(max_slots_hw > XHCI_MAX_SLOTS_CAP ? XHCI_MAX_SLOTS_CAP : max_slots_hw);
    g_max_ports = (uint8_t)(max_ports_hw > XHCI_MAX_PORTS_CAP ? XHCI_MAX_PORTS_CAP : max_ports_hw);

    if (g_max_slots == 0 || g_max_ports == 0) {
        FAIL("HCSPARAMS1: MaxSlots ya da MaxPorts = 0 (anlamsiz donanim raporu)");
    }

    uint32_t dboff  = REG32(g_cap_base, 0x14) & 0xFFFFFFFCu;
    uint32_t rtsoff = REG32(g_cap_base, 0x18) & 0xFFFFFFE0u;

    uint32_t hccparams1 = REG32(g_cap_base, 0x10);
    g_context_size = (hccparams1 & (1u << 2)) ? 64 : 32;

    g_db_base = g_cap_base + dboff;
    g_rt_base = g_cap_base + rtsoff;
    g_port_base = g_op_base + 0x400;

    for (uint32_t i = 0; i < XHCI_MAX_PORTS_CAP; i++) {
        g_port_last_ccs[i] = 0;
        g_port_stable_count[i] = 0;
        g_port_slot[i] = 0;
    }
    for (uint32_t i = 0; i <= XHCI_MAX_SLOTS_CAP; i++) {
        g_devices[i].ep0_ring = NULL;
        g_devices[i].descriptor_valid = false;
        g_devices[i].error = "Aygit henuz adreslenmedi";
    }

    if (!wait_until_clear32(g_op_base, 0x04, (1u << 11))) {
        FAIL("Asama 1: CNR (Controller Not Ready) hic dusmedi - timeout");
    }

    XHCI_MARK('K');
    if (REG32(g_op_base, 0x00) & 0x1u) {
        REG32(g_op_base, 0x00) = REG32(g_op_base, 0x00) & ~0x1u;
        if (!wait_until_set32(g_op_base, 0x04, (1u << 0))) { /* HCH */
            FAIL("Asama 2: RS temizlendi ama HCH (Halted) hic set olmadi - timeout");
        }
    }

    XHCI_MARK('L');
    REG32(g_op_base, 0x00) = REG32(g_op_base, 0x00) | (1u << 1);
    if (!wait_until_clear32(g_op_base, 0x00, (1u << 1))) {
        FAIL("Asama 3: HCRST hic temizlenmedi - timeout");
    }
    if (!wait_until_clear32(g_op_base, 0x04, (1u << 11))) {
        FAIL("Asama 3: reset sonrasi CNR tekrar dusmedi - timeout");
    }

    XHCI_MARK('M');
    REG32(g_op_base, 0x38) = g_max_slots;
    uint64_t dcbaa_phys = pmm_alloc_page();
    if (!dcbaa_phys) {
        FAIL("Asama 5: DCBAA icin pmm_alloc_page basarisiz (bellek tukendi mi?)");
    }
    zero_page((void *)dcbaa_phys);
    g_dcbaa = (uint64_t *)dcbaa_phys;
    REG64(g_op_base, 0x30) = dcbaa_phys;

    uint64_t cmd_ring_phys = pmm_alloc_page();
    if (!cmd_ring_phys) {
        FAIL("Asama 6: Command Ring icin pmm_alloc_page basarisiz");
    }
    zero_page((void *)cmd_ring_phys);
    g_cmd_ring = (xhci_trb_t *)cmd_ring_phys;

    xhci_trb_t *link = &g_cmd_ring[COMMAND_RING_TRB_COUNT - 1];
    link->parameter = cmd_ring_phys;
    link->status = 0;
    link->control = (TRB_TYPE_LINK << 10) | (1u << 1) | 1u;

    REG64(g_op_base, 0x18) = cmd_ring_phys | 0x1u;
    g_cmd_enqueue_index = 0;
    g_cmd_cycle = 1;

    uint64_t evt_ring_phys = pmm_alloc_page();
    if (!evt_ring_phys) {
        FAIL("Asama 7: Event Ring icin pmm_alloc_page basarisiz");
    }
    zero_page((void *)evt_ring_phys);
    g_evt_ring = (xhci_trb_t *)evt_ring_phys;
    g_evt_dequeue_index = 0;
    g_evt_cycle = 1;

    uint64_t erst_phys = pmm_alloc_page();
    if (!erst_phys) {
        FAIL("Asama 7: ERST icin pmm_alloc_page basarisiz");
    }
    zero_page((void *)erst_phys);

    xhci_erst_entry_t *erst = (xhci_erst_entry_t *)erst_phys;
    erst[0].ring_segment_base = evt_ring_phys;
    erst[0].ring_segment_size = EVENT_RING_TRB_COUNT;
    erst[0].reserved = 0;

    volatile uint8_t *ir0 = g_rt_base + 0x20;
    REG32(ir0, 0x08) = 1;
    REG64(ir0, 0x18) = evt_ring_phys;
    REG64(ir0, 0x10) = erst_phys;
    g_ir0 = ir0;
    XHCI_MARK('N');

    REG32(g_op_base, 0x00) = REG32(g_op_base, 0x00) | 0x1u;
    if (!wait_until_clear32(g_op_base, 0x04, (1u << 0))) {
        FAIL("Asama 8: RS set edildi ama HCH hic temizlenmedi - controller calismaya baslamadi");
    }

    XHCI_MARK('O');
    g_initialized = true;
    g_last_error = "OK";
    return true;
}

bool xhci_is_ready(void) {
    return g_initialized;
}

uint8_t xhci_get_port_count(void) {
    return g_initialized ? g_max_ports : 0;
}

const char *xhci_get_last_error(void) {
    return g_last_error;
}

uint32_t xhci_read_portsc(uint8_t port_index) {
    if (!g_initialized || port_index >= g_max_ports) return 0;
    return REG32(g_port_base, (uint32_t)port_index * 0x10);
}

void xhci_write_portsc(uint8_t port_index, uint32_t value) {
    if (!g_initialized || port_index >= g_max_ports) return;
    REG32(g_port_base, (uint32_t)port_index * 0x10) = value;
}

static bool consume_next_event(xhci_trb_t *out) {
    if (!g_evt_ring || !g_ir0) return false;

    xhci_trb_t *trb = &g_evt_ring[g_evt_dequeue_index];
    uint32_t cycle_bit = trb->control & 1u;

    if (cycle_bit != (g_evt_cycle & 1u)) {
        return false;
    }

    *out = *trb;

    g_evt_dequeue_index++;
    if (g_evt_dequeue_index == EVENT_RING_TRB_COUNT) {
        g_evt_dequeue_index = 0;
        g_evt_cycle ^= 1u;
    }

    uint64_t erdp = (uint64_t)(&g_evt_ring[g_evt_dequeue_index]) | (1u << 3); /* EHB ack */
    REG64(g_ir0, 0x18) = erdp;

    return true;
}

static bool poll_command_completion(uint64_t cmd_trb_phys, uint32_t *out_slot_id, uint32_t *out_completion_code) {
    uint64_t deadline = pit_get_ticks() + XHCI_TIMEOUT_TICKS;
    while (pit_get_ticks() < deadline) {
        xhci_trb_t evt;
        if (!consume_next_event(&evt)) {
            __asm__ volatile ("hlt");
            continue;
        }

        uint32_t trb_type = (evt.control >> 10) & 0x3Fu;

        if (trb_type == TRB_TYPE_CMD_COMPLETION && evt.parameter == cmd_trb_phys) {
            *out_completion_code = (evt.status >> 24) & 0xFFu;
            *out_slot_id = (evt.control >> 24) & 0xFFu;
            return true;
        }

    }
    return false;
}

static uint64_t cmd_ring_push(uint64_t parameter, uint32_t status, uint32_t control_no_cycle) {
    xhci_trb_t *trb = &g_cmd_ring[g_cmd_enqueue_index];
    uint64_t trb_phys = (uint64_t)trb;

    trb->parameter = parameter;
    trb->status = status;
    trb->control = control_no_cycle | (g_cmd_cycle & 1u);

    g_cmd_enqueue_index++;
    if (g_cmd_enqueue_index == COMMAND_RING_TRB_COUNT - 1) {
        xhci_trb_t *link = &g_cmd_ring[COMMAND_RING_TRB_COUNT - 1];
        link->control = (link->control & ~1u) | (g_cmd_cycle & 1u);
        g_cmd_enqueue_index = 0;
        g_cmd_cycle ^= 1u;
    }

    return trb_phys;
}

static void ring_doorbell(uint8_t index, uint8_t target) {
    REG32(g_db_base, (uint32_t)index * 4) = target;
}

static uint64_t ep0_ring_push(xhci_device_t *device, uint64_t parameter, uint32_t status, uint32_t control_no_cycle) {
    xhci_trb_t *trb = &device->ep0_ring[device->ep0_enqueue_index];
    uint64_t trb_phys = (uint64_t)trb;
    trb->parameter = parameter;
    trb->status = status;
    trb->control = control_no_cycle | (device->ep0_cycle & 1u);
    if (++device->ep0_enqueue_index == COMMAND_RING_TRB_COUNT - 1) {
        xhci_trb_t *link = &device->ep0_ring[COMMAND_RING_TRB_COUNT - 1];
        link->control = (link->control & ~1u) | (device->ep0_cycle & 1u);
        device->ep0_enqueue_index = 0;
        device->ep0_cycle ^= 1u;
    }
    return trb_phys;
}

static bool poll_transfer_completion(uint64_t trb_phys, uint8_t slot_id, uint32_t *out_completion_code) {
    uint64_t deadline = pit_get_ticks() + XHCI_TIMEOUT_TICKS;
    while (pit_get_ticks() < deadline) {
        xhci_trb_t evt;
        if (!consume_next_event(&evt)) {
            __asm__ volatile ("hlt");
            continue;
        }
        if (((evt.control >> 10) & 0x3Fu) == TRB_TYPE_TRANSFER_EVENT &&
            evt.parameter == trb_phys && ((evt.control >> 24) & 0xFFu) == slot_id) {
            *out_completion_code = (evt.status >> 24) & 0xFFu;
            return true;
        }
    }
    return false;
}

static bool ep0_get_device_descriptor(uint8_t slot_id) {
    xhci_device_t *device = &g_devices[slot_id];
    if (!device->ep0_ring) { device->error = "EP0 transfer ring yok"; return false; }
    uint64_t setup = 0x80ull | ((uint64_t)USB_REQ_GET_DESCRIPTOR << 8) | ((uint64_t)(USB_DESC_DEVICE << 8) << 16) | ((uint64_t)18 << 48);
    ep0_ring_push(device, setup, 8, (TRB_TYPE_SETUP_STAGE << 10) | (3u << 16) | (1u << 6) | (1u << 4));
    ep0_ring_push(device, (uint64_t)device->device_descriptor, 18, (TRB_TYPE_DATA_STAGE << 10) | (1u << 16) | (1u << 4));
    uint64_t status_trb = ep0_ring_push(device, 0, 0, (TRB_TYPE_STATUS_STAGE << 10) | (1u << 5));
    ring_doorbell(slot_id, 1);

    uint32_t completion;
    if (!poll_transfer_completion(status_trb, slot_id, &completion)) {
        device->error = "GET_DESCRIPTOR transfer eventi timeout";
        return false;
    }
    if (completion != TRB_COMPLETION_SUCCESS) {
        device->error = "GET_DESCRIPTOR tamamlanma kodu basarisiz";
        return false;
    }
    if (device->device_descriptor[0] < 18 || device->device_descriptor[1] != USB_DESC_DEVICE) {
        device->error = "Gecersiz USB device descriptor";
        return false;
    }
    device->descriptor_valid = true;
    device->error = "OK";
    XHCI_MARK('P');
    return true;
}

static uint16_t ep0_max_packet_size_for_speed(uint32_t speed) {
    switch (speed) {
        case 1: return 8;
        case 2: return 8;
        case 3: return 64;
        case 4: return 512;
        default: return 8;
    }
}

static bool reset_port_and_get_speed(uint8_t port_index, uint32_t *out_speed) {
    uint32_t val = xhci_read_portsc(port_index);

    xhci_write_portsc(port_index, (val & PORTSC_PP) | PORTSC_CSC | PORTSC_PR);

    if (!wait_until_set32(g_port_base, (uint32_t)port_index * 0x10, PORTSC_PRC)) {
        return false;
    }

    val = xhci_read_portsc(port_index);
    xhci_write_portsc(port_index, (val & PORTSC_PP) | PORTSC_PRC);

    val = xhci_read_portsc(port_index);
    if (!(val & PORTSC_PED)) {
        return false;
    }

    *out_speed = (val >> PORTSC_SPEED_SHIFT) & PORTSC_SPEED_MASK;
    return true;
}

static bool enumerate_new_device(uint8_t port_index) {
    uint32_t speed;
    if (!reset_port_and_get_speed(port_index, &speed)) {
        return false;
    }

    uint64_t cmd_phys = cmd_ring_push(0, 0, (TRB_TYPE_ENABLE_SLOT << 10));
    ring_doorbell(0, 0);

    uint32_t slot_id = 0, completion = 0;
    if (!poll_command_completion(cmd_phys, &slot_id, &completion)) {
        return false;
    }
    if (completion != TRB_COMPLETION_SUCCESS || slot_id == 0 || slot_id > g_max_slots) {
        return false;
    }

    uint64_t dev_ctx_phys = pmm_alloc_page();
    if (!dev_ctx_phys) return false;
    zero_page((void *)dev_ctx_phys);
    g_dcbaa[slot_id] = dev_ctx_phys;

    uint64_t ep0_ring_phys = pmm_alloc_page();
    if (!ep0_ring_phys) return false;
    zero_page((void *)ep0_ring_phys);

    xhci_trb_t *ep0_ring = (xhci_trb_t *)ep0_ring_phys;
    xhci_trb_t *ep0_link = &ep0_ring[COMMAND_RING_TRB_COUNT - 1];
    ep0_link->parameter = ep0_ring_phys;
    ep0_link->status = 0;
    ep0_link->control = (TRB_TYPE_LINK << 10) | (1u << 1) | 1u;

    uint64_t input_ctx_phys = pmm_alloc_page();
    if (!input_ctx_phys) return false;
    zero_page((void *)input_ctx_phys);

    uint8_t *ictx = (uint8_t *)input_ctx_phys;
    uint32_t *icc = (uint32_t *)(ictx + 0);
    uint32_t *slot_ctx = (uint32_t *)(ictx + g_context_size);
    uint32_t *ep0_ctx = (uint32_t *)(ictx + 2u * g_context_size);

    icc[1] = (1u << 0) | (1u << 1);

    uint32_t root_hub_port = (uint32_t)port_index + 1u;
    slot_ctx[0] = (speed << 20) | (1u << 27);
    slot_ctx[1] = (root_hub_port << 16);

    uint16_t max_packet = ep0_max_packet_size_for_speed(speed);
    ep0_ctx[1] = (4u << 3) | ((uint32_t)max_packet << 16);
    uint64_t ep0_ring_ptr_with_dcs = ep0_ring_phys | 0x1u;
    ep0_ctx[2] = (uint32_t)(ep0_ring_ptr_with_dcs & 0xFFFFFFFFu);
    ep0_ctx[3] = (uint32_t)(ep0_ring_ptr_with_dcs >> 32);
    ep0_ctx[4] = 8u;

    uint32_t addr_dev_control = (TRB_TYPE_ADDRESS_DEVICE << 10) | ((uint32_t)slot_id << 24);
    cmd_phys = cmd_ring_push(input_ctx_phys, 0, addr_dev_control);
    ring_doorbell(0, 0);

    if (!poll_command_completion(cmd_phys, &slot_id, &completion)) {
        return false;
    }
    if (completion != TRB_COMPLETION_SUCCESS) {
        return false;
    }

    g_port_slot[port_index] = (uint8_t)slot_id;
    g_devices[slot_id].ep0_ring = ep0_ring;
    g_devices[slot_id].ep0_enqueue_index = 0;
    g_devices[slot_id].ep0_cycle = 1;
    g_devices[slot_id].descriptor_valid = false;
    g_devices[slot_id].error = "GET_DESCRIPTOR bekleniyor";
    if (!ep0_get_device_descriptor((uint8_t)slot_id)) {
        return false;
    }
    return true;
}

#define PORT_POLL_INTERVAL_TICKS 20
static uint64_t g_last_poll_tick = 0;

void xhci_poll_ports(void) {
    if (!g_initialized) return;

    uint64_t now = pit_get_ticks();
    if (now - g_last_poll_tick < PORT_POLL_INTERVAL_TICKS) {
        return;
    }
    g_last_poll_tick = now;

    for (uint8_t p = 0; p < g_max_ports; p++) {
        uint32_t val = xhci_read_portsc(p);
        uint8_t ccs = (val & PORTSC_CCS) ? 1 : 0;

        if (ccs == g_port_last_ccs[p]) {
            if (g_port_stable_count[p] < 255) g_port_stable_count[p]++;
        } else {
            g_port_last_ccs[p] = ccs;
            g_port_stable_count[p] = 1;
        }

        if (g_port_stable_count[p] == PORT_DEBOUNCE_STABLE_COUNT) {
            if (ccs == 1 && g_port_slot[p] == 0) {
                enumerate_new_device(p);
            } else if (ccs == 0) {
                g_port_slot[p] = 0;
            }
        }
    }
}

uint8_t xhci_get_port_slot(uint8_t port_index) {
    if (port_index >= XHCI_MAX_PORTS_CAP) return 0;
    return g_port_slot[port_index];
}

bool xhci_get_device_descriptor(uint8_t port_index, const uint8_t **out_descriptor) {
    if (!out_descriptor || port_index >= g_max_ports) return false;
    uint8_t slot_id = g_port_slot[port_index];
    if (slot_id == 0 || slot_id > XHCI_MAX_SLOTS_CAP || !g_devices[slot_id].descriptor_valid) {
        return false;
    }
    *out_descriptor = g_devices[slot_id].device_descriptor;
    return true;
}

const char *xhci_get_port_error(uint8_t port_index) {
    if (port_index >= g_max_ports) return "Gecersiz xHCI portu";
    uint8_t slot_id = g_port_slot[port_index];
    if (slot_id == 0 || slot_id > XHCI_MAX_SLOTS_CAP) return "Aygit henuz adreslenmedi";
    return g_devices[slot_id].error;
}