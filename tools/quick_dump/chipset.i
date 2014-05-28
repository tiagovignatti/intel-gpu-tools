%module chipset
%include "stdint.i"
%{
#include <pciaccess.h>
#include <stdint.h>
#include "intel_chipset.h"
#include "intel_io.h"
extern int is_sandybridge(unsigned short pciid);
extern int is_ivybridge(unsigned short pciid);
extern int is_valleyview(unsigned short pciid);
extern int is_cherryview(unsigned short pciid);
extern int is_haswell(unsigned short pciid);
extern int is_broadwell(unsigned short pciid);
extern struct pci_device *intel_get_pci_device();
extern int intel_register_access_init(struct pci_device *pci_dev, int safe);
extern uint32_t intel_register_read(uint32_t reg);
extern void intel_register_write(uint32_t reg, uint32_t val);
extern void intel_register_access_fini();
extern int intel_register_access_needs_fakewake();
extern unsigned short pcidev_to_devid(struct pci_device *pci_dev);
extern uint32_t intel_dpio_reg_read(uint32_t reg, int phy);
extern uint32_t intel_flisdsi_reg_read(uint32_t reg);
%}

extern int is_sandybridge(unsigned short pciid);
extern int is_ivybridge(unsigned short pciid);
extern int is_valleyview(unsigned short pciid);
extern int is_cherryview(unsigned short pciid);
extern int is_haswell(unsigned short pciid);
extern int is_broadwell(unsigned short pciid);
extern struct pci_device *intel_get_pci_device();
extern int intel_register_access_init(struct pci_device *pci_dev, int safe);
extern uint32_t intel_register_read(uint32_t reg);
extern void intel_register_write(uint32_t reg, uint32_t val);
extern void intel_register_access_fini();
extern int intel_register_access_needs_fakewake();
extern unsigned short pcidev_to_devid(struct pci_device *pci_dev);
extern uint32_t intel_dpio_reg_read(uint32_t reg, int phy);
extern uint32_t intel_flisdsi_reg_read(uint32_t reg);
