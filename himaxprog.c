#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include "ftd2xx.h"
#include "libft4222.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))
#define HX_FLASH_READ_ID	0x9f
#define HX_FLASH_FACTORY_MODE	0x44

static void hx_hexdump(FILE *fp, uint8_t *buf, size_t buf_len, char *label)
{
	size_t i;

	printf("%s", label);
	for (i = 0; i < buf_len; i++) {
		fprintf(fp, " %02x", buf[i]);
		if ((i + 1) % 16 == 0) {
			fprintf(fp, "\n%s", label);
		}
	}
	if (i % 16 != 0) {
		fprintf(fp, "\n");
	}
}

static int hx_hex2bin(char *hex, uint8_t *buf)
{
	for (size_t ia = 0, ib = 0;;) {
		int b0, b1;

		if ((b0 = hex[ia++]) == '\0') {
			break;
		}

		if ((b1 = hex[ia++]) == '\0') {
			fprintf(stderr, "odd number of characters in '%s'\n", hex);
			return -EINVAL;
		}

		b0 = (b0 >= '0' && b0 <= '9') ? b0 - '0' :
		     (b0 >= 'a' && b0 <= 'f') ? b0 - 'a' + 10 :
		     (b0 >= 'A' && b0 <= 'F') ? b0 - 'A' + 10 :
		     -EINVAL;
		if (b0 < 0) {
			return b0;
		}

		b1 = (b1 >= '0' && b1 <= '9') ? b1 - '0' :
		     (b1 >= 'a' && b1 <= 'f') ? b1 - 'a' + 10 :
		     (b1 >= 'A' && b1 <= 'F') ? b1 - 'A' + 10 :
		     -EINVAL;
		if (b1 < 0) {
			return b1;
		}

		buf[ib++] = ((b0 & 0xf) << 4) | ((b1 & 0xf) << 0);
	}

	return 0;
}

static int hx_chip_version(FT_DEVICE_LIST_INFO_NODE *info, void *arg)
{
	FT_HANDLE ftdi = NULL;
	FT4222_Version v;
	FILE *fp = arg;
	int err;

	err = FT_OpenEx((void *)(uintptr_t)info->LocId, FT_OPEN_BY_LOCATION, &ftdi);
	if (err) {
		perror("FT_OpenEx");
		return err;
	}

	err = FT4222_GetVersion(ftdi, &v);
	if (err) {
		perror("FT4222_GetVersion");
		return err;
	}

	fprintf(fp, "\nDevice '%s'\n", info->Description);
	fprintf(fp, "  Chip version: %08X, LibFT4222 version: %08X\n",
		v.chipVersion, v.dllVersion);

	return 0;
}

static int hx_select_first(FT_DEVICE_LIST_INFO_NODE *info, void *arg)
{
	int *locid = arg;

	fprintf(stderr, "Device '%s'\n", info->Description);

	*locid = info->LocId;
	return 1;
}

static int hx_scan(int (*fn)(FT_DEVICE_LIST_INFO_NODE *, void *), void *arg)
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
		return -ENODEV;
	}

	info = calloc(num, sizeof(FT_DEVICE_LIST_INFO_NODE));
	if (info == NULL) {
		perror("calloc");
		return -ENOMEM;
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
				err = (*fn)(&info[i], arg);
				if (err) {
					return err;
				}
			}
			found = true;
		}

		if (info[i].Type == FT_DEVICE_4222H_3) {
			/* In mode 3, the FT4222H presents a single interface. */
			err = (*fn)(&info[i], arg);
			if (err) {
				return err;
			}

			found = true;
		}
	}

	if (!found) {
		fprintf(stderr, "No valid FT4222H detected.\n");
	}

exit:
	free(info);
	return err;
}

