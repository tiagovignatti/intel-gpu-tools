/*
 * Copyright © 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "intel_io.h"
#include "intel_chipset.h"

#include "intel_reg_spec.h"


#ifdef HAVE_SYS_IO_H
#include <sys/io.h>
#else

static inline int _not_supported(void)
{
       fprintf(stderr, "portio-vga not supported\n");
       exit(EXIT_FAILURE);
}
#define inb(port)              _not_supported()
#define outb(value, port)      _not_supported()
#define iopl(level)

#endif /* HAVE_SYS_IO_H */

struct config {
	struct pci_device *pci_dev;
	char *mmiofile;
	uint32_t devid;

	/* read: number of registers to read */
	uint32_t count;

	/* write: do a posting read */
	bool post;

	/* decode register for all platforms */
	bool all_platforms;

	/* spread out bits for convenience */
	bool binary;

	/* register spec */
	char *specfile;
	struct reg *regs;
	ssize_t regcount;

	int verbosity;
};

/* port desc must have been set */
static int set_reg_by_addr(struct config *config, struct reg *reg,
			   uint32_t addr)
{
	int i;

	reg->addr = addr;
	if (reg->name)
		free(reg->name);
	reg->name = NULL;

	for (i = 0; i < config->regcount; i++) {
		struct reg *r = &config->regs[i];

		if (reg->port_desc.port != r->port_desc.port)
			continue;

		/* ->mmio_offset should be 0 for non-MMIO ports. */
		if (addr + reg->mmio_offset == r->addr + r->mmio_offset) {
			/* Always output the "normalized" offset+addr. */
			reg->mmio_offset = r->mmio_offset;
			reg->addr = r->addr;

			reg->name = r->name ? strdup(r->name) : NULL;
			break;
		}
	}

	return 0;
}

/* port desc must have been set */
static int set_reg_by_name(struct config *config, struct reg *reg,
			   const char *name)
{
	int i;

	reg->name = strdup(name);
	reg->addr = 0;

	for (i = 0; i < config->regcount; i++) {
		struct reg *r = &config->regs[i];

		if (reg->port_desc.port != r->port_desc.port)
			continue;

		if (!r->name)
			continue;

		if (strcasecmp(name, r->name) == 0) {
			reg->addr = r->addr;

			/* Also get MMIO offset if not already specified. */
			if (!reg->mmio_offset && r->mmio_offset)
				reg->mmio_offset = r->mmio_offset;

			return 0;
		}
	}

	return -1;
}

static void to_binary(char *buf, size_t buflen, uint32_t val)
{
	int i;

	if (!buflen)
		return;

	*buf = '\0';

	/* XXX: This quick and dirty implementation makes eyes hurt. */
	for (i = 31; i >= 0; i--) {
		if (i % 8 == 0)
			snprintf(buf, buflen, " %2d", i);
		else
			snprintf(buf, buflen, "  ");
		buflen -= strlen(buf);
		buf += strlen(buf);
	}
	snprintf(buf, buflen, "\n");
	buflen -= strlen(buf);
	buf += strlen(buf);

	for (i = 31; i >= 0; i--) {
		snprintf(buf, buflen, " %s%d", i % 8 == 7 ? " " : "",
			 !!(val & (1 << i)));
		buflen -= strlen(buf);
		buf += strlen(buf);
	}
	snprintf(buf, buflen, "\n");
}

