#include "e3_connector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zmq.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <grp.h>

#include "common/utils/LOG/log.h"

#define CHUNK_SIZE 8192
#define UNUSED_SOCKET -2 // -1 is set for errors, thus we use -2 to indicate unused sockets

#define IPC_BASE_DIR "/tmp/dapps"
#define E3_IPC_SETUP_PATH IPC_BASE_DIR "/setup"
#define E3_IPC_SOCKET_PATH IPC_BASE_DIR "/e3_socket" // outbound
#define DAPP_IPC_SOCKET_PATH IPC_BASE_DIR "/dapp_socket" // inbound

// ZeroMQ Connector Functions
int zeromq_setup_initial_connection(E3Connector *self){
    self->setup_socket = zmq_socket(self->context, ZMQ_REP);

    int ret = zmq_bind(self->setup_socket, self->setup_endpoint);        
    if (strncmp(self->setup_endpoint, "ipc", 3) == 0) {
      // Set the permissions to add the setgid bit and group write (chmod g+ws)
      if (chmod(E3_IPC_SETUP_PATH, 0666) == -1) {
        LOG_E(E3AP, "Failed to chmod IPC inbound endpoint: %s\n", strerror(errno));
      } else {
        LOG_I(E3AP, "Permissions of %s set correctly\n", self->setup_endpoint);
      }
    }

    return ret;
}

int zeromq_recv_setup_request(E3Connector *self, void *buffer, size_t buffer_size) {
    // Use the provided buffer to receive the message as raw bytes
    return zmq_recv(self->setup_socket, buffer, buffer_size, 0);
}

int zeromq_send_response(E3Connector *self, const uint8_t *response, size_t response_size) {
    // Send raw bytes as response using the setup socket
    return zmq_send(self->setup_socket, response, response_size, 0);
}

int zeromq_setup_inbound_connection(E3Connector *self) {
    int conflate = 1;
    void *inbound_socket = zmq_socket(self->context, ZMQ_SUB);
    int ret = zmq_connect(inbound_socket, self->inbound_endpoint);
    zmq_setsockopt(inbound_socket, ZMQ_SUBSCRIBE, "", 0);
    zmq_setsockopt(inbound_socket, ZMQ_CONFLATE, &conflate, sizeof(conflate)); // Keep only the last message
    self->inbound_socket = inbound_socket;
    return ret;
}

int zeromq_receive(E3Connector *self, void *buffer, size_t buffer_size) {
    return zmq_recv(self->inbound_socket, buffer, buffer_size, 0);
}

int zeromq_setup_outbound_connection(E3Connector *self) {
    void *outbound_socket = zmq_socket(self->context, ZMQ_PUB);
    int ret = zmq_bind(outbound_socket, self->outbound_endpoint);

    if (strncmp(self->outbound_endpoint, "ipc", 3) == 0) {
      if (chmod(E3_IPC_SOCKET_PATH, 0666) == -1) {
        LOG_E(E3AP, "Failed to chmod IPC outbound endpoint: %s\n", strerror(errno));
      } else {
        LOG_I(E3AP, "Permissions of %s set correctly\n", self->outbound_endpoint);
      }
    }

    self->outbound_socket = outbound_socket;
    return ret;
}

int zeromq_send(E3Connector *self, const uint8_t *payload, size_t payload_size) {
    return zmq_send(self->outbound_socket, payload, payload_size, 0);
}

void zeromq_dispose(E3Connector *self)
{
  zmq_ctx_destroy(self->context);
  // Unlink the IPC socket files if needed
  if (strncmp(self->setup_endpoint, E3_IPC_SETUP_PATH, strlen(E3_IPC_SETUP_PATH)) == 0) {
    unlink(E3_IPC_SETUP_PATH);
  }
  if (strncmp(self->inbound_endpoint, E3_IPC_SOCKET_PATH, strlen(E3_IPC_SOCKET_PATH)) == 0) {
    unlink(E3_IPC_SOCKET_PATH);
  }
  if (strncmp(self->outbound_endpoint, DAPP_IPC_SOCKET_PATH, strlen(DAPP_IPC_SOCKET_PATH)) == 0) {
    unlink(DAPP_IPC_SOCKET_PATH);
  }

  remove(IPC_BASE_DIR);

  free(self);
}