static int hx_flash_wake(FT_HANDLE ftdi)
{
	uint8_t byte;
	uint16_t _;
	int err;

	err = nanosleep(&(struct timespec){.tv_nsec = 1000 * 1000}, NULL);
	if (err) {
		perror("nanosleep");
		return err;
	}

	err = FT4222_SPIMaster_SingleRead(ftdi, &byte, 1, &_, false);
	if (err) {
		perror("FT4222_SPIMaster_SingleRead");
		return err;
	}

	err = nanosleep(&(struct timespec){.tv_nsec = 1000 * 1000}, NULL);
	if (err) {
		perror("nanosleep");
		return err;
	}

	err = FT4222_SPIMaster_SingleRead(ftdi, &byte, 1, &_, true);
	if (err) {
		perror("FT4222_SPIMaster_SingleRead");
		return err;
	}

	err = nanosleep(&(struct timespec){.tv_nsec = 1000 * 1000}, NULL);
	if (err) {
		perror("nanosleep");
		return err;
	}

	return 0;
}

static int hx_spi_init(FT_HANDLE ftdi, int spi_num)
{
	int err;

	printf("spi_num=%u\n", spi_num);

	err = FT4222_SPIMaster_Init(ftdi, SPI_IO_SINGLE, CLK_DIV_4, CLK_IDLE_LOW, CLK_LEADING, 1 << spi_num);
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
	char *s;
	uint8_t manufacturer_id = 0;
	uint8_t device_id = 0;
	uint8_t factory_mode = 0;
	int spi_num;
	int err;

	spi_num = strtoul(argv[0], &s, 10);
	if (*s != '\0' || spi_num > 3) {
		fprintf(stderr, "flash detect needs a number between 0 and 3 as argument\n");
		return -EINVAL;
	}

	err = hx_spi_init(ftdi, spi_num);
	if (err) {
		perror("hx_spi_init");
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

static int cmd_spi(FT_HANDLE ftdi, char **argv)
{
	uint8_t *buf;
	char *s;
	size_t write_len;
	size_t read_len;
	uint16_t _;
	int spi_num;
	int err;

	spi_num = strtoul(argv[0], &s, 10);
	if (*s != '\0' || spi_num > 3) {
		fprintf(stderr, "<arg1> needs to be a number between 0 and 3\n");
		return -EINVAL;
	}

	buf = (uint8_t *)argv[1];
	write_len = strlen(argv[1]) / 2;

	err = hx_hex2bin(argv[1], buf);
	if (err) {
		fprintf(stderr, "<arg2> is invalid hex string\n");
		return err;
	}

	read_len = strtoul(argv[2], &s, 10);
	if (*s != '\0' || read_len > INT16_MAX) {
		fprintf(stderr, "<arg3> is invalid number: '%s'\n", argv[2]);
		return -EINVAL;
	}

	err = hx_spi_init(ftdi, spi_num);
	if (err) {
		perror("hx_spi_init");
		return err;
	}

	err = FT4222_SPIMaster_SingleWrite(ftdi, buf, write_len, &_, false);
	if (err) {
		perror("FT4222_SPIMaster_SingleRead");
		return err;
	}

	hx_hexdump(stdout, buf, write_len, "w:");

	buf = malloc(read_len);
	if (buf == NULL) {
		perror("malloc");
		return -ENOMEM;
	}

	err = FT4222_SPIMaster_SingleRead(ftdi, buf, read_len, &_, false);
	if (err) {
		perror("FT4222_SPIMaster_SingleRead");
		goto end;
	}

	hx_hexdump(stdout, buf, read_len, "r:");
end:
	free(buf);
	return err;
}

static int cmd_i2c_scan(FT_HANDLE ftdi, char **argv)
{
	uint16_t _;
	uint8_t buf[1] = {0};
	int err;

	(void)argv;

	err = FT4222_I2CMaster_Init(ftdi, 1000/*kbps*/);
	if (err) {
		perror("FT4222_I2CMaster_Init");
		return err;
	}

	for (uint16_t addr = 0; addr < 0x80; addr++) {
		err = FT4222_I2CMaster_Write(ftdi, addr, buf, 0, &_);
		if (err) {
			printf(" --");
		} else {
			printf(" %02x", addr);
		}
		if ((addr + 1) % 16 == 0) {
			printf("\n");
		}
	}

	return 0;
}

static int cmd_gpio_read(FT_HANDLE ftdi, char **argv)
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
		printf(" %u:%s", (int)i, val ? "high" : "low");
	}	
	printf("\n");

	return 0;
}

