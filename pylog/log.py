#!/usr/bin/env python

import collections
import threading
import socket
import gobject
import dbus
import dbus.service
import dbus.mainloop.glib
from obmc.dbuslib.bindings import DbusProperties, DbusObjectManager, get_dbus

DBUS_NAME = 'org.openbmc.log.ObmcConsole'
OBJ_NAME = '/org/openbmc/log/obmcConsole'

class ObmcConsoleObject(dbus.service.Object):
    def __init__(self, bus, name):
        dbus.service.Object.__init__(self, bus, name)
        self.__buffer = collections.deque(maxlen = 16)
        self.__obmc_socket_th = threading.Thread(target=self.obmc_socket_thread)
        self.__obmc_socket_th.start()
        #self.Set(DBUS_NAME, "current_state", "")

    @dbus.service.method(DBUS_NAME, in_signature='', out_signature='s')
    def read(self):
        print "read(): method call:"
        s = "\n".join(self.__buffer)
        print s
        return s
        #return self.Get(DBUS_NAME, "current_state")

    def obmc_socket_thread(self):
        console_socket_path = "obmc-console"
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	print "Connecting to", console_socket_path
        try:
            sock.connect(console_socket_path)
        except socket.error, msg:
            print "socket error:", msg
        try:
    	    while 1:
                line = sock.makefile().readline()
                self.__buffer.append(line)
                print line
        finally:
            sock.close()

if __name__ == '__main__':
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = get_dbus()
    obj = ObmcConsoleObject(bus, OBJ_NAME)
    mainloop = gobject.MainLoop()
    name = dbus.service.BusName(DBUS_NAME, bus)

    print "Running log manager"
    mainloop.run()

# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4
