struct igfx_info {
	int gen;
};

struct pci_device;

struct pci_device *igfx_get(void);
const struct igfx_info *igfx_get_info(struct pci_device *pci_dev);
void *igfx_get_mmio(struct pci_device *pci_dev);

static inline uint32_t
igfx_read(void *mmio, uint32_t reg)
{
	return *(volatile uint32_t *)((volatile char *)mmio + reg);
}
