#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <systemd/sd-bus.h>

#define DBUS_MAX_NAME_LEN 256

const char *objectmapper_service_name =  "org.openbmc.ObjectMapper";
const char *objectmapper_object_name  =  "/org/openbmc/ObjectMapper";
const char *objectmapper_intf_name    =  "org.openbmc.ObjectMapper";

static char *uart_read(void)
{
	return "UART read\nanother line read from uart\n\n\n\nEOF"; 
}

/*
 * Router function for any FAN operations that come via dbus
 */
static int uart_function_router(sd_bus_message *msg, void *user_data,
		sd_bus_error *ret_error)
{
	char *s; /* string reply */
	int rc = -1;

	/* get the operation. */
	const char *uart_function = sd_bus_message_get_member(msg);
	if (!uart_function) {
		fprintf(stderr, "log: Null uart function specificed\n");
		return sd_bus_reply_method_return(msg, "i", rc);
	}

	/* route the user action to appropriate handlers. */
	if ((strcmp(uart_function, "read") == 0)) {
		s = uart_read();
		return sd_bus_reply_method_return(msg, "s", s);
	}

	return sd_bus_reply_method_return(msg, "i", rc);
}

/* Dbus Services offered by this FAN controller */
static const sd_bus_vtable uart_vtable[] =
{
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("read", "", "s", &uart_function_router,
			SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END,
};

int main(int argc, char **argv)
{
	sd_bus *bus;
	sd_bus_slot *uart_slot = NULL;
	const char *uart_object = "/org/openbmc/log/uart";
	const char *uart_iface = "org.openbmc.log.uart";
	int rc;

	rc = sd_bus_open_system(&bus);
	if (rc < 0) {
		fprintf(stderr,"log: Error opening system bus.\n");
		return rc;
	}

	rc = sd_bus_add_object_vtable(bus, &uart_slot, uart_object, uart_iface,
			uart_vtable, NULL);
	if (rc < 0) {
		fprintf(stderr, "log: Failed to add object to dbus: %s\n",
				strerror(-rc));
		return rc;
	}

	rc = sd_bus_request_name(bus, uart_iface, 0);
	if (rc < 0) {
		fprintf(stderr, "log: Failed to acquire service name: %s\n",
				strerror(-rc));
		return rc;
	}

	for (;;) {
		/* process */
		rc = sd_bus_process(bus, NULL);
		if (rc < 0) {
			fprintf(stderr, "log: Failed to process bus: %s\n",
					strerror(-rc));
			break;
		}
		if (rc > 0) {
			continue;
		}

		rc = sd_bus_wait(bus, (uint64_t)-1);
		if (rc < 0) {
			fprintf(stderr, "fanctl: Failed to wait on bus: %s\n",
				strerror(-rc));
			break;
		}
	}

	sd_bus_slot_unref(uart_slot);
	sd_bus_unref(bus);

	return EXIT_SUCCESS;
}