static void dump_decode(struct config *config, struct reg *reg, uint32_t val)
{
	char decode[1024];
	char tmp[1024];
	char bin[1024];

	if (config->binary)
		to_binary(bin, sizeof(bin), val);
	else
		*bin = '\0';

	intel_reg_spec_decode(tmp, sizeof(tmp), reg, val,
			      config->all_platforms ? 0 : config->devid);

	if (*tmp) {
		/* We have a decode result, and maybe binary decode. */
		if (config->all_platforms)
			snprintf(decode, sizeof(decode), "\n%s%s", tmp, bin);
		else
			snprintf(decode, sizeof(decode), " (%s)\n%s", tmp, bin);
	} else if (*bin) {
		/* No decode result, but binary decode. */
		snprintf(decode, sizeof(decode), "\n%s", bin);
	} else {
		/* No decode nor binary decode. */
		snprintf(decode, sizeof(decode), "\n");
	}

	if (reg->port_desc.port == PORT_MMIO) {
		/* Omit port name for MMIO, optionally include MMIO offset. */
		if (reg->mmio_offset)
			printf("%24s (0x%08x:0x%08x): 0x%08x%s",
			       reg->name ?: "",
			       reg->mmio_offset, reg->addr,
			       val, decode);
		else
			printf("%35s (0x%08x): 0x%08x%s",
			       reg->name ?: "",
			       reg->addr,
			       val, decode);
	} else {
		char name[100], addr[100];

		/* If no name, use addr as name for easier copy pasting. */
		if (reg->name)
			snprintf(name, sizeof(name), "%s:%s",
				 reg->port_desc.name, reg->name);
		else
			snprintf(name, sizeof(name), "%s:0x%08x",
				 reg->port_desc.name, reg->addr);

		/* Negative port numbers are not real sideband ports. */
		if (reg->port_desc.port > PORT_NONE)
			snprintf(addr, sizeof(addr), "0x%02x:0x%08x",
				 reg->port_desc.port, reg->addr);
		else
			snprintf(addr, sizeof(addr), "%s:0x%08x",
				 reg->port_desc.name, reg->addr);

		printf("%24s (%s): 0x%08x%s", name, addr, val, decode);
	}
}

static int read_register(struct config *config, struct reg *reg, uint32_t *valp)
{
	uint32_t val = 0;

	switch (reg->port_desc.port) {
	case PORT_MMIO:
		val = INREG(reg->mmio_offset + reg->addr);
		break;
	case PORT_PORTIO_VGA:
		iopl(3);
		val = inb(reg->addr);
		iopl(0);
		break;
	case PORT_MMIO_VGA:
		val = INREG8(reg->addr);
		break;
	case PORT_BUNIT:
	case PORT_PUNIT:
	case PORT_NC:
	case PORT_DPIO:
	case PORT_GPIO_NC:
	case PORT_CCK:
	case PORT_CCU:
	case PORT_DPIO2:
	case PORT_FLISDSI:
		if (!IS_VALLEYVIEW(config->devid) &&
		    !IS_CHERRYVIEW(config->devid)) {
			fprintf(stderr, "port %s only supported on vlv/chv\n",
				reg->port_desc.name);
			return -1;
		}
		val = intel_iosf_sb_read(reg->port_desc.port, reg->addr);
		break;
	default:
		fprintf(stderr, "port %d not supported\n", reg->port_desc.port);
		return -1;
	}

	if (valp)
		*valp = val;

	return 0;
}

static void dump_register(struct config *config, struct reg *reg)
{
	uint32_t val;

	if (read_register(config, reg, &val) == 0)
		dump_decode(config, reg, val);
}

static int write_register(struct config *config, struct reg *reg, uint32_t val)
{
	int ret = 0;

	if (config->verbosity > 0) {
		printf("Before:\n");
		dump_register(config, reg);
	}

	switch (reg->port_desc.port) {
	case PORT_MMIO:
		OUTREG(reg->mmio_offset + reg->addr, val);
		break;
	case PORT_PORTIO_VGA:
		if (val > 0xff) {
			fprintf(stderr, "value 0x%08x out of range for port %s\n",
				val, reg->port_desc.name);
			return -1;
		}
		iopl(3);
		outb(val, reg->addr);
		iopl(0);
		break;
	case PORT_MMIO_VGA:
		if (val > 0xff) {
			fprintf(stderr, "value 0x%08x out of range for port %s\n",
				val, reg->port_desc.name);
			return -1;
		}
		OUTREG8(reg->addr, val);
		break;
	case PORT_BUNIT:
	case PORT_PUNIT:
	case PORT_NC:
	case PORT_DPIO:
	case PORT_GPIO_NC:
	case PORT_CCK:
	case PORT_CCU:
	case PORT_DPIO2:
	case PORT_FLISDSI:
		if (!IS_VALLEYVIEW(config->devid) &&
		    !IS_CHERRYVIEW(config->devid)) {
			fprintf(stderr, "port %s only supported on vlv/chv\n",
				reg->port_desc.name);
			return -1;
		}
		intel_iosf_sb_write(reg->port_desc.port, reg->addr, val);
		break;
	default:
		fprintf(stderr, "port %d not supported\n", reg->port_desc.port);
		ret = -1;
	}

	if (config->verbosity > 0) {
		printf("After:\n");
		dump_register(config, reg);
	} else if (config->post) {
		read_register(config, reg, NULL);
	}

	return ret;
}