// POSIX Connector Functions
int posix_setup_initial_connection(E3Connector *self){
  int sock;
  struct sockaddr_in addr_in;
  struct sockaddr_un addr_un;
  int ret;
  if (strncmp(self->setup_endpoint, "sctp", 4) == 0) {
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(9990);
    addr_in.sin_addr.s_addr = inet_addr("127.0.0.1");
    ret = bind(sock, (struct sockaddr *)&addr_in, sizeof(addr_in));
  } else if (strncmp(self->setup_endpoint, "tcp", 3) == 0) {
    sock = socket(AF_INET, SOCK_STREAM, 0);
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(9990);
    addr_in.sin_addr.s_addr = inet_addr("127.0.0.1");
    ret = bind(sock, (struct sockaddr *)&addr_in, sizeof(addr_in));
  } else { // Unix Domain Sockets for POSIX IPC
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    addr_un.sun_family = AF_UNIX;
    strncpy(addr_un.sun_path, E3_IPC_SETUP_PATH, sizeof(addr_un.sun_path) - 1);
    ret = bind(sock, (struct sockaddr *)&addr_un, sizeof(addr_un));
  }

  if (ret < 0) {
    LOG_E(E3AP, "Error in bind initial connection: %s\n", strerror(errno));
    return ret;
  }

  ret = listen(sock, 5);
  if (ret < 0) {
    LOG_E(E3AP, "Error in listen initial connection: %s\n", strerror(errno));
    return ret;
  }
  self->setup_socket = (void *)(intptr_t)sock;

  return ret;
}

int posix_recv_setup_request(E3Connector *self, void *buffer, size_t buffer_size) {
    self->setup_connection_socket = accept((intptr_t)self->setup_socket, NULL, NULL); // Store the client socket for sending response
    return recv(self->setup_connection_socket, buffer, buffer_size, 0);
}

int send_in_chunks(int sockfd, const uint8_t *buffer, size_t buffer_size)
{
    size_t total_sent = 0;
    int chunks = 0;

    LOG_D(E3AP, "Buffer_size %ld\n", buffer_size);

    // Send the buffer size first
    uint32_t network_order_size = htonl(buffer_size);
    ssize_t sent = send(sockfd, &network_order_size, sizeof(network_order_size), 0);
    if (sent != sizeof(network_order_size)) {
        LOG_E(E3AP, "Failed to send buffer size: %s\n", strerror(errno));
        return -1;
    }

    ssize_t sent_chunk;
    size_t chunk_total_sent = 0;
    
    // Send the buffer in chunks
    while (total_sent < buffer_size) {
        size_t bytes_to_send = buffer_size - total_sent > CHUNK_SIZE ? CHUNK_SIZE : buffer_size - total_sent;
        
        chunk_total_sent = 0;
        while (chunk_total_sent < bytes_to_send) {  // Ensure full chunk is sent
            sent_chunk = send(sockfd, buffer + total_sent + chunk_total_sent, bytes_to_send - chunk_total_sent, 0);
            if (sent_chunk == -1) {
                LOG_E(E3AP, "Failed to send data: %s\n", strerror(errno));
                return sent_chunk;
            }
            chunk_total_sent += sent_chunk;
        }
        
        total_sent += chunk_total_sent;
        chunks++;
    }

    LOG_D(E3AP, "Chunks sent %d\n", chunks);
    LOG_D(E3AP, "Total size sent %ld\n", total_sent);
    return 0;
}

int posix_send_response(E3Connector *self, const uint8_t *response, size_t response_size) {
    return send_in_chunks(self->setup_connection_socket, response, response_size);
}

int posix_setup_inbound_connection(E3Connector *self)
{
  int sock;
  struct sockaddr_in addr_in;
  struct sockaddr_un addr_un;
  int ret;
  if (strncmp(self->inbound_endpoint, "sctp", 4) == 0) {
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(9999);
    addr_in.sin_addr.s_addr = inet_addr("127.0.0.1");
    ret = bind(sock, (struct sockaddr *)&addr_in, sizeof(addr_in));
  } else if (strncmp(self->inbound_endpoint, "tcp", 3) == 0) {
    sock = socket(AF_INET, SOCK_STREAM, 0);
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(9999);
    addr_in.sin_addr.s_addr = inet_addr("127.0.0.1");
    ret = bind(sock, (struct sockaddr *)&addr_in, sizeof(addr_in));
  } else { // Unix Domain Sockets for POSIX IPC
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    addr_un.sun_family = AF_UNIX;
    strncpy(addr_un.sun_path, DAPP_IPC_SOCKET_PATH, sizeof(addr_un.sun_path) - 1);
    ret = bind(sock, (struct sockaddr *)&addr_un, sizeof(addr_un));
  }

  if (ret < 0) {
    LOG_E(E3AP, "Bind in setup inbound connection failed: %s\n", strerror(errno));
    return ret;
  }

  ret = listen(sock, 5);
  
  if (ret < 0) {
    LOG_E(E3AP, "Bind in setup inbound connection failed: %s\n", strerror(errno));
    return ret;
  }

  self->inbound_socket = (void *)(intptr_t)sock;
  self->inbound_connection_socket = accept((intptr_t)self->inbound_socket, NULL, NULL);
    if (self->inbound_connection_socket <= 0) {
    LOG_E(E3AP, "Accept in setup inbound connection failed: %s\n", strerror(errno));
  }

  return ret;
}

