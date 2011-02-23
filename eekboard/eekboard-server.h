/* 
 * Copyright (C) 2010-2011 Daiki Ueno <ueno@unixuser.org>
 * Copyright (C) 2010-2011 Red Hat, Inc.
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef EEKBOARD_SERVER_H
#define EEKBOARD_SERVER_H 1

#include <gio/gio.h>
#include "eekboard/eekboard-context.h"

G_BEGIN_DECLS

#define EEKBOARD_TYPE_SERVER (eekboard_server_get_type())
#define EEKBOARD_SERVER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), EEKBOARD_TYPE_SERVER, EekboardServer))
#define EEKBOARD_SERVER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), EEKBOARD_TYPE_SERVER, EekboardServerClass))
#define EEKBOARD_IS_SERVER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EEKBOARD_TYPE_SERVER))
#define EEKBOARD_IS_SERVER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), EEKBOARD_TYPE_SERVER))
#define EEKBOARD_SERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EEKBOARD_TYPE_SERVER, EekboardServerClass))

typedef struct _EekboardServer EekboardServer;
typedef struct _EekboardServerClass EekboardServerClass;
typedef struct _EekboardServerPrivate EekboardServerPrivate;

struct _EekboardServer {
    /*< private >*/
    GDBusProxy parent;

    EekboardServerPrivate *priv;
};

struct _EekboardServerClass {
    /*< private >*/
    GDBusProxyClass parent_class;

    /*< private >*/
    /* padding */
    gpointer pdummy[24];
};

GType            eekboard_server_get_type        (void) G_GNUC_CONST;

EekboardServer  *eekboard_server_new             (GDBusConnection *connection,
                                                  GCancellable    *cancellable);
EekboardContext *eekboard_server_create_context  (EekboardServer  *server,
                                                  const gchar     *client_name,
                                                  GCancellable    *cancellable);
void             eekboard_server_push_context    (EekboardServer  *server,
                                                  EekboardContext *context,
                                                  GCancellable    *cancellable);
void             eekboard_server_pop_context     (EekboardServer  *server,
                                                  GCancellable    *cancellable);
void             eekboard_server_destroy_context (EekboardServer  *server,
                                                  EekboardContext *context,
                                                  GCancellable    *cancellable);

G_END_DECLS
#endif  /* EEKBOARD_SERVER_H */