/* s has [(PORTNAME|PORTNUM|MMIO-OFFSET):](REGNAME|REGADDR) */
static int parse_reg(struct config *config, struct reg *reg, const char *s)
{
	unsigned long addr;
	char *endp;
	const char *p;
	int ret;

	memset(reg, 0, sizeof(*reg));

	p = strchr(s, ':');
	if (p == s) {
		ret = -1;
	} else if (p) {
		char *port_name = strndup(s, p - s);

		ret = parse_port_desc(reg, port_name);

		free(port_name);
		p++;
	} else {
		/*
		 * XXX: If port is not specified in input, see if the register
		 * matches by name, and initialize port desc based on that.
		 */
		ret = parse_port_desc(reg, NULL);
		p = s;
	}

	if (ret) {
		fprintf(stderr, "invalid port in '%s'\n", s);
		return ret;
	}

	addr = strtoul(p, &endp, 16);
	if (endp > p && *endp == 0) {
		/* It's a number. */
		ret = set_reg_by_addr(config, reg, addr);
	} else {
		/* Not a number, it's a name. */
		ret = set_reg_by_name(config, reg, p);
	}

	return ret;
}

/* XXX: add support for register ranges, maybe REGISTER..REGISTER */
static int intel_reg_read(struct config *config, int argc, char *argv[])
{
	int i, j;

	if (argc == 1) {
		fprintf(stderr, "read: no registers specified\n");
		return EXIT_FAILURE;
	}

	if (config->mmiofile)
		intel_mmio_use_dump_file(config->mmiofile);
	else
		intel_register_access_init(config->pci_dev, 0);

	for (i = 1; i < argc; i++) {
		struct reg reg;

		if (parse_reg(config, &reg, argv[i]))
			continue;

		for (j = 0; j < config->count; j++) {
			dump_register(config, &reg);
			/* Update addr and name. */
			set_reg_by_addr(config, &reg,
					reg.addr + reg.port_desc.stride);
		}
	}

	intel_register_access_fini();

	return EXIT_SUCCESS;
}

static int intel_reg_write(struct config *config, int argc, char *argv[])
{
	int i;

	if (argc == 1) {
		fprintf(stderr, "write: no registers specified\n");
		return EXIT_FAILURE;
	}

	intel_register_access_init(config->pci_dev, 0);

	for (i = 1; i < argc; i += 2) {
		struct reg reg;
		uint32_t val;
		char *endp;

		if (parse_reg(config, &reg, argv[i]))
			continue;

		if (i + 1 == argc) {
			fprintf(stderr, "write: no value\n");
			break;
		}

		val = strtoul(argv[i + 1], &endp, 16);
		if (endp == argv[i + 1] || *endp) {
			fprintf(stderr, "write: invalid value '%s'\n",
				argv[i + 1]);
			continue;
		}

		write_register(config, &reg, val);
	}

	intel_register_access_fini();

	return EXIT_SUCCESS;
}

static int intel_reg_dump(struct config *config, int argc, char *argv[])
{
	struct reg *reg;
	int i;

	if (config->mmiofile)
		intel_mmio_use_dump_file(config->mmiofile);
	else
		intel_register_access_init(config->pci_dev, 0);

	for (i = 0; i < config->regcount; i++) {
		reg = &config->regs[i];

		/* can't dump sideband with mmiofile */
		if (config->mmiofile && reg->port_desc.port != PORT_MMIO)
			continue;

		dump_register(config, &config->regs[i]);
	}

	intel_register_access_fini();

	return EXIT_FAILURE;
}