int posix_receive(E3Connector *self, void *buffer, size_t buffer_size) {
    return recv(self->inbound_connection_socket, buffer, buffer_size, 0);
}

int posix_setup_outbound_connection(E3Connector *self) {
    int sock;
    struct sockaddr_in addr_in;
    struct sockaddr_un addr_un;
    int ret;
    if (strncmp(self->outbound_endpoint, "sctp", 4) == 0) {
      sock = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
      addr_in.sin_family = AF_INET;
      addr_in.sin_port = htons(9991);
      addr_in.sin_addr.s_addr = inet_addr("127.0.0.1");
      ret = connect(sock, (struct sockaddr *)&addr_in, sizeof(addr_in));
    } else if (strncmp(self->outbound_endpoint, "tcp", 3) == 0) {
      sock = socket(AF_INET, SOCK_STREAM, 0);
      addr_in.sin_family = AF_INET;
      addr_in.sin_port = htons(9991);
      addr_in.sin_addr.s_addr = inet_addr("127.0.0.1");
      ret = connect(sock, (struct sockaddr *)&addr_in, sizeof(addr_in));
    } else { // Unix Domain Sockets for POSIX IPC
      sock = socket(AF_UNIX, SOCK_STREAM, 0);
      addr_un.sun_family = AF_UNIX;
      strncpy(addr_un.sun_path, E3_IPC_SOCKET_PATH, sizeof(addr_un.sun_path) - 1);
      ret = connect(sock, (struct sockaddr *)&addr_un, sizeof(addr_un));
    }
    self->outbound_socket = (void *)(intptr_t)sock;
    return ret;
}

int posix_send(E3Connector *self, const uint8_t *payload, size_t payload_size) {
    return send_in_chunks((intptr_t)self->outbound_socket, payload, payload_size);
}

void posix_dispose(E3Connector *self) {
  if (self->inbound_connection_socket != UNUSED_SOCKET) {
    close(self->inbound_connection_socket);
  }

  if (self->setup_connection_socket != UNUSED_SOCKET) {
    close(self->setup_connection_socket);
  }

  close((intptr_t)self->inbound_socket);
  close((intptr_t)self->outbound_socket);
  close((intptr_t)self->setup_socket);

  // Unlink the IPC socket files if needed
  if (strncmp(self->setup_endpoint, E3_IPC_SETUP_PATH, strlen(E3_IPC_SETUP_PATH)) == 0) {
    unlink(E3_IPC_SETUP_PATH);
  }
  if (strncmp(self->inbound_endpoint, E3_IPC_SOCKET_PATH, strlen(E3_IPC_SOCKET_PATH)) == 0) {
    unlink(E3_IPC_SOCKET_PATH);
  }
  if (strncmp(self->outbound_endpoint, DAPP_IPC_SOCKET_PATH, strlen(DAPP_IPC_SOCKET_PATH)) == 0) {
    unlink(DAPP_IPC_SOCKET_PATH);
  }

  remove(IPC_BASE_DIR);

  // Free the connector memory
  free(self);
}


