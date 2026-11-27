/* client_handler.h - per-connection thread: login, then command loop. */
#ifndef CLIENT_HANDLER_H
#define CLIENT_HANDLER_H

/* Thread entry point. `arg` is the session_t* opened for this connection.
 * The thread closes the session before returning. */
void *client_thread(void *arg);

#endif