static int intel_reg_snapshot(struct config *config, int argc, char *argv[])
{
	int mmio_bar = IS_GEN2(config->devid) ? 1 : 0;

	if (config->mmiofile) {
		fprintf(stderr, "specifying --mmio=FILE is not compatible\n");
		return EXIT_FAILURE;
	}

	intel_mmio_use_pci_bar(config->pci_dev);

	/* XXX: error handling */
	if (write(1, igt_global_mmio, config->pci_dev->regions[mmio_bar].size) == -1)
		fprintf(stderr, "Error writing snapshot: %s", strerror(errno));

	if (config->verbosity > 0)
		printf("use this with --mmio=FILE --devid=0x%04X\n",
		       config->devid);

	return EXIT_SUCCESS;
}

/* XXX: add support for reading and re-decoding a previously done dump */
static int intel_reg_decode(struct config *config, int argc, char *argv[])
{
	int i;

	if (argc == 1) {
		fprintf(stderr, "decode: no registers specified\n");
		return EXIT_FAILURE;
	}

	for (i = 1; i < argc; i += 2) {
		struct reg reg;
		uint32_t val;
		char *endp;

		if (parse_reg(config, &reg, argv[i]))
			continue;

		if (i + 1 == argc) {
			fprintf(stderr, "decode: no value\n");
			break;
		}

		val = strtoul(argv[i + 1], &endp, 16);
		if (endp == argv[i + 1] || *endp) {
			fprintf(stderr, "decode: invalid value '%s'\n",
				argv[i + 1]);
			continue;
		}

		dump_decode(config, &reg, val);
	}

	return EXIT_SUCCESS;
}

static int intel_reg_list(struct config *config, int argc, char *argv[])
{
	int i;

	for (i = 0; i < config->regcount; i++) {
		printf("%s\n", config->regs[i].name);
	}

	return EXIT_SUCCESS;
}

static int intel_reg_help(struct config *config, int argc, char *argv[]);

struct command {
	const char *name;
	const char *description;
	const char *synopsis;
	int (*function)(struct config *config, int argc, char *argv[]);
};

static const struct command commands[] = {
	{
		.name = "read",
		.function = intel_reg_read,
		.synopsis = "[--count=N] REGISTER [...]",
		.description = "read and decode specified register(s)",
	},
	{
		.name = "write",
		.function = intel_reg_write,
		.synopsis = "[--post] REGISTER VALUE [REGISTER VALUE ...]",
		.description = "write value(s) to specified register(s)",
	},
	{
		.name = "dump",
		.function = intel_reg_dump,
		.description = "dump all known registers",
	},
	{
		.name = "decode",
		.function = intel_reg_decode,
		.synopsis = "REGISTER VALUE [REGISTER VALUE ...]",
		.description = "decode value(s) for specified register(s)",
	},
	{
		.name = "snapshot",
		.function = intel_reg_snapshot,
		.description = "create a snapshot of the MMIO bar to stdout",
	},
	{
		.name = "list",
		.function = intel_reg_list,
		.description = "list all known register names",
	},
	{
		.name = "help",
		.function = intel_reg_help,
		.description = "show this help",
	},
};