static int cmd_gpio_write(FT_HANDLE ftdi, char **argv)
{
	GPIO_Dir direction[4];
	GPIO_Port port[4] = { GPIO_PORT0, GPIO_PORT1, GPIO_PORT2, GPIO_PORT3 };
	size_t n;
	int err;

	n = strspn(argv[0], "01z");
	if (n > 4 || argv[0][n] != '\0') {
		fprintf(stderr, "expecting a at most 4 characters among [01z]\n");
		return -EINVAL;
	}

	direction[0] = argv[0][0] == 'z' ? GPIO_INPUT : GPIO_OUTPUT;
	direction[1] = argv[0][1] == 'z' ? GPIO_INPUT : GPIO_OUTPUT;
	direction[2] = argv[0][2] == 'z' ? GPIO_INPUT : GPIO_OUTPUT;
	direction[3] = argv[0][3] == 'z' ? GPIO_INPUT : GPIO_OUTPUT;

	err = FT4222_GPIO_Init(ftdi, direction);
	if (err) {
		perror("FT4222_GPIO_Init");
		return err;
	}

	for (size_t i = 0; i < n; i++) {
		if (direction[i] == GPIO_INPUT) {
			continue;
		}

		fprintf(stderr, "setting pin %u to %u\n", (int)i, argv[0][i] - '0');

		err = FT4222_GPIO_Write(ftdi, port[i], argv[0][i] - '0');
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
		return -EINVAL;
	}

	err = FT4222_SetSuspendOut(ftdi, argv[0][0] == '0' ? false : true);
	if (err) {
		perror("FT4222_SetSuspendOut");
		return err;
	}

	/* Read back immediately */
	return cmd_gpio_read(ftdi, NULL);
}

static int cmd_gpio_wakeup(FT_HANDLE ftdi, char **argv)
{
	int err;

	if (strspn(argv[0], "01") != 4 && argv[0][1] != '\0') {
		fprintf(stderr, "expecting a string[1] made of '0' or '1'\n");
		return -EINVAL;
	}

	err = FT4222_SetWakeUpInterrupt(ftdi, argv[0][0] == '0' ? false : true);
	if (err) {
		perror("FT4222_SetWakeUpInterrupt");
		return err;
	}

	/* Read back immediately */
	return cmd_gpio_read(ftdi, NULL);
}

static int cmd_reset(FT_HANDLE ftdi, char **argv)
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

static int hx_run_command(int (*fn)(FT_HANDLE, char **), char **argv)
{
	FT_HANDLE ftdi;
	int locid;
	int err;

	err = hx_scan(hx_select_first, &locid);
	if (err < 0) {
		perror("hx_scan: scanning devices");
		return err;
	}

	err = FT_OpenEx((void *)(uintptr_t)locid, FT_OPEN_BY_LOCATION, &ftdi);
	if (err) {
		perror("FT_OpenEx");
		return err;
	}

	err = fn(ftdi, argv);

	FT4222_UnInitialize(ftdi);
	FT_Close(ftdi);
	return err;
}

static const struct hx_cmd {
	char *argv[5];
	size_t argc;
	int (*fn)(FT_HANDLE, char **);
	char *help;
} cmds[] = {
	{{"flash", "detect"}, 1, cmd_flash_detect,
	 "scan the presence of a flash chip on each SPI bus"},
	{{"spi"}, 3, cmd_spi,
	 "Write to SPI num <arg1> the hex data <arg2> then read <arg3> bytes"},
	{{"i2c", "scan"}, 0, cmd_i2c_scan,
	 "Perform an I2C scan on the I2C interface <arg1>"},
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
	fprintf(stderr, "Utility to access the FT4222 chip such as on the Himax WE2 board\n");
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
			fprintf(stderr, " <arg%u>", (int)(ii + 1));
		}
		fprintf(stderr, "\n    %s\n", cmd->help);
	}

	return hx_scan(hx_chip_version, stderr);
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
