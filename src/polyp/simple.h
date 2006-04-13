#ifndef foosimplehfoo
#define foosimplehfoo

/* $Id$ */

/***
  This file is part of polypaudio.
 
  polypaudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2 of the License,
  or (at your option) any later version.
 
  polypaudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.
 
  You should have received a copy of the GNU Lesser General Public License
  along with polypaudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#include <sys/types.h>

#include <polyp/sample.h>
#include <polyp/def.h>
#include <polyp/cdecl.h>

/** \page simple Simple API
 *
 * \section overv_sec Overview
 *
 * The simple API is designed for applications with very basic sound
 * playback or capture needs. It can only support a single stream per
 * connection and has no handling of complex features like events, channel
 * mappings and volume control. It is, however, very simple to use and
 * quite sufficent for many programs.
 *
 * \section conn_sec Connecting
 *
 * The first step before using the sound system is to connect to the
 * server. This is normally done this way:
 *
 * \code
 * pa_simple *s;
 * pa_sample_spec ss;
 *
 * ss.format = PA_SAMPLE_S16_NE;
 * ss.channels = 2;
 * ss.rate = 44100;
 *
 * s = pa_simple_new(NULL,               // Use the default server.
 *                   "Fooapp",           // Our application's name.
 *                   PA_STREAM_PLAYBACK,
 *                   NULL,               // Use the default device.
 *                   "Music",            // Description of our stream.
 *                   &ss,                // Our sample format.
 *                   NULL,               // Use default buffering attributes.
 *                   NULL,               // Ignore error code.
 *                   );
 * \endcode
 *
 * At this point a connected object is returned, or NULL if there was a
 * problem connecting.
 *
 * \section transfer_sec Transferring data
 *
 * Once the connection is established to the server, data can start flowing.
 * Using the connection is very similar to the normal read() and write()
 * system calls. The main difference is that they're call pa_simple_read()
 * and pa_simple_write(). Note that these operations always block.
 *
 * \section ctrl_sec Buffer control
 *
 * If a playback stream is used then a few other operations are available:
 *
 * \li pa_simple_drain() - Will wait for all sent data to finish playing.
 * \li pa_simple_flush() - Will throw away all data currently in buffers.
 * \li pa_simple_get_playback_latency() - Will return the total latency of
 *                                        the playback pipeline.
 *
 * \section cleanup_sec Cleanup
 *
 * Once playback or capture is complete, the connection should be closed
 * and resources freed. This is done through:
 *
 * \code
 * pa_simple_free(s);
 * \endcode
 */

/** \file
 * A simple but limited synchronous playback and recording
 * API. This is a synchronous, simplified wrapper around the standard
 * asynchronous API. */

/** \example pacat-simple.c
 * A simple playback tool using the simple API */

/** \example parec-simple.c
 * A simple recording tool using the simple API */

PA_C_DECL_BEGIN

/** \struct pa_simple
 * An opaque simple connection object */
typedef struct pa_simple pa_simple;

/** Create a new connection to the server */
pa_simple* pa_simple_new(
    const char *server,                 /**< Server name, or NULL for default */
    const char *name,                   /**< A descriptive name for this client (application name, ...) */
    pa_stream_direction_t dir,       /**< Open this stream for recording or playback? */
    const char *dev,                    /**< Sink (resp. source) name, or NULL for default */
    const char *stream_name,            /**< A descriptive name for this client (application name, song title, ...) */
    const pa_sample_spec *ss,    /**< The sample type to use */
    const pa_buffer_attr *attr,  /**< Buffering attributes, or NULL for default */
    int *error                          /**< A pointer where the error code is stored when the routine returns NULL. It is OK to pass NULL here. */
    );

/** Close and free the connection to the server. The connection objects becomes invalid when this is called. */
void pa_simple_free(pa_simple *s);

/** Write some data to the server */
int pa_simple_write(pa_simple *s, const void*data, size_t length, int *error);

/** Wait until all data already written is played by the daemon */
int pa_simple_drain(pa_simple *s, int *error);

/** Read some data from the server */
int pa_simple_read(pa_simple *s, void*data, size_t length, int *error);

/** Return the playback latency. \since 0.5 */
pa_usec_t pa_simple_get_playback_latency(pa_simple *s, int *error);

/** Flush the playback buffer. \since 0.5 */
int pa_simple_flush(pa_simple *s, int *error);

PA_C_DECL_END

#endif
