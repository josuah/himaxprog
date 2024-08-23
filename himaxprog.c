#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include "ftd2xx.h"
#include "libft4222.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))

static void hx_hexdump(FILE *fp, uint8_t *buf, size_t buf_len)
{
	size_t i;

	for (i = 0; i < buf_len; i++) {
		fprintf(fp, " %02x", buf[i]);
		if ((i + 1) % 16 == 0) {
			fprintf(fp, "\n");
		}
	}
	if (i % 16 != 0) {
		fprintf(fp, "\n");
	}
}

static int hx_chip_version(FILE *fp, unsigned int location)
{
	FT_HANDLE ftdi = NULL;
	FT4222_Version v;
	int err;

	err = FT_OpenEx((void *)(uintptr_t)location, FT_OPEN_BY_LOCATION, &ftdi);
	if (err) {
		perror("FT_OpenEx");
		return err;
	}

	err = FT4222_GetVersion(ftdi, &v);
	if (err) {
		perror("FT4222_GetVersion");
		return err;
	}

	fprintf(fp, "  Chip version: %08X, LibFT4222 version: %08X\n",
		v.chipVersion, v.dllVersion);

	FT_Close(ftdi);
	return 0;
}

static int cmd_list(char **argv)
{
	FT_DEVICE_LIST_INFO_NODE *info = NULL;
	unsigned int num = 0;
	bool found = false;
	int err;

	(void)argv;

	err = FT_CreateDeviceInfoList(&num);
	if (err) {
		perror("FT_CreateDeviceInfoList");
		goto exit;
	}
	
	if (num == 0) {
		perror("No FTDI devices found");
		err = ENODEV;
		goto exit;
	}

	info = calloc(num, sizeof(FT_DEVICE_LIST_INFO_NODE));
	if (info == NULL) {
		perror("calloc");
		err = errno;
		goto exit;
	}

	err = FT_GetDeviceInfoList(info, &num);
	if (err) {
		perror("FT_GetDeviceInfoList");
		goto exit;
	}

	for (unsigned int i = 0; i < num; i++) {
		if (info[i].Type == FT_DEVICE_4222H_0 ||
		    info[i].Type == FT_DEVICE_4222H_1_2) {
			size_t n = strlen(info[i].Description);

			/* In mode 0, the FT4222H presents two interfaces: A and B.
			 * In modes 1 and 2, it presents four interfaces: A, B, C and D.
			 */
			if (info[i].Description[n - 1] == 'A') {
				/* Interface A may be configured as an I2C master. */
				printf("\nDevice %u: '%s'\n", i, info[i].Description);
				hx_chip_version(stdout, info[i].LocId);
			}
			found = true;
		}
		 
		if (info[i].Type == FT_DEVICE_4222H_3) {
			/* In mode 3, the FT4222H presents a single interface. */
			printf("\nDevice %u: '%s'\n", i, info[i].Description);
			hx_chip_version(stdout, info[i].LocId);
			found = true;
		}
	}

	if (!found) {
		printf("No FT4222H detected.\n");
	}

exit:
	free(info);
	return err;
}

#define HX_FLASH_RDID	0x9f

static int hx_flash_get_id(FT_HANDLE ftdi, uint8_t *manufacturer_id, uint8_t *device_id)
{
	uint8_t cmd_buf[1] = {HX_FLASH_RDID};
	uint8_t res_buf[2];
	uint16_t _;
	int err;

	err = FT4222_SPIMaster_SingleWrite(ftdi, cmd_buf, sizeof(cmd_buf), &_, false);
	if (err) {
		perror("FT4222_SPIMaster_SingleRead");
		return err;
	}

	err = FT4222_SPIMaster_SingleRead(ftdi, res_buf, sizeof(res_buf), &_, true);
	if (err) {
		perror("FT4222_SPIMaster_SingleRead");
		return err;
	}

	*manufacturer_id = res_buf[0];
	*device_id = res_buf[1];

	hx_hexdump(stdout, res_buf, sizeof(res_buf));
	return 0;
}

static int cmd_flash_detect(char **argv)
{
	FT_HANDLE ftdi = NULL;
	uint8_t manufacturer_id = 0;
	uint8_t device_id = 0;
	int spi_num;
	int err;

	if (argv[0][0] < '0' || argv[0][0] > '3' || argv[0][1] != '\0') {
		fprintf(stderr, "flash detect needs a number between 0 and 3 as argument\n");
		return EINVAL;
	}
	spi_num = 1 << (argv[0][0] - '0');

	err = FT_Open(0, &ftdi);
	if (err) {
		perror("FT_Open");
		goto end;
	}

	err = FT4222_SPIMaster_Init(ftdi, SPI_IO_SINGLE, CLK_DIV_4, CLK_IDLE_LOW, CLK_LEADING, spi_num);
	if (err) {
		perror("FT4222_SPIMaster_Init");
		goto end;
	}

	err = FT4222_SPIMaster_SetCS(ftdi, CS_ACTIVE_LOW);
	if (err) {
		perror("FT4222_SPIMaster_SetCS");
		goto end;
	}

	err = FT4222_SPIMaster_SetMode(ftdi, 0, 0);
	if (err) {
		perror("FT4222_SPIMaster_SetMode");
		goto end;
	}

	err = hx_flash_get_id(ftdi, &manufacturer_id, &device_id);
	if (err) {
		perror("hx_flash_get_id");
		goto end;
	}

	printf("manufacturer_id = %u\n", manufacturer_id);
	printf("device_id = %u\n", device_id);

	FT4222_UnInitialize(ftdi);

end:
	FT_Close(ftdi);
	return err;
}

const struct hx_cmd {
	char *argv[10];
	size_t argc;
	int (*fn)(char **);
	char *help;
} cmds[] = {
	{{"list"}, 0, cmd_list,
	 "show all supported FTDI devices attached"},
	{{"flash", "detect"}, 1, cmd_flash_detect,
	 "scan the presence of a flash chip on each SPI bus"},
};

static void usage(char *argv0)
{
	for (size_t i = 0; i < ARRAY_SIZE(cmds); i++) {
		const struct hx_cmd *cmd = &cmds[i];

		fprintf(stderr, "%s", argv0);
		for (size_t ii = 0; ii < ARRAY_SIZE(cmd->argv); ii++) {
			if (cmd->argv[ii] == NULL) {
				break;
			}
			fprintf(stderr, " %s", cmd->argv[ii]);
		}
		for (size_t ii = 0; ii < cmd->argc; ii++) {
			fprintf(stderr, " <arg%lu>", ii + 1);
		}
		fprintf(stderr, " - %s\n", cmd->help);
	}
}

int main(int argc, char **argv)
{
	char *argv0 = argv[0];

	(void)argc;

	argv++;

	/* Parse command arguments */
	for (size_t i = 0; i < ARRAY_SIZE(cmds); i++) {
		const struct hx_cmd *cmd = &cmds[i];

		for (size_t ii = 0; ii < sizeof(cmd->argv); ii++) {
			if (cmd->argv[ii] == NULL) {
				for (size_t iii = 0; iii < cmd->argc; iii++) {
					if (argv[ii + iii] == NULL) {
						goto next_cmd;
					}
				}
				if (cmd->argv[ii + cmd->argc] == NULL) {
					return cmd->fn(argv + ii);
				}
			} else if (argv[ii] == NULL || strcmp(argv[ii], cmd->argv[ii]) != 0) {
				goto next_cmd;
			}
		}
next_cmd:;
	}

	/* No command found: exit */
	usage(argv0);
	return 1;
}
