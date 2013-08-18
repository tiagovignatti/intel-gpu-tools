struct gpu_freq {
	int min, max;
	int rpn, rp1, rp0;
	int request;
	int current;
};

int gpu_freq_init(struct gpu_freq *gf);
int gpu_freq_update(struct gpu_freq *gf);