static int intel_reg_help(struct config *config, int argc, char *argv[])
{
	int i;

	printf("Intel graphics register multitool\n\n");
	printf("Usage: intel_reg [OPTION ...] COMMAND\n\n");
	printf("COMMAND is one of:\n");
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		printf("  %-14s%s\n", commands[i].name,
		       commands[i].synopsis ?: "");
		printf("  %-14s%s\n", "", commands[i].description);
	}

	printf("\n");
	printf("REGISTER is defined as:\n");
        printf("  [(PORTNAME|PORTNUM|MMIO-OFFSET):](REGNAME|REGADDR)\n");

	printf("\n");
	printf("PORTNAME is one of:\n");
	intel_reg_spec_print_ports();
	printf("\n");

	printf("\n");
	printf("OPTIONS common to most COMMANDS:\n");
	printf(" --spec=PATH    Read register spec from directory or file\n");
	printf(" --mmio=FILE    Use an MMIO snapshot\n");
	printf(" --devid=DEVID  Specify PCI device ID for --mmio=FILE\n");
	printf(" --all          Decode registers for all known platforms\n");
	printf(" --binary       Binary dump registers\n");
	printf(" --verbose      Increase verbosity\n");
	printf(" --quiet        Reduce verbosity\n");

	printf("\n");
	printf("Environment variables:\n");
	printf(" INTEL_REG_SPEC Read register spec from directory or file\n");

	return EXIT_SUCCESS;
}

/*
 * Get codename for a gen5+ platform to be used for finding register spec file.
 */
static const char *get_codename(uint32_t devid)
{
	if (IS_GEN5(devid))
		return "ironlake";
	else if (IS_GEN6(devid))
		return "sandybridge";
	else if (IS_IVYBRIDGE(devid))
		return "ivybridge";
	else if (IS_HASWELL(devid))
		return "haswell";
	else if (IS_BROADWELL(devid))
		return "broadwell";
	else if (IS_SKYLAKE(devid))
		return "skylake";
	else if (IS_KABYLAKE(devid))
		return "kabylake";
	else if (IS_CHERRYVIEW(devid))
		return "cherryview";
	else if (IS_VALLEYVIEW(devid))
		return "valleyview";

	return NULL;
}

/*
 * Get register definitions filename for devid in dir. Return 0 if found,
 * negative error code otherwise.
 */
static int get_reg_spec_file(char *buf, size_t buflen, const char *dir,
			     uint32_t devid)
{
	const char *codename;

	/* First, try file named after devid, e.g. "0412" for Haswell GT2. */
	snprintf(buf, buflen, "%s/%04x", dir, devid);
	if (!access(buf, F_OK))
		return 0;

	/*
	 * Second, for gen5+, try file named after codename, e.g. "haswell" for
         * Haswell.
	 */
	codename = get_codename(devid);
	if (codename) {
		snprintf(buf, buflen, "%s/%s", dir, codename);
		if (!access(buf, F_OK))
			return 0;
	}

	/*
	 * Third, try file named after gen, e.g. "gen7" for Haswell (which is
	 * technically 7.5 but this is how it works).
	 */
	snprintf(buf, buflen, "%s/gen%d", dir, intel_gen(devid));
	if (!access(buf, F_OK))
		return 0;

	return -ENOENT;
}

/*
 * Read register spec.
 */
static int read_reg_spec(struct config *config)
{
	char buf[PATH_MAX];
	const char *path;
	struct stat st;
	int r;

	path = config->specfile;
	if (!path)
		path = getenv("INTEL_REG_SPEC");

	if (!path)
		path = PKGDATADIR"/registers";

	r = stat(path, &st);
	if (r) {
		fprintf(stderr, "Warning: stat '%s' failed: %s. "
			"Using builtin register spec.\n",
			path, strerror(errno));
		goto builtin;
	}

	if (S_ISDIR(st.st_mode)) {
		r = get_reg_spec_file(buf, sizeof(buf), path, config->devid);
		if (r) {
			fprintf(stderr, "Warning: register spec not found in "
				"'%s'. Using builtin register spec.\n", path);
			goto builtin;
		}
		path = buf;
	}

	config->regcount = intel_reg_spec_file(&config->regs, path);
	if (config->regcount <= 0) {
		fprintf(stderr, "Warning: reading '%s' failed. "
			"Using builtin register spec.\n", path);
		goto builtin;
	}

	return config->regcount;

builtin:
	/* Fallback to builtin register spec. */
	config->regcount = intel_reg_spec_builtin(&config->regs, config->devid);

	return config->regcount;
}

enum opt {
	OPT_UNKNOWN = '?',
	OPT_END = -1,
	OPT_MMIO,
	OPT_DEVID,
	OPT_COUNT,
	OPT_POST,
	OPT_ALL,
	OPT_BINARY,
	OPT_SPEC,
	OPT_VERBOSE,
	OPT_QUIET,
	OPT_HELP,
};

