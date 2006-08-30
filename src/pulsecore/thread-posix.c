/* $Id$ */

/***
  This file is part of PulseAudio.
 
  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2 of the License,
  or (at your option) any later version.
 
  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.
 
  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <pthread.h>
#include <atomic_ops.h>

#include <pulse/xmalloc.h>

#include "thread.h"

#define ASSERT_SUCCESS(x) do { \
    int _r = (x); \
    assert(_r == 0); \
} while(0)

struct pa_thread {
    pthread_t id;
    pa_thread_func_t thread_func;
    void *userdata;
    AO_t running;
};

struct pa_tls {
    pthread_key_t key;
};

static pa_tls *thread_tls;
static pthread_once_t thread_tls_once = PTHREAD_ONCE_INIT;

static void thread_tls_once_func(void) {
    thread_tls = pa_tls_new(NULL);
    assert(thread_tls);
}

static void* internal_thread_func(void *userdata) {
    pa_thread *t = userdata;
    assert(t);

    t->id = pthread_self();

    ASSERT_SUCCESS(pthread_once(&thread_tls_once, thread_tls_once_func));
    pa_tls_set(thread_tls, t);
    
    AO_store_release_write(&t->running, 1);
    t->thread_func(t->userdata);
    AO_store_release_write(&t->running, 0);
    
    return NULL;
}

pa_thread* pa_thread_new(pa_thread_func_t thread_func, void *userdata) {
    pa_thread *t;

    t = pa_xnew(pa_thread, 1);
    t->thread_func = thread_func;
    t->userdata = userdata;

    if (pthread_create(&t->id, NULL, internal_thread_func, t) < 0) {
        pa_xfree(t);
        return NULL;
    }

    return t;
}

int pa_thread_is_running(pa_thread *t) {
    assert(t);

    return !!AO_load_acquire_read(&t->running);
}

void pa_thread_free(pa_thread *t) {
    assert(t);
    
    pa_thread_join(t);
    pa_xfree(t);
}

int pa_thread_join(pa_thread *t) {
    assert(t);
    
    return pthread_join(t->id, NULL);
}

pa_thread* pa_thread_self(void) {
    ASSERT_SUCCESS(pthread_once(&thread_tls_once, thread_tls_once_func));
    return pa_tls_get(thread_tls);
}

pa_tls* pa_tls_new(pa_free_cb_t free_cb) {
    pa_tls *t;

    t = pa_xnew(pa_tls, 1);

    if (pthread_key_create(&t->key, free_cb) < 0) {
        pa_xfree(t);
        return NULL;
    }
        
    return t;
}

void pa_tls_free(pa_tls *t) {
    assert(t);

    ASSERT_SUCCESS(pthread_key_delete(t->key));
    pa_xfree(t);
}

void *pa_tls_get(pa_tls *t) {
    assert(t);
    
    return pthread_getspecific(t->key);
}

void *pa_tls_set(pa_tls *t, void *userdata) {
    void *r;
    
    r = pthread_getspecific(t->key);
    ASSERT_SUCCESS(pthread_setspecific(t->key, userdata));
    return r;
}
