struct gpu_freq {
	int rpn, rp1, rp0, max;
	int request;
	int current;
};

int gpu_freq_init(struct gpu_freq *gf);
int gpu_freq_update(struct gpu_freq *gf);