int main(int argc, char *argv[])
{
	int ret, i, index;
	char *endp;
	enum opt opt;
	const struct command *command = NULL;
	struct config config = {
		.count = 1,
	};
	bool help = false;

	static struct option options[] = {
		/* global options */
		{ "spec",	required_argument,	NULL,	OPT_SPEC },
		{ "verbose",	no_argument,		NULL,	OPT_VERBOSE },
		{ "quiet",	no_argument,		NULL,	OPT_QUIET },
		{ "help",	no_argument,		NULL,	OPT_HELP },
		/* options specific to read and dump */
		{ "mmio",	required_argument,	NULL,	OPT_MMIO },
		{ "devid",	required_argument,	NULL,	OPT_DEVID },
		/* options specific to read */
		{ "count",	required_argument,	NULL,	OPT_COUNT },
		/* options specific to write */
		{ "post",	no_argument,		NULL,	OPT_POST },
		/* options specific to read, dump and decode */
		{ "all",	no_argument,		NULL,	OPT_ALL },
		{ "binary",	no_argument,		NULL,	OPT_BINARY },
		{ 0 }
	};

	for (opt = 0; opt != OPT_END; ) {
		opt = getopt_long(argc, argv, "", options, &index);

		switch (opt) {
		case OPT_MMIO:
			config.mmiofile = strdup(optarg);
			if (!config.mmiofile) {
				fprintf(stderr, "strdup: %s\n",
					strerror(errno));
				return EXIT_FAILURE;
			}
			break;
		case OPT_DEVID:
			config.devid = strtoul(optarg, &endp, 16);
			if (*endp) {
				fprintf(stderr, "invalid devid '%s'\n", optarg);
				return EXIT_FAILURE;
			}
			break;
		case OPT_COUNT:
			config.count = strtol(optarg, &endp, 10);
			if (*endp) {
				fprintf(stderr, "invalid count '%s'\n", optarg);
				return EXIT_FAILURE;
			}
			break;
		case OPT_POST:
			config.post = true;
			break;
		case OPT_SPEC:
			config.specfile = strdup(optarg);
			if (!config.specfile) {
				fprintf(stderr, "strdup: %s\n",
					strerror(errno));
				return EXIT_FAILURE;
			}
			break;
		case OPT_ALL:
			config.all_platforms = true;
			break;
		case OPT_BINARY:
			config.binary = true;
			break;
		case OPT_VERBOSE:
			config.verbosity++;
			break;
		case OPT_QUIET:
			config.verbosity--;
			break;
		case OPT_HELP:
			help = true;
			break;
		case OPT_END:
			break;
		case OPT_UNKNOWN:
			return EXIT_FAILURE;
		}
	}

	argc -= optind;
	argv += optind;

	if (help || (argc > 0 && strcmp(argv[0], "help") == 0))
		return intel_reg_help(&config, argc, argv);

	if (argc == 0) {
		fprintf(stderr, "Command missing. Try intel_reg help.\n");
		return EXIT_FAILURE;
	}

	if (config.mmiofile) {
		if (!config.devid) {
			fprintf(stderr, "--mmio requires --devid\n");
			return EXIT_FAILURE;
		}
	} else {
		/* XXX: devid without --mmio could be useful for decode. */
		if (config.devid) {
			fprintf(stderr, "--devid without --mmio\n");
			return EXIT_FAILURE;
		}
		config.pci_dev = intel_get_pci_device();
		config.devid = config.pci_dev->device_id;
	}

	if (read_reg_spec(&config) < 0) {
		return EXIT_FAILURE;
	}

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0) {
			command = &commands[i];
			break;
		}
	}

	if (!command) {
		fprintf(stderr, "'%s' is not an intel-reg command\n", argv[0]);
		return EXIT_FAILURE;
	}

	ret = command->function(&config, argc, argv);

	free(config.mmiofile);

	return ret;
}
