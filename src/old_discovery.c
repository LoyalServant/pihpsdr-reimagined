/* Copyright (C)
 * 2015 - John Melton, G0ORX/N6LYT
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#ifdef _WIN32
#include <WinSock2.h>
#include <windows.h>
#include <WS2tcpip.h>
#include <iphlpapi.h>
#else
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if_arp.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <sys/select.h>
#endif
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "discovered.h"
#include "discovery.h"
#include "old_discovery.h"
#include "stemlab_discovery.h"
#include "message.h"
#include "mystring.h"

static char interface_name[64];
static struct sockaddr_in interface_addr = {0};
static struct sockaddr_in interface_netmask = {0};

#define DISCOVERY_PORT 1024
static int discovery_socket;

static GThread *discover_thread_id;
static gpointer discover_receive_thread(gpointer data);


#ifdef _WIN32
static void discover(int ifNum, u_long ifAddr, u_long ifNet_mask, int discflag)
#else
static void discover(struct ifaddrs *iface, int discflag)
#endif
{
    int rc;
    struct sockaddr_in *sa;
    struct sockaddr_in *mask;
    struct sockaddr_in to_addr = {0};
    struct sockaddr_in addr_in, mask_in;
    int flags;
    struct timeval tv;
    int optval;
    socklen_t optlen;
    fd_set fds;
    unsigned char buffer[1032];
    int i, len;
    char addr_str[INET_ADDRSTRLEN];
    char netmask_str[INET_ADDRSTRLEN];

    t_print("discover: Entering function\n");

#ifdef _WIN32
    struct in_addr addr;
    addr.s_addr = ifAddr;
    inet_ntop(AF_INET, &addr, addr_str, INET_ADDRSTRLEN);
    addr.s_addr = ifNet_mask;
    inet_ntop(AF_INET, &addr, netmask_str, INET_ADDRSTRLEN);

    t_print("discover: ifNum=%d, ifAddr=%s, ifNet_mask=%s, discflag=%d\n", ifNum, addr_str, netmask_str, discflag);
#else
    //t_print("discover: iface->ifa_name=%s, discflag=%d\n", iface->ifa_name, discflag);
#endif

    switch (discflag) {
        case 1:
#ifdef _WIN32
            // Windows interface handling
            addr_in.sin_family = AF_INET;
            addr_in.sin_addr.s_addr = ifAddr;
            addr_in.sin_port = 0;

            mask_in.sin_family = AF_INET;
            mask_in.sin_addr.s_addr = ifNet_mask;
            mask_in.sin_port = 0;

            sa = &addr_in;
            mask = &mask_in;

            STRLCPY(interface_name, "interface", sizeof(interface_name)); // FIXME
#else
            // unix interface handling
            STRLCPY(interface_name, iface->ifa_name, sizeof(interface_name));
            sa = (struct sockaddr_in *)iface->ifa_addr;
            mask = (struct sockaddr_in *)iface->ifa_netmask;
#endif
            t_print("discover: looking for HPSDR devices on %s\n", interface_name);
            discovery_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (discovery_socket < 0) {
                t_perror("discover: create socket failed for discovery_socket:");
                return;
            }

            interface_netmask.sin_addr.s_addr = mask->sin_addr.s_addr;
            interface_addr.sin_family = AF_INET;
            interface_addr.sin_addr.s_addr = sa->sin_addr.s_addr;
            interface_addr.sin_port = htons(0);

            t_print("Binding to:\n");
            t_print("Family: %d\n", interface_addr.sin_family);
            t_print("Address: %s\n", inet_ntoa(interface_addr.sin_addr));
            t_print("Port: %d\n", ntohs(interface_addr.sin_port));

            if (bind(discovery_socket, (struct sockaddr *)&interface_addr, sizeof(interface_addr)) < 0) {
                t_perror("discover: bind socket failed for discovery_socket:");
#ifdef _WIN32
                closesocket(discovery_socket);
#else                
                close(discovery_socket);
#endif                
                return;
            }

            t_print("discover: bound to %s\n", interface_name);
            int on = 1;
            rc = setsockopt(discovery_socket, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof(on));
            if (rc != 0) {
                t_print("discover: cannot set SO_BROADCAST: rc=%d\n", rc);
#ifdef _WIN32
                closesocket(discovery_socket);
#else                
                close(discovery_socket);
#endif
                return;
            }

            to_addr.sin_family = AF_INET;
            to_addr.sin_port = htons(DISCOVERY_PORT);
            to_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
            break;

        case 2:
            t_print("discover: case 2 - Unicast discovery\n");

            interface_addr.sin_family = AF_INET;
            interface_addr.sin_addr.s_addr = INADDR_ANY;
            memset(&to_addr, 0, sizeof(to_addr));
            to_addr.sin_family = AF_INET;
            to_addr.sin_port = htons(DISCOVERY_PORT);

#ifdef _WIN32
            if (InetPton(AF_INET, ipaddr_radio, &to_addr.sin_addr) <= 0) {
                t_perror("discover: invalid IP address format:");
                return;
            }
#else
            if (inet_pton(AF_INET, ipaddr_radio, &to_addr.sin_addr) <= 0) {
                t_perror("discover: invalid IP address format:");
                return;
            }
#endif

            t_print("discover: looking for HPSDR device with IP %s\n", ipaddr_radio);
            discovery_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (discovery_socket < 0) {
                t_perror("discover: create socket failed for discovery_socket:");
                return;
            }
            break;

        case 3:
            t_print("discover: case 3 - TCP discovery\n");

            memset(&to_addr, 0, sizeof(to_addr));
            to_addr.sin_family = AF_INET;
            to_addr.sin_port = htons(DISCOVERY_PORT);

#ifdef _WIN32
            if (InetPton(AF_INET, ipaddr_radio, &to_addr.sin_addr) <= 0) {
                t_perror("discover: invalid IP address format:");
                return;
            }
#else
            if (inet_pton(AF_INET, ipaddr_radio, &to_addr.sin_addr) <= 0) {
                t_perror("discover: invalid IP address format:");
                return;
            }
#endif

            t_print("Trying to detect via TCP with IP %s\n", ipaddr_radio);
            discovery_socket = socket(AF_INET, SOCK_STREAM, 0);
            if (discovery_socket < 0) {
                t_perror("discover: create socket failed for TCP discovery_socket\n");
                return;
            }

#ifdef _WIN32
            u_long mode = 1;
            if (ioctlsocket(discovery_socket, FIONBIO, &mode) != 0) {
                t_perror("discover: ioctlsocket() failed for TCP discovery_socket:");
                closesocket(discovery_socket);
                return;
            }
#else
            flags = fcntl(discovery_socket, F_GETFL, 0);
            if (flags < 0) {
                t_perror("discover: fcntl(F_GETFL) failed:");
                close(discovery_socket);
                return;
            }
            if (fcntl(discovery_socket, F_SETFL, flags | O_NONBLOCK) < 0) {
                t_perror("discover: fcntl(F_SETFL) failed:");
                close(discovery_socket);
                return;
            }
#endif

            rc = connect(discovery_socket, (const struct sockaddr *)&to_addr, sizeof(to_addr));

#ifdef _WIN32
            if (rc == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
                t_perror("discover: connect() failed for TCP discovery_socket:");
                closesocket(discovery_socket);
                return;
            }
#else
            if ((errno != EINPROGRESS) && (rc < 0)) {
                t_perror("discover: connect() failed for TCP discovery_socket:");
                close(discovery_socket);
                return;
            }
#endif

            tv.tv_sec = 3;
            tv.tv_usec = 0;
            FD_ZERO(&fds);
            FD_SET(discovery_socket, &fds);
            rc = select(discovery_socket + 1, NULL, &fds, NULL, &tv);
            if (rc < 0) {
                t_perror("discover: select() failed on TCP discovery_socket:");
#ifdef _WIN32
                closesocket(discovery_socket);
#else                
                close(discovery_socket);
#endif
                return;
            }

            if (rc == 0) {
                t_print("discover: select() timed out on TCP discovery socket\n");
#ifdef _WIN32
                closesocket(discovery_socket);
#else                
                close(discovery_socket);
#endif
                return;
            }

            optlen = sizeof(int);
            rc = getsockopt(discovery_socket, SOL_SOCKET, SO_ERROR, (char *)&optval, &optlen);
            if (rc < 0) {
                t_perror("discover: getsockopt() failed on TCP discovery_socket:");
#ifdef _WIN32
                closesocket(discovery_socket);
#else                
                close(discovery_socket);
#endif
                return;
            }

            if (optval != 0) {
                t_print("discover: connect() on TCP socket did not succeed\n");
#ifdef _WIN32
                closesocket(discovery_socket);
#else                
                close(discovery_socket);
#endif
                return;
            }

#ifdef _WIN32
            mode = 0;
            if (ioctlsocket(discovery_socket, FIONBIO, &mode) != 0) {
                t_perror("discover: ioctlsocket() failed to set socket back to blocking mode:");
                closesocket(discovery_socket);
                return;
            }
#else
            if (fcntl(discovery_socket, F_SETFL, flags & ~O_NONBLOCK) < 0) {
                t_perror("discover: fcntl(F_SETFL) failed to set socket back to blocking mode:");
                close(discovery_socket);
                return;
            }
#endif
            break;

        default:
            t_print("discover: Invalid discflag\n");
            return;
            break;
    }

    optval = 1;
    setsockopt(discovery_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof(optval));
#ifdef SO_REUSEPORT
    setsockopt(discovery_socket, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
#endif
    rc = devices;
    discover_thread_id = g_thread_new("old discover receive", discover_receive_thread, NULL);

    switch (discflag) {
        case 1:
        case 2:
            len = 63; // send UDP packet
            break;
        case 3:
            len = 1032; // send TCP packet
            break;
    }

    buffer[0] = 0xEF;
    buffer[1] = 0xFE;
    buffer[2] = 0x02;

    for (i = 3; i < len; i++) {
        buffer[i] = 0x00;
    }

    if (sendto(discovery_socket, (const char *)buffer, len, 0, (struct sockaddr *)&to_addr, sizeof(to_addr)) < 0) {
        t_perror("discover: sendto socket failed for discovery_socket:\n");
#ifdef _WIN32
        closesocket(discovery_socket);
#else                
        close(discovery_socket);
#endif
        return;
    }

    g_thread_join(discover_thread_id);
#ifdef _WIN32
    closesocket(discovery_socket);
#else                
    close(discovery_socket);
#endif

    switch (discflag) {
        case 1:
#ifdef _WIN32
            t_print("discover: exiting discover for interface\n");
#else
            t_print("discover: exiting discover for %s\n", iface->ifa_name);
#endif
            break;
        case 2:
            t_print("discover: exiting HPSDR discover for IP %s\n", ipaddr_radio);
            if (devices == rc + 1) {
                memcpy((void *)&discovered[rc].info.network.address, (void *)&to_addr, sizeof(to_addr));
                discovered[rc].info.network.address_length = sizeof(to_addr);
                STRLCPY(discovered[rc].info.network.interface_name, "UDP", sizeof(discovered[rc].info.network.interface_name));
                discovered[rc].use_routing = 1;
            }
            break;
        case 3:
            t_print("discover: exiting TCP discover for IP %s\n", ipaddr_radio);
            if (devices == rc + 1) {
                memcpy((void *)&discovered[rc].info.network.address, (void *)&to_addr, sizeof(to_addr));
                discovered[rc].info.network.address_length = sizeof(to_addr);
                STRLCPY(discovered[rc].info.network.interface_name, "TCP", sizeof(discovered[rc].info.network.interface_name));
                discovered[rc].use_routing = 1;
                discovered[rc].use_tcp = 1;
            }
            break;
    }
}

static gpointer discover_receive_thread(gpointer data)
{
    struct sockaddr_in addr;
    socklen_t len;
    unsigned char buffer[2048];
    struct timeval tv;
    int i;
    t_print("discover_receive_thread\n");

    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(discovery_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));
    len = sizeof(addr);

    while (1) {
        int bytes_read = recvfrom(discovery_socket, (char *)buffer, sizeof(buffer), 0, (struct sockaddr *)&addr, &len);

        if (bytes_read < 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                t_print("discovery: bytes read %d\n", bytes_read);
                t_print("old_discovery: recvfrom socket failed for discover_receive_thread: %d\n", err);
                break;
            } else {
                t_print("old_discovery: recvfrom would block, continuing...\n");
            }
#else
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                t_print("discovery: bytes read %d\n", bytes_read);
                t_print("old_discovery: recvfrom socket failed for discover_receive_thread: %s\n", strerror(errno));
                break;
            } else {
                t_print("old_discovery: recvfrom would block, continuing...\n");
            }
#endif
            break;
        }

        if (bytes_read == 0) {
            t_print("discovery: no data read, exiting thread\n");
            break;
        }

        t_print("old_discovery: received %d bytes\n", bytes_read);

        if ((buffer[0] & 0xFF) == 0xEF && (buffer[1] & 0xFF) == 0xFE) {
            int status = buffer[2] & 0xFF;

            if (status == 2 || status == 3) {
                if (devices < MAX_DEVICES) {
                    discovered[devices].protocol = ORIGINAL_PROTOCOL;
                    discovered[devices].device = buffer[10] & 0xFF;
                    discovered[devices].software_version = buffer[9] & 0xFF;

                    switch (discovered[devices].device) {
                        case DEVICE_METIS:
                            STRLCPY(discovered[devices].name, "Metis", sizeof(discovered[devices].name));
                            discovered[devices].frequency_min = 0.0;
                            discovered[devices].frequency_max = 61440000.0;
                            break;
                        case DEVICE_HERMES:
                            STRLCPY(discovered[devices].name, "Hermes", sizeof(discovered[devices].name));
                            discovered[devices].frequency_min = 0.0;
                            discovered[devices].frequency_max = 61440000.0;
                            break;
                        case DEVICE_GRIFFIN:
                            STRLCPY(discovered[devices].name, "Griffin", sizeof(discovered[devices].name));
                            discovered[devices].frequency_min = 0.0;
                            discovered[devices].frequency_max = 61440000.0;
                            break;
                        case DEVICE_ANGELIA:
                            STRLCPY(discovered[devices].name, "Angelia", sizeof(discovered[devices].name));
                            discovered[devices].frequency_min = 0.0;
                            discovered[devices].frequency_max = 61440000.0;
                            break;
                        case DEVICE_ORION:
                            STRLCPY(discovered[devices].name, "Orion", sizeof(discovered[devices].name));
                            discovered[devices].frequency_min = 0.0;
                            discovered[devices].frequency_max = 61440000.0;
                            break;
                        case DEVICE_HERMES_LITE:
                            if (discovered[devices].software_version < 40) {
                                STRLCPY(discovered[devices].name, "HermesLite V1", sizeof(discovered[devices].name));
                            } else {
                                STRLCPY(discovered[devices].name, "HermesLite V2", sizeof(discovered[devices].name));
                                discovered[devices].device = DEVICE_HERMES_LITE2;
                                t_print("discovered HL2: Gateware Major Version=%d Minor Version=%d\n", buffer[9], buffer[15]);
                            }
                            discovered[devices].frequency_min = 0.0;
                            discovered[devices].frequency_max = 30720000.0;
                            break;
                        case DEVICE_ORION2:
                            STRLCPY(discovered[devices].name, "Orion2", sizeof(discovered[devices].name));
                            discovered[devices].frequency_min = 0.0;
                            discovered[devices].frequency_max = 61440000.0;
                            break;
                        case DEVICE_STEMLAB:
                            STRLCPY(discovered[devices].name, "STEMlab", sizeof(discovered[devices].name));
                            discovered[devices].frequency_min = 0.0;
                            discovered[devices].frequency_max = 61440000.0;
                            break;
                        case DEVICE_STEMLAB_Z20:
                            STRLCPY(discovered[devices].name, "STEMlab-Zync7020", sizeof(discovered[devices].name));
                            discovered[devices].frequency_min = 0.0;
                            discovered[devices].frequency_max = 61440000.0;
                            break;
                        default:
                            STRLCPY(discovered[devices].name, "Unknown", sizeof(discovered[devices].name));
                            discovered[devices].frequency_min = 0.0;
                            discovered[devices].frequency_max = 61440000.0;
                            break;
                    }

                    t_print("old_discovery: name=%s min=%0.3f MHz max=%0.3f Mhz\n", discovered[devices].name,
                            discovered[devices].frequency_min * 1E-6,
                            discovered[devices].frequency_max * 1E-6);

                    for (i = 0; i < 6; i++) {
                        discovered[devices].info.network.mac_address[i] = buffer[i + 3];
                    }

                    discovered[devices].status = status;
                    memcpy((void *)&discovered[devices].info.network.address, (void *)&addr, sizeof(addr));
                    discovered[devices].info.network.address_length = sizeof(addr);
                    memcpy((void *)&discovered[devices].info.network.interface_address, (void *)&interface_addr, sizeof(interface_addr));
                    memcpy((void *)&discovered[devices].info.network.interface_netmask, (void *)&interface_netmask,
                           sizeof(interface_netmask));
                    discovered[devices].info.network.interface_length = sizeof(interface_addr);
                    STRLCPY(discovered[devices].info.network.interface_name, interface_name,
                            sizeof(discovered[devices].info.network.interface_name));
                    discovered[devices].use_tcp = 0;
                    discovered[devices].use_routing = 0;
                    discovered[devices].supported_receivers = 2;
                    t_print("old_discovery: found device=%d software_version=%d status=%d address=%s (%02X:%02X:%02X:%02X:%02X:%02X) on %s min=%0.3f MHz max=%0.3f Mhz\n",
                            discovered[devices].device,
                            discovered[devices].software_version,
                            discovered[devices].status,
                            inet_ntoa(discovered[devices].info.network.address.sin_addr),
                            discovered[devices].info.network.mac_address[0],
                            discovered[devices].info.network.mac_address[1],
                            discovered[devices].info.network.mac_address[2],
                            discovered[devices].info.network.mac_address[3],
                            discovered[devices].info.network.mac_address[4],
                            discovered[devices].info.network.mac_address[5],
                            discovered[devices].info.network.interface_name,
                            discovered[devices].frequency_min * 1E-6,
                            discovered[devices].frequency_max * 1E-6);
                    devices++;
                }
            }
        }
    }

    t_print("discovery: exiting discover_receive_thread\n");
    g_thread_exit(NULL);
    return NULL;
}

void old_discovery()
{
  struct ifaddrs *addrs, *ifa;
  int i, is_local;
  t_print("old_discovery\n");

  //
  // In the second phase of the STEMlab (RedPitaya) discovery,
  // we know that it can be reached by a specific IP address
  // and need no discovery any more
  //
  if (!discover_only_stemlab)
  {
#ifdef _WIN32
    PMIB_IPADDRTABLE pIPAddrTable;
    pIPAddrTable = GetIPAddressTable();
    for (i = 0; i < (int)pIPAddrTable->dwNumEntries; i++)
    {
      g_print("\n\tSearch on Interface Index:\t%ld\n", pIPAddrTable->table[i].dwIndex);
      discover(pIPAddrTable->table[i].dwIndex, (u_long)pIPAddrTable->table[i].dwAddr, pIPAddrTable->table[i].dwMask, 1);
      //discover(pIPAddrTable->table[i].dwIndex, (u_long)pIPAddrTable->table[i].dwAddr, pIPAddrTable->table[i].dwMask, 2);
      //discover(pIPAddrTable->table[i].dwIndex, (u_long)pIPAddrTable->table[i].dwAddr, pIPAddrTable->table[i].dwMask, 3);
    }
    free(pIPAddrTable);
#else
    getifaddrs(&addrs);
    ifa = addrs;

    while (ifa)
    {
      g_main_context_iteration(NULL, 0);

      //
      // Sometimes there are many (virtual) interfaces, and some
      // of them are very unlikely to offer a radio connection.
      // These are skipped.
      //
      if (ifa->ifa_addr)
      {
        if (
            ifa->ifa_addr->sa_family == AF_INET && (ifa->ifa_flags & IFF_UP) == IFF_UP && (ifa->ifa_flags & IFF_RUNNING) == IFF_RUNNING && (ifa->ifa_flags & IFF_LOOPBACK) != IFF_LOOPBACK && strncmp("veth", ifa->ifa_name, 4) && strncmp("dock", ifa->ifa_name, 4) && strncmp("hass", ifa->ifa_name, 4))
        {
          discover(ifa, 1); // send UDP broadcast packet to interface
        }
      }

      ifa = ifa->ifa_next;
    }

    freeifaddrs(addrs);
#endif
  }

  //
  // If an IP address has already been "discovered" via a
  // METIS broadcast package, it makes no sense to re-discover
  // it via a routed UDP packet.
  //
  is_local = 0;

  for (i = 0; i < devices; i++)
  {
    if (!strncmp(inet_ntoa(discovered[i].info.network.address.sin_addr), ipaddr_radio, 20) && discovered[i].protocol == ORIGINAL_PROTOCOL)
    {
      is_local = 1;
    }
  }
#ifdef _WIN32
  if (!is_local)
  { /*shhhh*/
  }
#else
  if (!is_local)
  {
    discover(NULL, 2);
  }
  discover(NULL, 3);
#endif

  t_print("discovery found %d devices\n", devices);

  for (i = 0; i < devices; i++)
  {
    t_print("discovery: found device=%d software_version=%d status=%d address=%s (%02X:%02X:%02X:%02X:%02X:%02X) on %s\n",
            discovered[i].device,
            discovered[i].software_version,
            discovered[i].status,
            inet_ntoa(discovered[i].info.network.address.sin_addr),
            discovered[i].info.network.mac_address[0],
            discovered[i].info.network.mac_address[1],
            discovered[i].info.network.mac_address[2],
            discovered[i].info.network.mac_address[3],
            discovered[i].info.network.mac_address[4],
            discovered[i].info.network.mac_address[5],
            discovered[i].info.network.interface_name);
  }
}
