#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <systemd/sd-bus.h>

#define DBUS_MAX_NAME_LEN 256

const char *objectmapper_service_name =  "org.openbmc.ObjectMapper";
const char *objectmapper_object_name  =  "/org/openbmc/ObjectMapper";
const char *objectmapper_intf_name    =  "org.openbmc.ObjectMapper";

static char *buffer;
static size_t buffer_sz;
static size_t buffer_capacity;
static const size_t buffer_init_capacity = 16 * 1024;

static char *obmc_console_read(void)
{
	return buffer;
}

static uint32_t obmc_console_get_size(void)
{
	return buffer_sz;
}

static uint32_t obmc_console_get_capacity(void)
{
	return buffer_capacity;
}

static int obmc_console_function_router(sd_bus_message *msg, void *user_data,
		sd_bus_error *ret_error)
{
	char *s;
	uint32_t u;

	/* get the operation. */
	const char *obmc_console_function = sd_bus_message_get_member(msg);
	if (!obmc_console_function) {
		fprintf(stderr, "NULL obmc_console function specificed\n");
		return sd_bus_reply_method_return(msg, "i", -1);
	}

	/* route the user action to appropriate handlers. */
	if (!strcmp(obmc_console_function, "read")) {
		s = obmc_console_read();
		return sd_bus_reply_method_return(msg, "s", s);
	}
	if (!strcmp(obmc_console_function, "get_size")) {
		u = obmc_console_get_size();
		return sd_bus_reply_method_return(msg, "u", u);
	}
	if (!strcmp(obmc_console_function, "get_capacity")) {
		u = obmc_console_get_capacity();
		return sd_bus_reply_method_return(msg, "u", u);
	}

	return sd_bus_reply_method_return(msg, "i", -1);
}

static const sd_bus_vtable obmc_console_vtable[] =
{
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("read", "", "s", &obmc_console_function_router,
			SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("get_size", "", "u", &obmc_console_function_router,
			SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("get_capacity", "", "u", &obmc_console_function_router,
			SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END,
};

static void *socket_th_function(void *args)
{
	static const char console_socket_path[] = "\0obmc-console";
	static const size_t console_socket_path_len = 
		sizeof(console_socket_path) - 1;
	struct sockaddr_un addr;
	int fd;
	char c;
       
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		perror("Failed to create socket");
		goto out;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	memcpy(&addr.sun_path, &console_socket_path, console_socket_path_len);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr))) {
		perror("Failed to connect to console server");
		goto close_sock;
	}

	buffer = malloc(buffer_init_capacity);
	if (!buffer) {
		fprintf(stderr, "Failed to allocate memory\n");
		goto close_sock;
	}
	buffer[0] = '\0';
	buffer_sz = 0;
	buffer_capacity = buffer_init_capacity;

	for (;;) {
		if (read(fd, &c, 1) < 1)
			break;
		if (buffer_sz + 1 == buffer_capacity) {
			/* if there is not enought space for new data */
			fprintf(stderr, "Buffer is full\n");
			goto close_sock;
		}
		buffer[buffer_sz] = c;
		buffer_sz++;
	}

	fprintf(stderr, "exit socket thread\n");
 close_sock:
	close(fd);
 out:
	return NULL;
}

int main(int argc, char **argv)
{
	sd_bus *bus;
	sd_bus_slot *obmc_console_slot = NULL;
	const char *obmc_console_object = "/org/openbmc/log/obmcConsole";
	const char *obmc_console_iface = "org.openbmc.log.obmcConsole";
	pthread_t socket_thread;
	int rc;

	rc = sd_bus_open_system(&bus);
	if (rc < 0) {
		fprintf(stderr,"Error opening system bus: %s\n",
			strerror(-rc));
		return rc;
	}

	rc = sd_bus_add_object_vtable(bus, &obmc_console_slot, 
			obmc_console_object, obmc_console_iface,
			obmc_console_vtable, NULL);
	if (rc < 0) {
		fprintf(stderr, "Failed to add object to dbus: %s\n",
			strerror(-rc));
		return rc;
	}

	rc = sd_bus_request_name(bus, obmc_console_iface, 0);
	if (rc < 0) {
		fprintf(stderr, "Failed to acquire service name: %s\n",
			strerror(-rc));
		return rc;
	}

	rc = pthread_create(&socket_thread, NULL, socket_th_function, NULL);
	if (rc < 0) {
		fprintf(stderr, "Failed to create thread: %s\n",
			strerror(rc));
		rc = 1;
		goto sdbus_free;
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

	rc = 0;
 sdbus_free:
	sd_bus_slot_unref(obmc_console_slot);
	sd_bus_unref(bus);

	return rc;
}

