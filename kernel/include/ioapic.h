#ifndef LIXOS_IOAPIC_H
#define LIXOS_IOAPIC_H

#include <stdint.h>

void ioapic_init(uint64_t hhdm_offset, uint64_t ioapic_phys_base = 0xFEC00000);
void ioapic_set_irq(uint8_t pin, uint8_t vector, uint8_t dest_lapic_id, bool masked = false);
void ioapic_mask_irq(uint8_t pin);
void ioapic_unmask_irq(uint8_t pin);

#endif // LIXOS_IOAPIC_H
