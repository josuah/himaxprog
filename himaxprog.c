#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include "ftd2xx.h"
#include "libft4222.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))

#if 0
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
#endif

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

	return 0;
}

static int hx_list(FILE *fp)
{
	FT_DEVICE_LIST_INFO_NODE *info = NULL;
	unsigned int num = 0;
	bool found = false;
	int err;

	err = FT_CreateDeviceInfoList(&num);
	if (err) {
		perror("FT_CreateDeviceInfoList");
		return err;
	}
	
	if (num == 0) {
		perror("No FTDI devices found");
		return ENODEV;
	}

	info = calloc(num, sizeof(FT_DEVICE_LIST_INFO_NODE));
	if (info == NULL) {
		perror("calloc");
		return ENOMEM;
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
				fprintf(fp, "\nDevice %u: '%s'\n", i, info[i].Description);
				hx_chip_version(stdout, info[i].LocId);
			}
			found = true;
		}
		 
		if (info[i].Type == FT_DEVICE_4222H_3) {
			/* In mode 3, the FT4222H presents a single interface. */
			fprintf(fp, "\nDevice %u: '%s'\n", i, info[i].Description);
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

#define HX_FLASH_READ_ID	0x9f
#define HX_FLASH_FACTORY_MODE	0x44

static int hx_flash_wake(FT_HANDLE ftdi)
{
	struct timespec ts = {0};
	uint8_t byte;
	uint16_t _;
	int err;

	ts.tv_nsec = 1000 * 1000;
	err = nanosleep(&ts, NULL);
	if (err) {
		perror("nanosleep");
		return err;
	}

	err = FT4222_SPIMaster_SingleRead(ftdi, &byte, 1, &_, false);
	if (err) {
		perror("FT4222_SPIMaster_SingleRead");
		return err;
	}

	ts.tv_nsec = 1000*1000;
	err = nanosleep(&ts, NULL);
	if (err) {
		perror("nanosleep");
		return err;
	}

	err = FT4222_SPIMaster_SingleRead(ftdi, &byte, 1, &_, true);
	if (err) {
		perror("FT4222_SPIMaster_SingleRead");
		return err;
	}

	ts.tv_nsec = 1000*1000;
	err = nanosleep(&ts, NULL);
	if (err) {
		perror("nanosleep");
		return err;
	}

	return 0;
}

static int hx_flash_get_id(FT_HANDLE ftdi, uint8_t *manufacturer_id, uint8_t *device_id)
{
	uint8_t cmd_buf[1] = {HX_FLASH_READ_ID};
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
	return 0;
}

static int hx_flash_get_factory_mode(FT_HANDLE ftdi, uint8_t *factory_mode)
{
	uint8_t cmd_buf[1] = {HX_FLASH_FACTORY_MODE};
	uint8_t res_buf[1];
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

	*factory_mode = res_buf[0];
	return 0;
}

static int cmd_flash_detect(FT_HANDLE ftdi, char **argv)
{
	uint8_t manufacturer_id = 0;
	uint8_t device_id = 0;
	uint8_t factory_mode = 0;
	int spi_num;
	int err;

	if (argv[0][0] < '0' || argv[0][0] > '3' || argv[0][1] != '\0') {
		fprintf(stderr, "flash detect needs a number between 0 and 3 as argument\n");
		return EINVAL;
	}
	spi_num = 1 << (argv[0][0] - '0');

	err = FT4222_SPIMaster_Init(ftdi, SPI_IO_SINGLE, CLK_DIV_4, CLK_IDLE_LOW, CLK_LEADING, spi_num);
	if (err) {
		perror("FT4222_SPIMaster_Init");
		return err;
	}

	err = FT4222_SPIMaster_SetCS(ftdi, CS_ACTIVE_LOW);
	if (err) {
		perror("FT4222_SPIMaster_SetCS");
		return err;
	}

	err = FT4222_SPIMaster_SetMode(ftdi, 0, 0);
	if (err) {
		perror("FT4222_SPIMaster_SetMode");
		return err;
	}

	err = hx_flash_wake(ftdi);
	if (err) {
		perror("hx_flash_wake");
		return err;
	}

	err = hx_flash_get_id(ftdi, &manufacturer_id, &device_id);
	if (err) {
		perror("hx_flash_get_id");
		return err;
	}

	printf("manufacturer_id = %u\n", manufacturer_id);
	printf("device_id = %u\n", device_id);

	err = hx_flash_get_factory_mode(ftdi, &factory_mode);
	if (err) {
		perror("hx_flash_get_factory_mode");
		return err;
	}

	printf("factory_mode = %u\n", factory_mode);

	return 0;
}

int cmd_gpio_read(FT_HANDLE ftdi, char **argv)
{
	GPIO_Dir directions[4] = {GPIO_INPUT, GPIO_INPUT, GPIO_INPUT, GPIO_INPUT};
	int err;

	(void)argv;

	err = FT4222_GPIO_Init(ftdi, directions);
	if (err) {
		perror("FT4222_GPIO_Init");
		return err;
	}

	printf("gpio:");
	for (size_t i = 0; i < 4; i++) {
		unsigned int val;

		err = FT4222_GPIO_Read(ftdi, i, &val);
		if (err) {
			perror("FT4222_GPIO_Read");
			return err;
		}
		printf(" %lu:%s", i, val ? "high" : "low");
	}	
	printf("\n");

	return 0;
}

int cmd_gpio_write(FT_HANDLE ftdi, char **argv)
{
	GPIO_Dir directions[4];
	int err;

	if (strspn(argv[0], "01z") != 4 && argv[0][4] != '\0') {
		fprintf(stderr, "expecting a string[4] made of '0', '1' or 'z'\n");
		return EINVAL;
	}

	directions[0] = argv[0][0] == 'z' ? GPIO_INPUT : GPIO_OUTPUT;
	directions[1] = argv[0][1] == 'z' ? GPIO_INPUT : GPIO_OUTPUT;
	directions[2] = argv[0][2] == 'z' ? GPIO_INPUT : GPIO_OUTPUT;
	directions[3] = argv[0][3] == 'z' ? GPIO_INPUT : GPIO_OUTPUT;

	err = FT4222_GPIO_Init(ftdi, directions);
	if (err) {
		perror("FT4222_GPIO_Init");
		return err;
	}

	for (size_t i = 0; i < 4; i++) {
		if (directions[i] == GPIO_INPUT) {
			continue;
		}

		err = FT4222_GPIO_Write(ftdi, i, argv[0][i] - '0');
		if (err) {
			perror("FT4222_GPIO_Write");
			return err;
		}
	}	

	/* Read back immediately */
	return cmd_gpio_read(ftdi, NULL);
}

int cmd_gpio_suspend(FT_HANDLE ftdi, char **argv)
{
	int err;

	if (strspn(argv[0], "01") != 4 && argv[0][1] != '\0') {
		fprintf(stderr, "expecting a string[1] made of '0' or '1'\n");
		return EINVAL;
	}

	err = FT4222_SetSuspendOut(ftdi, argv[0][0] == '0' ? false : true);
	if (err) {
		perror("FT4222_SetSuspendOut");
		return err;
	}

	/* Read back immediately */
	return cmd_gpio_read(ftdi, NULL);
}

int cmd_gpio_wakeup(FT_HANDLE ftdi, char **argv)
{
	int err;

	if (strspn(argv[0], "01") != 4 && argv[0][1] != '\0') {
		fprintf(stderr, "expecting a string[1] made of '0' or '1'\n");
		return EINVAL;
	}

	err = FT4222_SetWakeUpInterrupt(ftdi, argv[0][0] == '0' ? false : true);
	if (err) {
		perror("FT4222_SetWakeUpInterrupt");
		return err;
	}

	/* Read back immediately */
	return cmd_gpio_read(ftdi, NULL);
}

int cmd_reset(FT_HANDLE ftdi, char **argv)
{
	int err;

	(void)argv;

	err = FT4222_ChipReset(ftdi);
	if (err) {
		perror("FT4222_ChipReset");
		return err;
	}

	return 0;
}

int hx_run_command(int (*fn)(FT_HANDLE, char **), char **argv)
{
	FT_HANDLE ftdi;
	int err;

	err = FT_Open(0, &ftdi);
	if (err) {
		perror("FT_Open");
		return err;
	}

	err = fn(ftdi, argv);

	FT4222_UnInitialize(ftdi);
	FT_Close(ftdi);
	return err;
}

const struct hx_cmd {
	char *argv[5];
	size_t argc;
	int (*fn)(FT_HANDLE, char **);
	char *help;
} cmds[] = {
	{{"flash", "detect"}, 1, cmd_flash_detect,
	 "scan the presence of a flash chip on each SPI bus"},
	{{"spi"}, 1, cmd_spi,
	 "Write bytes over SPI, or read where 'rr' is encountered"},
	{{"gpio", "read"}, 0, cmd_gpio_read,
	 "Read from all 4 GPIO pins"},
	{{"gpio", "write"}, 1, cmd_gpio_write,
	 "Write to selected GPIO pins"},
	{{"gpio", "suspend"}, 1, cmd_gpio_suspend,
	 "Write to the 'suspend' GPIO pin"},
	{{"gpio", "wakeup"}, 1, cmd_gpio_wakeup,
	 "Write to the 'wakeup' GPIO pin"},
	{{"reset"}, 0, cmd_reset,
	 "Reset the FTDI adapter chip"},
};

static int usage(char *argv0)
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

	return hx_list(stdout);
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
					return hx_run_command(cmd->fn, argv + ii);
				}
			} else if (argv[ii] == NULL || strcmp(argv[ii], cmd->argv[ii]) != 0) {
				goto next_cmd;
			}
		}
next_cmd:;
	}

	/* No command found: exit */
	return usage(argv0);
}
