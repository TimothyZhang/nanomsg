/*
    Copyright (c) 2012-2014 Martin Sustrik  All rights reserved.
    Copyright (c) 2013 GoPivotal, Inc.  All rights reserved.
    Copyright 2016 Garrett D'Amore <garrett@damore.org>

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom
    the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#ifndef NN_TRANSPORT_INCLUDED
#define NN_TRANSPORT_INCLUDED

#include "nn.h"

#include "aio/fsm.h"

#include "utils/list.h"
#include "utils/msg.h"

#include <stddef.h>

/*  This is the API between the nanomsg core and individual transports. */

struct nn_sock;
struct nn_cp;

/******************************************************************************/
/*  Container for transport-specific socket options.                          */
/******************************************************************************/

struct nn_optset;

struct nn_optset_vfptr {
    void (*destroy) (struct nn_optset *self);
    int (*setopt) (struct nn_optset *self, int option, const void *optval,
        size_t optvallen);
    int (*getopt) (struct nn_optset *self, int option, void *optval,
        size_t *optvallen);
};

struct nn_optset {
    const struct nn_optset_vfptr *vfptr;
};

/******************************************************************************/
/*  The base class for endpoints.                                             */
/******************************************************************************/

/*  The best way to think about endpoints is that endpoint is an object created
    by each nn_bind() or nn_connect() call. Each endpoint is associated with
    exactly one address string (e.g. "tcp://127.0.0.1:5555"). */

struct nn_ep;

struct nn_ep_vfptr {

    /*  Ask the endpoint to stop itself. The endpoint is allowed to linger
        to send the pending outbound data. When done, it reports the fact by
        invoking nn_ep_stopped() function. */
    void (*stop) (struct nn_ep *);

    /*  Deallocate the endpoint object. */
    void (*destroy) (struct nn_ep *);
};

/*  Gets the opaque value stored by a transport at setup time. */
void *nn_ep_tran_private (struct nn_ep *);

/*  Set up an ep for use by a transport.  The final opaque argument can be
    accessed later by calling nn_ep_tran_private(). */
void nn_ep_tran_setup (struct nn_ep *, const struct nn_ep_vfptr *, void *);

/*  Notify the user that stopping is done. */
void nn_ep_stopped (struct nn_ep *);

/*  Returns the AIO context associated with the endpoint. */
struct nn_ctx *nn_ep_getctx (struct nn_ep *);

/*  Returns the address string associated with this endpoint. */
const char *nn_ep_getaddr (struct nn_ep *self);

/*  Retrieve value of a socket option. */
void nn_ep_getopt (struct nn_ep *, int level, int option,
    void *optval, size_t *optvallen);

/*  Returns 1 if the specified socket type is a valid peer for this socket,
    or 0 otherwise. */
int nn_ep_ispeer (struct nn_ep *, int socktype);

/*  Returns 1 if the ep's are valid peers for each other, 0 otherwise. */
int nn_ep_ispeer_ep (struct nn_ep *, struct nn_ep *);

/*  Notifies a monitoring system the error on this endpoint  */
void nn_ep_set_error(struct nn_ep*, int errnum);

/*  Notifies a monitoring system that error is gone  */
void nn_ep_clear_error(struct nn_ep *);

/*  Increments statistics counters in the socket structure  */
void nn_ep_stat_increment(struct nn_ep *, int name, int increment);


/******************************************************************************/
/*  The base class for pipes.                                                 */
/******************************************************************************/

/*  Pipe represents one "connection", i.e. perfectly ordered uni- or
    bi-directional stream of messages. One endpoint can create multiple pipes
    (for example, bound TCP socket is an endpoint, individual accepted TCP
    connections are represented by pipes. */

struct nn_pipebase;

/*  This value is returned by pipe's send and recv functions to signalise that
    more sends/recvs are not possible at the moment. From that moment on,
    the core will stop invoking the function. To re-establish the message
    flow nn_pipebase_received (respectively nn_pipebase_sent) should
    be called. */
#define NN_PIPEBASE_RELEASE 1

/*  Specifies that received message is already split into header and body.
    This flag is used only by inproc transport to avoid merging and re-splitting
    the messages passed with a single process. */
#define NN_PIPEBASE_PARSED 2

struct nn_pipebase_vfptr {

    /*  Send a message to the network. The function can return either error
        (negative number) or any combination of the flags defined above. */
    int (*send) (struct nn_pipebase *self, struct nn_msg *msg);

    /*  Receive a message from the network. The function can return either error
        (negative number) or any combination of the flags defined above. */
    int (*recv) (struct nn_pipebase *self, struct nn_msg *msg);
};

/*  Endpoint specific options. Same restrictions as for nn_pipebase apply  */
struct nn_ep_options
{
    int sndprio;
    int rcvprio;
    int ipv4only;
};

/*  The member of this structure are used internally by the core. Never use
    or modify them directly from the transport. */
struct nn_pipebase {
    struct nn_fsm fsm;
    const struct nn_pipebase_vfptr *vfptr;
    uint8_t state;
    uint8_t instate;
    uint8_t outstate;
    struct nn_sock *sock;
    void *data;
    struct nn_fsm_event in;
    struct nn_fsm_event out;
    struct nn_ep_options options;
};

/*  Initialise the pipe.  */
void nn_pipebase_init (struct nn_pipebase *self,
    const struct nn_pipebase_vfptr *vfptr, struct nn_ep *ep);

/*  Terminate the pipe. */
void nn_pipebase_term (struct nn_pipebase *self);

/*  Call this function once the connection is established. */
int nn_pipebase_start (struct nn_pipebase *self);

/*  Call this function once the connection is broken. */
void nn_pipebase_stop (struct nn_pipebase *self);

/*  Call this function when new message was fully received. */
void nn_pipebase_received (struct nn_pipebase *self);

/*  Call this function when current outgoing message was fully sent. */
void nn_pipebase_sent (struct nn_pipebase *self);

/*  Retrieve value of a socket option. */
void nn_pipebase_getopt (struct nn_pipebase *self, int level, int option,
    void *optval, size_t *optvallen);

/*  Returns 1 is the specified socket type is a valid peer for this socket,
    or 0 otherwise. */
int nn_pipebase_ispeer (struct nn_pipebase *self, int socktype);

/******************************************************************************/
/*  The transport class.                                                      */
/******************************************************************************/

struct nn_transport {

    /*  Name of the transport as it appears in the connection strings ("tcp",
        "ipc", "inproc" etc. */
    const char *name;

    /*  ID of the transport. */
    int id;

    /*  Following methods are guarded by a global critical section. Two of these
        function will never be invoked in parallel. The first is called when
        the library is initialised, the second one when it is terminated, i.e.
        when there are no more open sockets. Either of them can be set to NULL
        if no specific initialisation/termination is needed. */
    void (*init) (void);
    void (*term) (void);

    /*  Each of these functions creates an endpoint and sets up the newly
        established endpoint in 'ep' parameter using nn_ep_tran_setup ().
        These functions are guarded by a socket-wide critical section.
        Two of these function will never be invoked in parallel on the same
        socket. */
    int (*bind) (struct nn_ep *);
    int (*connect) (struct nn_ep *);

    /*  Create an object to hold transport-specific socket options.
        Set this member to NULL in case there are no transport-specific
        socket options available. */
    struct nn_optset *(*optset) (void);

    /*  This member is used exclusively by the core. Never touch it directly
        from the transport. */
    struct nn_list_item item;
};

#endif
