#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>

#include <act_log.h>
#include <act_ipc.h>
#include "net_basic.h"

//! Number of pending incoming connections queue will hold.
#define BACKLOG 10

/** \brief Send a network message to a programme
 * \param prog The programme to which to send the message
 * \param msg The message to send
 * \return TRUE on success, FALSE on error
 */
unsigned char act_send(struct act_prog *prog, struct act_msg *msg)
{
  if ((prog == NULL) || (msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return FALSE;
  }
  if (send(prog->sockfd, msg, sizeof(struct act_msg), 0) == sizeof(struct act_msg))
    return TRUE;
  act_log_error(act_log_msg("Error sending message to %s (fd %d) - %s.", prog->name, prog->sockfd, strerror(errno)));
  return FALSE;
}

/** \brief Receive a network message from a programme
 * \param prog The programme from which to receive message
 * \param msg The message structure where the new message will be copied
 * \return TRUE if a message was received, FALSE otherwise
 */
unsigned char act_recv(struct act_prog *prog, struct act_msg *msg)
{
  if ((prog == NULL) || (msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return FALSE;
  }
  struct act_msg msgbuf;
  int numbytes;
  numbytes = recv(prog->sockfd, &msgbuf, sizeof(struct act_msg), MSG_DONTWAIT);
  if (numbytes == sizeof(struct act_msg))
  {
    memcpy(msg, &msgbuf, sizeof(struct act_msg));
    return TRUE;
  }
  if ((numbytes == -1) && (abs(errno) != EAGAIN))
    act_log_error(act_log_msg("Error receiving message from %s (fd %d) - %s.", prog->name, prog->sockfd, strerror(errno)));
  return FALSE;
}

int net_setup(const char *port)
{
  int sockfd, retval;
  struct addrinfo hints, *servinfo, *p;
  
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if ((retval = getaddrinfo(NULL, port, &hints, &servinfo)) != 0)
  {
    act_log_error(act_log_msg("Failed to get address info - %s.", gai_strerror(retval)));
    return -1;
  }
  
  for (p = servinfo; p != NULL; p = p->ai_next)
  {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) 
      continue;

    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
    {
      close(sockfd);
      continue;
    }
    break;
  }
  
  if (p == NULL)
  {
    act_log_error(act_log_msg("Failed to bind - %s.", gai_strerror(retval)));
    return -1;
  }
  freeaddrinfo(servinfo); // all done with this structure
  
  if (listen(sockfd, BACKLOG) == -1)
  {
    act_log_error(act_log_msg("Failed to listen - %s.", strerror(errno)));
    return -1;
  }
  
  retval = fcntl(sockfd, F_GETFL, 0);
  fcntl(sockfd, F_SETFL, retval | O_NONBLOCK);
  return sockfd;
}