// Factory function
E3Connector *create_connector(const char *link_layer, const char *transport_layer)
{
  E3Connector *connector = (E3Connector *)malloc(sizeof(E3Connector));

  // Handle ZeroMQ-based configurations
  if (strncmp(link_layer, "zmq", 3) == 0) {
    connector->context = zmq_ctx_new();

    // Rule of thumb is 1 thread per GBps, but we are crazy
    int io_threads = 2; // Set the number of I/O threads for ZMQ (1 is default)
    zmq_ctx_set(connector->context, ZMQ_IO_THREADS, io_threads);
    if (zmq_ctx_get(connector->context, ZMQ_IO_THREADS) != io_threads) {
      free(connector);
      LOG_E(E3AP, "Unable to set the I/O threads to ZMQ context\n");
      return NULL;
    }

    // Set endpoints based on the transport layer
    if (strncmp(transport_layer, "ipc", 3) == 0) {
      connector->setup_endpoint = "ipc://" E3_IPC_SETUP_PATH;
      connector->inbound_endpoint = "ipc://" DAPP_IPC_SOCKET_PATH;
      connector->outbound_endpoint = "ipc://" E3_IPC_SOCKET_PATH;
    } else if (strncmp(transport_layer, "tcp", 3) == 0) {
      connector->setup_endpoint = "tcp://127.0.0.1:9990";
      connector->inbound_endpoint = "tcp://127.0.0.1:9999";
      connector->outbound_endpoint = "tcp://127.0.0.1:9991";
    } else if (strncmp(transport_layer, "sctp", 4) == 0) {
      // Not actually working because SCTP is not implemented in zeromq
      connector->setup_endpoint = "sctp://127.0.0.1:9990";
      connector->inbound_endpoint = "sctp://127.0.0.1:9999";
      connector->outbound_endpoint = "sctp://127.0.0.1:9991";
    } else {
      free(connector);
      LOG_E(E3AP, "Unsupported transport layer for ZeroMQ: %s\n", transport_layer);
      return NULL;
    }

    LOG_I(E3AP, "Endpoint setup %s\n", connector->setup_endpoint);
    LOG_I(E3AP, "Endpoint inbound %s\n", connector->inbound_endpoint);
    LOG_I(E3AP, "Endpoint outbound %s\n", connector->outbound_endpoint);

    connector->setup_initial_connection = zeromq_setup_initial_connection;
    connector->recv_setup_request = zeromq_recv_setup_request;
    connector->send_response = zeromq_send_response;
    connector->setup_inbound_connection = zeromq_setup_inbound_connection;
    connector->receive = zeromq_receive;
    connector->setup_outbound_connection = zeromq_setup_outbound_connection;
    connector->send = zeromq_send;
    connector->dispose = zeromq_dispose;

    // Handle POSIX-based configurations
  } else if (strncmp(link_layer, "posix", 5) == 0) {
    // Set endpoints based on the transport layer
    if (strncmp(transport_layer, "ipc", 3) == 0) {
      connector->setup_endpoint = E3_IPC_SETUP_PATH;
      connector->inbound_endpoint = DAPP_IPC_SOCKET_PATH;
      connector->outbound_endpoint = E3_IPC_SOCKET_PATH;
    } else if (strncmp(transport_layer, "tcp", 3)
               == 0) { // these are not used at the moment and the code could benefit from using them
      connector->setup_endpoint = "tcp://127.0.0.1:9990";
      connector->inbound_endpoint = "tcp://127.0.0.1:9999";
      connector->outbound_endpoint = "tcp://127.0.0.1:9991";
    } else if (strncmp(transport_layer, "sctp", 4) == 0) {
      connector->setup_endpoint = "sctp://127.0.0.1:9990";
      connector->inbound_endpoint = "sctp://127.0.0.1:9999";
      connector->outbound_endpoint = "sctp://127.0.0.1:9991";
    } else {
      free(connector);
      LOG_E(E3AP, "Unsupported transport layer for POSIX: %s\n", transport_layer);
      return NULL;
    }

    connector->inbound_connection_socket = UNUSED_SOCKET;
    connector->setup_connection_socket = UNUSED_SOCKET;

    connector->setup_initial_connection = posix_setup_initial_connection;
    connector->recv_setup_request = posix_recv_setup_request;
    connector->send_response = posix_send_response;
    connector->setup_inbound_connection = posix_setup_inbound_connection;
    connector->receive = posix_receive;
    connector->setup_outbound_connection = posix_setup_outbound_connection;
    connector->send = posix_send;
    connector->dispose = posix_dispose;

  } else {
    free(connector);
    LOG_E(E3AP, "Unsupported link layer: %s\n", link_layer);
    return NULL;
  }

  // Create folder for IPC endpoints if needed
  if (strncmp(transport_layer, "ipc", 3) == 0) {
    struct stat st = {0};
    // Check if the directory exists
    if (stat(IPC_BASE_DIR, &st) == -1) {
      // Directory does not exist, so create it with permissions 0777
      if (mkdir(IPC_BASE_DIR, 0777) == -1) {
        LOG_E(E3AP, "Failed to create endpoint folder for IPC: %s", strerror(errno));
      } else {
        LOG_I(E3AP, "Directory %s created with permissions 0777\n", IPC_BASE_DIR);
      }
    } else {
      // Directory exists, set permissions to 0777
      LOG_D(E3AP, "Directory %s already exists, setting permissions to 0777\n", IPC_BASE_DIR);
      if (chmod(IPC_BASE_DIR, 0777) != 0) {
        LOG_E(E3AP, "Failed to set permissions on /tmp/dapps directory: %s", strerror(errno));
        return NULL;
      }
    }

    // Change the group ownership to "dapp"
    struct group *grp = getgrnam("dapp");
    if (grp == NULL) {
      LOG_E(E3AP, "Failed to get group gid for IPC: %s", strerror(errno));
      return NULL;
    }
    if (chown(IPC_BASE_DIR, -1, grp->gr_gid) == -1) { // -1 for uid to keep current owner
      LOG_E(E3AP, "Failed to chown folder for IPC: %s", strerror(errno));
      return NULL;
    } else {
      LOG_D(E3AP, "Group ownership of %s changed to 'dapp'\n", IPC_BASE_DIR);
    }
  }

  return connector;
}
