%module chipset 
%{
#include "intel_chipset.h"
extern int is_sandybridge(unsigned short pciid);
extern int is_ivybridge(unsigned short pciid);
extern int is_valleyview(unsigned short pciid);
%}

%include "intel_chipset.h"
extern int is_sandybridge(unsigned short pciid);
extern int is_ivybridge(unsigned short pciid);
extern int is_valleyview(unsigned short pciid);
